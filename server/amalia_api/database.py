"""SQLite connection management and append-only journal operations."""

from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterator


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class Database:
    def __init__(self, path: Path, migrations_dir: Path):
        self.path = path
        self.migrations_dir = migrations_dir

    def connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.path, timeout=30, isolation_level=None)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys=ON")
        connection.execute("PRAGMA busy_timeout=30000")
        connection.execute("PRAGMA journal_mode=WAL")
        connection.execute("PRAGMA synchronous=FULL")
        return connection

    @contextmanager
    def connection(self) -> Iterator[sqlite3.Connection]:
        connection = self.connect()
        try:
            yield connection
        finally:
            connection.close()

    def migrate(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.connection() as connection:
            connection.execute(
                "CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL)"
            )
            applied = {row[0] for row in connection.execute("SELECT version FROM schema_migrations")}
            for migration in sorted(self.migrations_dir.glob("*.sql")):
                version = int(migration.name.split("_", 1)[0])
                if version in applied:
                    continue
                script = migration.read_text(encoding="utf-8")
                connection.executescript(script)
                connection.execute(
                    "INSERT INTO schema_migrations(version,applied_at) VALUES(?,?)", (version, utc_now())
                )

    def create_task(self, name: str, task_number: str, user_id: str) -> sqlite3.Row:
        now = utc_now()
        with self.connection() as connection:
            cursor = connection.execute(
                "INSERT INTO tasks(name,task_number,created_at,created_by,status) VALUES(?,?,?,?, 'open')",
                (name, task_number, now, user_id),
            )
            return connection.execute("SELECT * FROM tasks WHERE id=?", (cursor.lastrowid,)).fetchone()

    def list_tasks(self, include_deleted: bool = False) -> list[sqlite3.Row]:
        where = "" if include_deleted else "WHERE deleted_at IS NULL"
        with self.connection() as connection:
            return connection.execute(f"SELECT * FROM tasks {where} ORDER BY created_at DESC").fetchall()

    def get_task(self, task_id: int) -> sqlite3.Row | None:
        with self.connection() as connection:
            return connection.execute("SELECT * FROM tasks WHERE id=?", (task_id,)).fetchone()

    def close_task(self, task_id: int) -> sqlite3.Row | None:
        with self.connection() as connection:
            connection.execute(
                "UPDATE tasks SET status='closed',ended_at=? WHERE id=? AND status='open' AND deleted_at IS NULL",
                (utc_now(), task_id),
            )
            return connection.execute("SELECT * FROM tasks WHERE id=?", (task_id,)).fetchone()

    def append_entry(
        self, task_id: int, idempotency_key: str, client_timestamp: str | None,
        user_id: str, username: str, display_name: str, workstation_id: str, text: str,
    ) -> tuple[sqlite3.Row, bool]:
        """Atomically allocate sequence number; return (row, was_created)."""
        with self.connection() as connection:
            connection.execute("BEGIN IMMEDIATE")
            try:
                existing = connection.execute(
                    "SELECT * FROM log_entries WHERE idempotency_key=?", (idempotency_key,)
                ).fetchone()
                if existing:
                    connection.execute("COMMIT")
                    return existing, False
                task = connection.execute(
                    "SELECT status,deleted_at FROM tasks WHERE id=?", (task_id,)
                ).fetchone()
                if not task or task["deleted_at"] is not None:
                    raise LookupError("task not found")
                if task["status"] != "open":
                    raise PermissionError("task is not open")
                sequence = connection.execute(
                    "SELECT COALESCE(MAX(sequence_number),0)+1 FROM log_entries WHERE task_id=?", (task_id,)
                ).fetchone()[0]
                now = utc_now()
                cursor = connection.execute(
                    """INSERT INTO log_entries(
                         task_id,sequence_number,idempotency_key,server_timestamp,client_timestamp,
                         user_id,username,display_name,workstation_id,text,created_at
                       ) VALUES(?,?,?,?,?,?,?,?,?,?,?)""",
                    (task_id, sequence, idempotency_key, now, client_timestamp, user_id,
                     username, display_name, workstation_id, text, now),
                )
                row = connection.execute("SELECT * FROM log_entries WHERE id=?", (cursor.lastrowid,)).fetchone()
                connection.execute("COMMIT")
                return row, True
            except Exception:
                connection.execute("ROLLBACK")
                raise

    def list_entries(self, task_id: int, after_sequence: int = 0) -> list[sqlite3.Row]:
        with self.connection() as connection:
            return connection.execute(
                "SELECT * FROM log_entries WHERE task_id=? AND sequence_number>? ORDER BY sequence_number",
                (task_id, after_sequence),
            ).fetchall()

    def record_export(self, task_id: int, export_format: str, user_id: str, sha256: str) -> sqlite3.Row:
        now = utc_now()
        with self.connection() as connection:
            connection.execute("BEGIN IMMEDIATE")
            task = connection.execute("SELECT id FROM tasks WHERE id=? AND deleted_at IS NULL", (task_id,)).fetchone()
            if not task:
                connection.execute("ROLLBACK")
                raise LookupError("task not found")
            cursor = connection.execute(
                "INSERT INTO exports(task_id,format,created_at,created_by,sha256) VALUES(?,?,?,?,?)",
                (task_id, export_format, now, user_id, sha256),
            )
            connection.execute(
                "UPDATE tasks SET exported_at=?,exported_by=? WHERE id=?", (now, user_id, task_id)
            )
            row = connection.execute("SELECT * FROM exports WHERE id=?", (cursor.lastrowid,)).fetchone()
            connection.execute("COMMIT")
            return row

    def delete_task_after_export(self, task_id: int, user_id: str, mode: str) -> str:
        now = utc_now()
        with self.connection() as connection:
            connection.execute("BEGIN IMMEDIATE")
            task = connection.execute("SELECT * FROM tasks WHERE id=?", (task_id,)).fetchone()
            if not task:
                connection.execute("ROLLBACK")
                raise LookupError("task not found")
            if not task["exported_at"] or not task["exported_by"]:
                connection.execute("ROLLBACK")
                raise PermissionError("successful export required")
            connection.execute(
                "INSERT INTO task_deletions(task_id,deleted_at,deleted_by,mode,last_exported_at,last_exported_by) VALUES(?,?,?,?,?,?)",
                (task_id, now, user_id, mode, task["exported_at"], task["exported_by"]),
            )
            if mode == "soft":
                connection.execute(
                    "UPDATE tasks SET status='deleted',deleted_at=?,deleted_by=? WHERE id=?",
                    (now, user_id, task_id),
                )
            else:
                connection.execute("DELETE FROM tasks WHERE id=?", (task_id,))
            connection.execute("COMMIT")
            return mode
