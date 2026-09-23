"""OIDC bearer-token and trusted Kerberos reverse-proxy authentication."""

from __future__ import annotations

import asyncio
import hmac
from dataclasses import dataclass
from typing import Iterable

import jwt
from fastapi import HTTPException, Request, WebSocket, status
from jwt import PyJWKClient

from .config import Settings


@dataclass(frozen=True)
class Principal:
    user_id: str
    username: str
    display_name: str
    roles: frozenset[str]

    def require(self, permission: str) -> None:
        if permission not in self.roles and "amalia:admin" not in self.roles:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Insufficient permissions")


class Authenticator:
    def __init__(self, settings: Settings):
        self.settings = settings
        self.jwks = PyJWKClient(
            settings.oidc_jwks_url or settings.oidc_issuer.rstrip("/") + "/protocol/openid-connect/certs",
            cache_keys=True,
        )

    @staticmethod
    def _roles(claims: dict) -> frozenset[str]:
        roles: set[str] = set(claims.get("roles", []))
        roles.update(claims.get("realm_access", {}).get("roles", []))
        for client_roles in claims.get("resource_access", {}).values():
            roles.update(client_roles.get("roles", []))
        return frozenset(roles)

    def _from_claims(self, claims: dict) -> Principal:
        subject = str(claims.get("sub", "")).strip()
        username = str(claims.get("preferred_username", subject)).strip()
        if not subject or not username or len(subject) > 200 or len(username) > 200:
            raise HTTPException(status_code=401, detail="Invalid identity claims")
        return Principal(
            user_id=subject,
            username=username,
            display_name=str(claims.get("name", username))[:200],
            roles=self._roles(claims),
        )

    def _oidc(self, authorization: str | None) -> Principal:
        if not authorization or not authorization.startswith("Bearer "):
            raise HTTPException(status_code=401, detail="Bearer token required")
        token = authorization[7:].strip()
        try:
            key = self.jwks.get_signing_key_from_jwt(token).key
            claims = jwt.decode(
                token,
                key,
                algorithms=["RS256", "ES256"],
                audience=self.settings.oidc_audience,
                issuer=self.settings.oidc_issuer,
                options={"require": ["exp", "iat", "sub", "iss", "aud"]},
            )
        except jwt.PyJWTError as error:
            raise HTTPException(status_code=401, detail="Invalid access token") from error
        return self._from_claims(claims)

    def _proxy(self, headers) -> Principal:
        configured = self.settings.proxy_shared_secret or ""
        supplied = headers.get("x-amalia-proxy-secret", "")
        if not configured or not hmac.compare_digest(configured, supplied):
            raise HTTPException(status_code=401, detail="Trusted proxy authentication required")
        username = headers.get("x-remote-user", "").strip()
        if not username or len(username) > 200:
            raise HTTPException(status_code=401, detail="Invalid proxy identity")
        roles = frozenset(item.strip() for item in headers.get("x-amalia-roles", "").split(",") if item.strip())
        return Principal(
            user_id=headers.get("x-amalia-user-id", username)[:200],
            username=username,
            display_name=headers.get("x-amalia-display-name", username)[:200],
            roles=roles,
        )

    @staticmethod
    def _test(headers) -> Principal:
        username = headers.get("x-test-user", "test.user")[:200]
        roles = headers.get(
            "x-test-roles", "journal:read,journal:write,journal:export,journal:delete"
        )
        return Principal(
            user_id=headers.get("x-test-user-id", username)[:200],
            username=username,
            display_name=headers.get("x-test-display-name", username)[:200],
            roles=frozenset(item.strip() for item in roles.split(",") if item.strip()),
        )

    def authenticate_headers(self, headers) -> Principal:
        if self.settings.auth_mode == "test":
            return self._test(headers)
        if self.settings.auth_mode == "proxy":
            return self._proxy(headers)
        return self._oidc(headers.get("authorization"))

    def request_principal(self, request: Request) -> Principal:
        return self.authenticate_headers(request.headers)

    async def websocket_principal(self, websocket: WebSocket) -> Principal:
        return await asyncio.to_thread(self.authenticate_headers, websocket.headers)
