# Architecture

```text
Desktop shortcut / Start menu
        |
        v
Amalia Launcher -- HTTPS + SHA-256 + Ed25519 --> update repository
        |
        v
Amalia Desktop
  |-- JSON application catalog
  |-- QMdiArea window/workspace manager
  |-- isolated/shared Qt WebEngine profiles
  |-- user-initiated OIDC Authorization Code + PKCE / Kerberos SSO
  |-- QtKeychain credential/token storage
  |-- native task journal
  |-- local SQLite WAL offline queue (UUID idempotency key)
  |-- REST client
  `-- authenticated reconnecting WebSocket client

HTTPS 443 reverse proxy (additive locations only)
        |
        v
127.0.0.1:18080  Amalia API (Unix user: amalia)
  |-- OIDC JWT validation or trusted Kerberos proxy identity
  |-- prepared SQLite operations, WAL, BEGIN IMMEDIATE sequencing
  |-- append-only DB triggers
  |-- per-task WebSocket fan-out
  `-- server-side CSV/PDF export
        |
        v
/var/lib/amalia/amalia.db (never opened by a workstation)
```

## Isolation boundaries

Server code is installed under `/opt/amalia`, mutable state under
`/var/lib/amalia`, logs under `/var/log/amalia`, and update files under
`/srv/amalia/updates`. The dedicated unprivileged `amalia` account has write
access only to Amalia state and logs. The API port is localhost-only.

The nginx example contains only new `/amalia/api/`, `/amalia/ws/`,
`/amalia/health` and `/amalia-updates/` locations. Installation does not edit,
replace or reload existing nginx configuration.

## Extending applications

Web applications are records in `desktop/resources/apps.json`: `id`, `name`,
`type`, HTTPS URL, icon, default size, multi-instance flag, profile isolation and
an open-ended `special` object. Adding an ordinary web application does not
require changes to `MainWindow`.

## Identity

- At startup the desktop presents an explicit login dialog and performs no
  credential-store read or Kerberos-session lookup before the user chooses a
  method. The user may also continue without authentication.
- Domain workstation: after the user selects domain login, the desktop checks
  the existing Kerberos session and sends no saved AD password. A reviewed
  SPNEGO reverse-proxy location may authenticate the HTTPS request and inject a
  server-only trusted identity.
- Non-domain workstation: after the user selects browser login, the system
  browser performs OIDC Authorization Code Flow with PKCE. The access token
  remains in memory, and Amalia does not persist an OIDC refresh token.
- The recommended identity provider is Keycloak with Samba AD federation over
  the private server network. LDAP/SMB/Kerberos are never published to internet.

Authorization is enforced again by every API operation. UI visibility is not a
security boundary.
