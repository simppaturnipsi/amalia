from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_server_installer_only_targets_amalia_paths_and_unit():
    script = (ROOT / "deploy/install/install-server.sh").read_text(encoding="utf-8")
    forbidden = ["ilona-admin.service", "hbmonitor.service", "/var/lib/ilona-admin", "/opt/ilona-admin"]
    assert all(item not in script for item in forbidden)
    assert "/opt/amalia" in script and "amalia-api.service" in script


def test_nginx_file_is_additive_and_has_no_server_block():
    config = (ROOT / "deploy/nginx/amalia-locations.conf.example").read_text(encoding="utf-8")
    assert "server {" not in config
    assert "location /amalia/api/" in config
    assert "location /amalia/ws/" in config
    assert "location /amalia-updates/" in config


def test_credentials_have_no_settings_or_file_fallback():
    source = (ROOT / "desktop/src/CredentialStore.cpp").read_text(encoding="utf-8")
    assert "QSettings" not in source
    assert "QFile" not in source
    assert "QKeychain" in source
