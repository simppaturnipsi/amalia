"""Environment-only configuration for the isolated API service."""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


def _bool(name: str, default: bool) -> bool:
    value = os.getenv(name)
    return default if value is None else value.casefold() in {"1", "true", "yes", "on"}


@dataclass(frozen=True)
class Settings:
    database_path: Path
    log_path: Path
    auth_mode: str
    oidc_issuer: str
    oidc_audience: str
    oidc_jwks_url: str | None
    proxy_shared_secret: str | None
    deletion_mode: str
    max_entry_length: int
    allowed_origins: tuple[str, ...]

    @classmethod
    def from_env(cls) -> "Settings":
        mode = os.getenv("AMALIA_AUTH_MODE", "oidc").casefold()
        if mode not in {"oidc", "proxy", "test"}:
            raise ValueError("AMALIA_AUTH_MODE must be oidc, proxy or test")
        deletion_mode = os.getenv("AMALIA_DELETION_MODE", "soft").casefold()
        if deletion_mode not in {"soft", "hard"}:
            raise ValueError("AMALIA_DELETION_MODE must be soft or hard")
        origins = tuple(item.strip() for item in os.getenv("AMALIA_ALLOWED_ORIGINS", "").split(",") if item.strip())
        return cls(
            database_path=Path(os.getenv("AMALIA_DATABASE", "/var/lib/amalia/amalia.db")),
            log_path=Path(os.getenv("AMALIA_LOG", "/var/log/amalia/amalia-api.log")),
            auth_mode=mode,
            oidc_issuer=os.getenv("AMALIA_OIDC_ISSUER", "https://ilona.example.invalid/realms/amalia"),
            oidc_audience=os.getenv("AMALIA_OIDC_AUDIENCE", "amalia-api"),
            oidc_jwks_url=os.getenv("AMALIA_OIDC_JWKS_URL"),
            proxy_shared_secret=os.getenv("AMALIA_PROXY_SHARED_SECRET"),
            deletion_mode=deletion_mode,
            max_entry_length=int(os.getenv("AMALIA_MAX_ENTRY_LENGTH", "10000")),
            allowed_origins=origins,
        )
