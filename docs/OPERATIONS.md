# Operations, backup and troubleshooting

## Logs

The API writes only to `/var/log/amalia/amalia-api.log` with rotation. It records
startup/shutdown, authentication/WebSocket/database errors, exports and whole
task deletions. It never logs request bodies, Authorization headers, access or
refresh tokens, passwords or external-service credentials. systemd stdout/stderr
is additionally available through `journalctl -u amalia-api`.

## Backup

Use SQLite online backup while the API is running:

```bash
sudo deploy/install/backup-amalia.sh
```

Store the resulting database and `.sha256` outside the host. Test restoration on
an isolated machine: stop `amalia-api`, restore to a new file owned by
`amalia:amalia` mode 0640, run `PRAGMA integrity_check`, then start the service.
Never place the live database on SMB/NFS; it stays on local server storage.

## Troubleshooting

- API: `curl http://127.0.0.1:18080/health`, then `journalctl -u amalia-api`.
- Proxy: `nginx -t`; verify only the new `/amalia/*` locations.
- OIDC: verify issuer/audience/JWKS and clock synchronization. Do not log tokens.
- Kerberos SSO: verify a workstation ticket with `klist` and the existing SPNEGO
  proxy independently. Do not add an AD password prompt to Amalia.
- WebSocket: verify HTTP/1.1 Upgrade headers and Authorization forwarding.
- Offline queue: stored per OS user in Qt's AppDataLocation as SQLite WAL. Rows
  remain pending after failure and reuse their original UUID.
- Credentials: verify GNOME Keyring/KWallet or Windows Credential Manager and
  QtKeychain. Amalia deliberately refuses an insecure file fallback.
- WebEngine: a TLS error is not bypassed. Correct the server certificate/CA.

## Deletion policy

`AMALIA_DELETION_MODE=soft` is the default and preserves the task/rows with a
deleted marker. `hard` deletes the whole exported task in one dedicated flow;
the audit row in `task_deletions` remains. Individual LogEntry removal is never
allowed in either mode.
