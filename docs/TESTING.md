# Test plan

Automated server/protocol tests run with `cd server && venv/bin/pytest -q` and
cover two and ten concurrent writers, contiguous server sequences, UUID
idempotency, simulated send interruption, append-only API/DB enforcement,
export/deletion gating, WebSocket delivery and reconnect+REST catch-up, OIDC and
proxy validation, update outage/corruption/signature/rollback and static
installation isolation.

## Repeatable desktop tests requiring Qt/OS integration

Run on both Windows 11 and Ubuntu Desktop after building a signed test package:

1. **Credential user isolation:** create OS users A and B. As A save unique Ohto
   test credentials, log out, log in as B and verify Amalia cannot read/show A's
   username and the B keychain has a distinct `fi.vapepa.amalia` entry. Repeat in
   the reverse direction. Inspect no `.json`, `.ini`, SQLite or QSettings file
   contains either password.
2. **Workspace:** open all modules, move/resize/minimize/maximize across available
   displays, close Amalia via Launcher, restart and verify open modules and valid
   geometries restore. Disconnect a display and verify the main window is moved
   to the primary display.
3. **Offline:** block only `/amalia/api/`, add rows, restart the desktop and verify
   the pending count persists. Restore access and verify each UUID appears once
   with server-assigned contiguous sequences.
4. **WebSocket:** drop `/amalia/ws/`, add a row from workstation B, restore the
   route and verify workstation A reconnects and catches up using REST.
5. **SSO/OIDC:** verify startup first shows Amalia's login dialog and does not
   read secure storage, inspect Kerberos or open a browser before the user makes
   a choice. On a domain workstation select Kerberos and verify the existing
   ticket is used without an AD password prompt. On a non-domain workstation
   select browser login and verify OIDC+PKCE. Select continue without login and
   verify normal offline usability.
6. **TLS:** present an untrusted certificate in a test environment and verify both
   WebEngine/API and Launcher reject it without an ignore option.

## Existing service regression gate

Before installation:

```bash
python3 tools/regression_snapshot.py \
  --url https://HOST/ILONA-ADMIN-HEALTH \
  --url https://HOST/HBMONITOR-HEALTH \
  --write /tmp/before-amalia.json
```

After installing only Amalia and adding the reviewed locations, rerun with
`--compare /tmp/before-amalia.json`. Any difference blocks deployment. Also
compare unit states, listening sockets and database file hashes/backups. This
target-specific gate is intentionally not executed by the source test suite,
because the source build must not contact or mutate production services.
