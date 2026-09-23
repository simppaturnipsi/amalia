"""Authenticated per-task WebSocket fan-out."""

from __future__ import annotations

import asyncio
from collections import defaultdict

from fastapi import WebSocket


class TaskConnectionManager:
    def __init__(self):
        self._connections: dict[int, set[WebSocket]] = defaultdict(set)
        self._lock = asyncio.Lock()

    async def connect(self, task_id: int, websocket: WebSocket) -> None:
        await websocket.accept()
        async with self._lock:
            self._connections[task_id].add(websocket)

    async def disconnect(self, task_id: int, websocket: WebSocket) -> None:
        async with self._lock:
            self._connections[task_id].discard(websocket)
            if not self._connections[task_id]:
                self._connections.pop(task_id, None)

    async def broadcast(self, task_id: int, event: dict) -> None:
        async with self._lock:
            targets = list(self._connections.get(task_id, ()))
        stale = []
        for websocket in targets:
            try:
                await websocket.send_json(event)
            except Exception:
                stale.append(websocket)
        for websocket in stale:
            await self.disconnect(task_id, websocket)
