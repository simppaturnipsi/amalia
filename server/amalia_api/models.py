"""Validated API payloads."""

from __future__ import annotations

from datetime import datetime
from uuid import UUID

from pydantic import BaseModel, ConfigDict, Field, field_validator


class TaskCreate(BaseModel):
    name: str = Field(min_length=1, max_length=200)
    task_number: str = Field(min_length=1, max_length=100)

    @field_validator("name", "task_number")
    @classmethod
    def not_blank(cls, value: str) -> str:
        value = value.strip()
        if not value:
            raise ValueError("must not be blank")
        return value


class TaskOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)
    id: int
    name: str
    task_number: str
    created_at: str
    created_by: str
    status: str
    ended_at: str | None = None
    exported_at: str | None = None
    exported_by: str | None = None
    deleted_at: str | None = None


class LogEntryCreate(BaseModel):
    idempotency_key: UUID
    client_timestamp: datetime | None = None
    workstation_id: str = Field(min_length=1, max_length=200)
    text: str = Field(min_length=1, max_length=10000)

    @field_validator("workstation_id", "text")
    @classmethod
    def not_blank(cls, value: str) -> str:
        if not value.strip():
            raise ValueError("must not be blank")
        return value.strip() if len(value) < 201 else value


class LogEntryOut(BaseModel):
    id: int
    task_id: int
    sequence_number: int
    idempotency_key: str
    server_timestamp: str
    client_timestamp: str | None
    user_id: str
    username: str
    display_name: str
    workstation_id: str
    text: str
    created_at: str


def row_dict(row) -> dict:
    return dict(row)
