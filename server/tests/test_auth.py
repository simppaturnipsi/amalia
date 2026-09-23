from datetime import datetime, timedelta, timezone
from types import SimpleNamespace

import jwt
import pytest
from cryptography.hazmat.primitives.asymmetric.rsa import generate_private_key
from fastapi import HTTPException

from amalia_api.auth import Authenticator
from amalia_api.config import Settings


def settings(tmp_path, mode="oidc", secret=None):
    return Settings(
        database_path=tmp_path / "db", log_path=tmp_path / "log", auth_mode=mode,
        oidc_issuer="https://id.example/realms/amalia", oidc_audience="amalia-api",
        oidc_jwks_url="https://id.example/jwks", proxy_shared_secret=secret,
        deletion_mode="soft", max_entry_length=10000, allowed_origins=(),
    )


def test_oidc_signature_issuer_audience_and_roles_are_validated(tmp_path):
    private_key = generate_private_key(public_exponent=65537, key_size=2048)
    now = datetime.now(timezone.utc)
    token = jwt.encode({
        "sub": "uuid-1", "preferred_username": "alice", "name": "Alice Ääkkönen",
        "iss": "https://id.example/realms/amalia", "aud": "amalia-api",
        "iat": now, "exp": now + timedelta(minutes=5),
        "realm_access": {"roles": ["journal:read"]},
    }, private_key, algorithm="RS256", headers={"kid": "test"})
    auth = Authenticator(settings(tmp_path))
    auth.jwks = SimpleNamespace(get_signing_key_from_jwt=lambda _token: SimpleNamespace(key=private_key.public_key()))
    principal = auth.authenticate_headers({"authorization": f"Bearer {token}"})
    assert principal.display_name == "Alice Ääkkönen"
    assert "journal:read" in principal.roles


def test_oidc_wrong_audience_is_rejected(tmp_path):
    private_key = generate_private_key(public_exponent=65537, key_size=2048)
    now = datetime.now(timezone.utc)
    token = jwt.encode({"sub": "u", "iss": "https://id.example/realms/amalia", "aud": "wrong",
                        "iat": now, "exp": now + timedelta(minutes=5)}, private_key, algorithm="RS256")
    auth = Authenticator(settings(tmp_path))
    auth.jwks = SimpleNamespace(get_signing_key_from_jwt=lambda _token: SimpleNamespace(key=private_key.public_key()))
    with pytest.raises(HTTPException) as error:
        auth.authenticate_headers({"authorization": f"Bearer {token}"})
    assert error.value.status_code == 401


def test_proxy_mode_requires_constant_time_shared_secret(tmp_path):
    auth = Authenticator(settings(tmp_path, "proxy", "correct-secret"))
    with pytest.raises(HTTPException):
        auth.authenticate_headers({"x-amalia-proxy-secret": "wrong", "x-remote-user": "alice"})
    principal = auth.authenticate_headers({
        "x-amalia-proxy-secret": "correct-secret", "x-remote-user": "alice",
        "x-amalia-roles": "journal:read,journal:write",
    })
    assert principal.username == "alice" and "journal:write" in principal.roles
