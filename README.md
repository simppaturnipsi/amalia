# Amalia

Amalia is a new, isolated Qt 6 desktop workspace and server-side task journal.
It does not modify Ilona Admin, HBMonitor, their databases, ports, authentication,
systemd units or URL paths. All server artifacts use the dedicated `amalia`
name, Unix account and directories.

## Components

- `desktop/`: Qt 6 Widgets/WebEngine workspace, OIDC+PKCE, Kerberos-session
  detection, QtKeychain, WebSocket journal and durable offline queue.
- `launcher/`: signed Stable/Test updater with offline launch and rollback.
- `server/`: localhost-only FastAPI service, SQLite WAL, migrations, REST,
  WebSocket, immutable journal rows and server-side CSV/PDF export.
- `deploy/`: additive systemd/nginx examples and idempotent server scripts.
- `packaging/`: Ubuntu `.deb` metadata.
- `installer/`: Qt Installer Framework configuration for `AmaliaSetup.exe`.
- `updates/` and `tools/`: update repository layout, Ed25519 publisher and tests.
- `docs/`: architecture, build, API, installation, updates and operations.

The current implementation version is `2026.08.8`. Release versions use
`YYYY.MM.N`, where `N` starts at 1 each month and increases for every build.
See [Architecture](docs/ARCHITECTURE.md),
[Build](docs/BUILD.md), [Server installation](docs/SERVER.md), [API](docs/API.md),
[Updates](docs/UPDATES.md) and [Operations](docs/OPERATIONS.md).

## Server tests

```bash
cd server
python3 -m venv venv
venv/bin/pip install -r requirements-dev.txt
venv/bin/pytest -q
```

The API binds only to `127.0.0.1:18080` in the provided systemd unit. External
access is added as reviewed HTTPS reverse-proxy locations; the example is never
installed into an existing nginx server block automatically.
