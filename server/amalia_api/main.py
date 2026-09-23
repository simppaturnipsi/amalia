"""Amalia REST/WebSocket application factory."""

from __future__ import annotations

import hashlib
import logging
import re
import sqlite3
from contextlib import asynccontextmanager
from logging.handlers import RotatingFileHandler
from pathlib import Path

from fastapi import Depends, FastAPI, HTTPException, Query, Request, Response, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from . import __version__
from .auth import Authenticator, Principal
from .config import Settings
from .database import Database
from .exports import csv_export, pdf_export
from .models import LogEntryCreate, LogEntryOut, TaskCreate, TaskOut, row_dict
from .websocket import TaskConnectionManager


def configure_logging(path: Path) -> logging.Logger:
    path.parent.mkdir(parents=True, exist_ok=True)
    logger = logging.getLogger("amalia.api")
    logger.setLevel(logging.INFO)
    logger.handlers.clear()
    handler = RotatingFileHandler(path, maxBytes=10 * 1024 * 1024, backupCount=10, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
    logger.addHandler(handler)
    logger.propagate = False
    return logger


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_env()
    migrations = Path(__file__).resolve().parent.parent / "migrations"
    database = Database(settings.database_path, migrations)
    auth = Authenticator(settings)
    sockets = TaskConnectionManager()
    logger = configure_logging(settings.log_path)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        database.migrate()
        logger.info("Amalia API started version=%s bind=localhost", __version__)
        yield
        logger.info("Amalia API stopped")

    app = FastAPI(title="Amalia API", version=__version__, lifespan=lifespan,
                  docs_url=None if settings.auth_mode != "test" else "/docs")
    app.state.database = database
    app.state.settings = settings
    app.state.authenticator = auth
    app.state.sockets = sockets

    if settings.allowed_origins:
        app.add_middleware(
            CORSMiddleware,
            allow_origins=list(settings.allowed_origins),
            allow_credentials=False,
            allow_methods=["GET", "POST", "DELETE"],
            allow_headers=["Authorization", "Content-Type", "X-Request-ID"],
        )

    @app.middleware("http")
    async def security_headers(request: Request, call_next):
        response = await call_next(request)
        if response.status_code in {401, 403}:
            logger.warning("Authentication/authorization failure method=%s path=%s status=%s",
                           request.method, request.url.path, response.status_code)
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["Cache-Control"] = "no-store"
        return response

    @app.exception_handler(sqlite3.DatabaseError)
    async def database_error(_request: Request, error: sqlite3.DatabaseError):
        logger.error("Database error type=%s", type(error).__name__)
        return JSONResponse(status_code=500, content={"detail": "Database operation failed"})

    principal_dependency = auth.request_principal

    @app.get("/health")
    async def health():
        return {"status": "ok", "version": __version__}

    @app.get("/api/v1/me")
    async def me(principal: Principal = Depends(principal_dependency)):
        return {"user_id": principal.user_id, "username": principal.username,
                "display_name": principal.display_name, "roles": sorted(principal.roles)}

    @app.post("/api/v1/tasks", response_model=TaskOut, status_code=201)
    async def create_task(payload: TaskCreate, principal: Principal = Depends(principal_dependency)):
        principal.require("journal:write")
        try:
            return row_dict(database.create_task(payload.name, payload.task_number, principal.user_id))
        except sqlite3.IntegrityError as error:
            raise HTTPException(status_code=409, detail="Task number already exists") from error

    @app.get("/api/v1/tasks", response_model=list[TaskOut])
    async def list_tasks(principal: Principal = Depends(principal_dependency)):
        principal.require("journal:read")
        return [row_dict(row) for row in database.list_tasks()]

    @app.get("/api/v1/tasks/{task_id}", response_model=TaskOut)
    async def get_task(task_id: int, principal: Principal = Depends(principal_dependency)):
        principal.require("journal:read")
        task = database.get_task(task_id)
        if not task or task["deleted_at"] is not None:
            raise HTTPException(status_code=404, detail="Task not found")
        return row_dict(task)

    @app.post("/api/v1/tasks/{task_id}/close", response_model=TaskOut)
    async def close_task(task_id: int, principal: Principal = Depends(principal_dependency)):
        principal.require("journal:write")
        task = database.close_task(task_id)
        if not task:
            raise HTTPException(status_code=404, detail="Task not found")
        return row_dict(task)

    @app.post("/api/v1/tasks/{task_id}/entries", response_model=LogEntryOut)
    async def append_entry(task_id: int, payload: LogEntryCreate, response: Response,
                           principal: Principal = Depends(principal_dependency)):
        principal.require("journal:write")
        if len(payload.text) > settings.max_entry_length:
            raise HTTPException(status_code=422, detail="Entry text is too long")
        try:
            row, created = database.append_entry(
                task_id, str(payload.idempotency_key),
                payload.client_timestamp.isoformat() if payload.client_timestamp else None,
                principal.user_id, principal.username, principal.display_name,
                payload.workstation_id, payload.text,
            )
        except LookupError as error:
            raise HTTPException(status_code=404, detail="Task not found") from error
        except PermissionError as error:
            raise HTTPException(status_code=409, detail="Task is not open") from error
        output = row_dict(row)
        response.status_code = 201 if created else 200
        if created:
            await sockets.broadcast(task_id, {"type": "log_entry.created", "task_id": task_id, "entry": output})
        return output

    @app.get("/api/v1/tasks/{task_id}/entries", response_model=list[LogEntryOut])
    async def list_entries(task_id: int, after_sequence: int = Query(0, ge=0),
                           principal: Principal = Depends(principal_dependency)):
        principal.require("journal:read")
        task = database.get_task(task_id)
        if not task or task["deleted_at"] is not None:
            raise HTTPException(status_code=404, detail="Task not found")
        return [row_dict(row) for row in database.list_entries(task_id, after_sequence)]

    @app.post("/api/v1/tasks/{task_id}/export/{export_format}")
    async def export_task(task_id: int, export_format: str,
                          principal: Principal = Depends(principal_dependency)):
        principal.require("journal:export")
        task = database.get_task(task_id)
        if not task or task["deleted_at"] is not None:
            raise HTTPException(status_code=404, detail="Task not found")
        entries = database.list_entries(task_id)
        if export_format == "csv":
            content, media_type, suffix = csv_export(task, entries), "text/csv; charset=utf-8", "csv"
        elif export_format == "pdf":
            content, media_type, suffix = pdf_export(task, entries), "application/pdf", "pdf"
        else:
            raise HTTPException(status_code=404, detail="Unsupported export format")
        digest = hashlib.sha256(content).hexdigest()
        database.record_export(task_id, export_format, principal.user_id, digest)
        logger.info("Journal exported task_id=%s format=%s user_id=%s sha256=%s",
                    task_id, export_format, principal.user_id, digest)
        safe_number = re.sub(r"[^A-Za-z0-9_.-]+", "_", task["task_number"])[:100] or "task"
        filename = f"amalia-{safe_number}.{suffix}"
        return Response(content=content, media_type=media_type,
                        headers={"Content-Disposition": f'attachment; filename="{filename}"', "X-Content-SHA256": digest})

    @app.delete("/api/v1/tasks/{task_id}", status_code=204)
    async def delete_task(task_id: int, confirm: bool = Query(False),
                          principal: Principal = Depends(principal_dependency)):
        principal.require("journal:delete")
        if not confirm:
            raise HTTPException(status_code=409, detail="Explicit confirmation is required")
        try:
            mode = database.delete_task_after_export(task_id, principal.user_id, settings.deletion_mode)
        except LookupError as error:
            raise HTTPException(status_code=404, detail="Task not found") from error
        except PermissionError as error:
            raise HTTPException(status_code=409, detail="Successful export is required before deletion") from error
        logger.warning("Journal task deleted task_id=%s mode=%s user_id=%s", task_id, mode, principal.user_id)
        return Response(status_code=204)

    @app.websocket("/ws/tasks/{task_id}")
    async def task_socket(websocket: WebSocket, task_id: int):
        try:
            principal = await auth.websocket_principal(websocket)
            principal.require("journal:read")
            task = database.get_task(task_id)
            if not task or task["deleted_at"] is not None:
                await websocket.close(code=4404)
                return
        except HTTPException as error:
            logger.warning("WebSocket authentication failed task_id=%s status=%s", task_id, error.status_code)
            await websocket.close(code=4401 if error.status_code == 401 else 4403)
            return
        await sockets.connect(task_id, websocket)
        try:
            await websocket.send_json({"type": "connected", "task_id": task_id})
            while True:
                await websocket.receive_text()
        except WebSocketDisconnect:
            pass
        except Exception as error:
            logger.warning("WebSocket error task_id=%s type=%s", task_id, type(error).__name__)
        finally:
            await sockets.disconnect(task_id, websocket)

    return app


app = create_app()
