import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_release_versions_use_year_month_counter_and_match():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    cmake_version = re.search(r"project\(Amalia VERSION ([^ ]+)", cmake).group(1)
    debian_version = re.search(
        r"^amalia \(([^)]+)\)",
        (ROOT / "packaging/debian/changelog").read_text(encoding="utf-8"),
    ).group(1)
    versions = {
        cmake_version,
        debian_version,
        json.loads((ROOT / "launcher/launcher.json").read_text(encoding="utf-8"))["bootstrapVersion"],
        json.loads((ROOT / "installer/config/launcher-windows.json").read_text(encoding="utf-8"))["bootstrapVersion"],
        ET.parse(ROOT / "installer/config/config.xml").getroot().findtext("Version"),
        ET.parse(ROOT / "installer/packages/fi.vapepa.amalia/meta/package.xml").getroot().findtext("Version"),
        json.loads((ROOT / "updates/metadata/stable-ubuntu.json.example").read_text(encoding="utf-8"))["version"],
    }
    assert len(versions) == 1
    assert re.fullmatch(r"20\d{2}\.(?:0[1-9]|1[0-2])\.[1-9]\d*", versions.pop())


def test_web_modules_are_configured_not_hardcoded_and_use_https():
    catalog = json.loads((ROOT / "desktop/resources/apps.json").read_text(encoding="utf-8"))
    modules = {item["id"]: item for item in catalog["applications"]}
    assert {"ohto", "karttahimmeli", "ilona-admin", "hbmonitor", "journal"} <= modules.keys()
    for module in modules.values():
        if module["type"] == "web":
            assert module["url"].startswith("https://")
    assert modules["ilona-admin"]["url"] == "https://ilona.ad.vapepa.info:10443/"
    assert modules["ilona-admin"]["special"]["allowCertificateErrors"] is False
    assert modules["hbmonitor"]["special"]["allowCertificateErrors"] is False


def test_tls_errors_are_aborted_and_never_ignored():
    sources = "\n".join(path.read_text(encoding="utf-8") for path in [
        ROOT / "desktop/src/ApiClient.cpp", ROOT / "desktop/src/AuthManager.cpp",
        ROOT / "desktop/src/JournalSocket.cpp", ROOT / "launcher/src/UpdateEngine.cpp",
    ])
    assert "ignoreSslErrors" not in sources
    assert "sslErrors" in sources and "abort()" in sources


def test_launcher_production_configuration_is_https_and_local_repo_off():
    config = json.loads((ROOT / "launcher/launcher.json").read_text(encoding="utf-8"))
    assert config["repositoryBaseUrl"].startswith("https://")
    assert config["channel"] in {"stable", "test"}
    assert config["allowLocalTestRepository"] is False
    assert config["installRoot"] == "@USER_DATA@"
    assert config["bootstrapExecutable"] == "/usr/bin/amalia-desktop"


def test_release_build_requires_keychain_and_packages_update_key():
    cmake = (ROOT / "desktop/CMakeLists.txt").read_text(encoding="utf-8")
    install = (ROOT / "packaging/debian/amalia.install").read_text(encoding="utf-8")
    assert "find_package(Qt6Keychain REQUIRED)" in cmake
    assert "update-public-key.pem" in install
    assert not (ROOT / "packaging/debian/postinst").exists()


def test_systemd_api_is_localhost_only_and_dedicated_user():
    unit = (ROOT / "deploy/systemd/amalia-api.service").read_text(encoding="utf-8")
    assert "User=amalia" in unit and "Group=amalia" in unit
    assert "--host 127.0.0.1 --port 18080 --workers 1" in unit
    assert "ReadWritePaths=/var/lib/amalia /var/log/amalia" in unit


def test_public_api_path_is_not_duplicated_by_nginx_proxy_pass():
    runtime = json.loads((ROOT / "desktop/resources/amalia.json").read_text(encoding="utf-8"))
    config = (ROOT / "deploy/nginx/amalia-locations.conf.example").read_text(encoding="utf-8")
    assert runtime["apiBaseUrl"].endswith("/amalia/api/v1/")
    assert "location /amalia/api/" in config
    assert "proxy_pass http://127.0.0.1:18080/api/;" in config


def test_browser_oidc_uses_the_public_identity_host():
    runtime = json.loads((ROOT / "desktop/resources/amalia.json").read_text(encoding="utf-8"))
    oidc = runtime["oidc"]
    assert oidc["issuer"] == "https://ilona.vapepa.info/realms/amalia"
    assert oidc["authorizationEndpoint"].startswith("https://ilona.vapepa.info/realms/amalia/")
    assert oidc["tokenEndpoint"].startswith("https://ilona.vapepa.info/realms/amalia/")


def test_journal_retries_loading_after_authentication_completes():
    api_header = (ROOT / "desktop/src/ApiClient.h").read_text(encoding="utf-8")
    api_source = (ROOT / "desktop/src/ApiClient.cpp").read_text(encoding="utf-8")
    journal = (ROOT / "desktop/src/JournalWidget.cpp").read_text(encoding="utf-8")
    assert "void authenticationReady();" in api_header
    assert "emit authenticationReady();" in api_source
    assert "&ApiClient::authenticationReady" in journal


def test_startup_authentication_requires_an_explicit_user_choice():
    main_window = (ROOT / "desktop/src/MainWindow.cpp").read_text(encoding="utf-8")
    auth_source = (ROOT / "desktop/src/AuthManager.cpp").read_text(encoding="utf-8")
    auth_header = (ROOT / "desktop/src/AuthManager.h").read_text(encoding="utf-8")

    assert "QTimer::singleShot(0, this, &MainWindow::showLoginDialog);" in main_window
    assert 'tr("Jatka ilman kirjautumista")' in main_window
    assert "startDomainSso" in main_window and "startInteractiveOidc" in main_window
    assert "readSecret" not in auth_source
    assert "refreshWithToken" not in auth_source
    assert "CredentialStore" not in auth_header
    assert "CredentialStore" not in auth_source
