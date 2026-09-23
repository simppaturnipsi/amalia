import os

os.environ.setdefault("AMALIA_AUTH_MODE", "test")
os.environ.setdefault("AMALIA_DATABASE", "/tmp/amalia-import.db")
os.environ.setdefault("AMALIA_LOG", "/tmp/amalia-import.log")

import pytest
from fastapi.testclient import TestClient

from amalia_api.config import Settings
from amalia_api.main import create_app


@pytest.fixture
def settings(tmp_path):
    return Settings(
        database_path=tmp_path / "amalia.db",
        log_path=tmp_path / "amalia.log",
        auth_mode="test",
        oidc_issuer="https://issuer.invalid/realms/amalia",
        oidc_audience="amalia-api",
        oidc_jwks_url=None,
        proxy_shared_secret=None,
        deletion_mode="soft",
        max_entry_length=10000,
        allowed_origins=(),
    )


@pytest.fixture
def app(settings):
    return create_app(settings)


@pytest.fixture
def client(app):
    with TestClient(app) as test_client:
        yield test_client


@pytest.fixture
def auth_headers():
    return {
        "X-Test-User": "alice",
        "X-Test-User-Id": "user-alice",
        "X-Test-Display-Name": "Alice Aakkonen",
        "X-Test-Roles": "journal:read,journal:write,journal:export,journal:delete",
    }


@pytest.fixture
def task(client, auth_headers):
    response = client.post(
        "/api/v1/tasks", headers=auth_headers,
        json={"name": "Kadonneen etsintä", "task_number": "2026-001"},
    )
    assert response.status_code == 201
    return response.json()
