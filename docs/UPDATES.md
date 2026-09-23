# Update publishing, channels and rollback

Create an Ed25519 key pair in a controlled signing environment. Keep the private
key offline/outside Git and outside the Ilona server. Distribute only the public
PEM through the signed first installer.

```bash
openssl genpkey -algorithm ED25519 -out /secure/amalia-update-private.pem
openssl pkey -in /secure/amalia-update-private.pem -pubout \
  -out update-public-key.pem
```

An update ZIP contains `amalia-desktop[.exe]`, `config/apps.json`,
`config/amalia.json` and the deployed Qt runtime. Publish:

```bash
server/venv/bin/python tools/publish_update.py \
  --package build/amalia-2026.08.8-ubuntu.zip \
  --repository /srv/amalia/updates \
  --channel test --platform ubuntu --version 2026.08.8 \
  --private-key /secure/amalia-update-private.pem
```

Release versions use `YYYY.MM.N`: the first build of a month is `.1`, each
following build in the same month increments `N`, and a new month resets it to
`.1`. The CMake project version, package version, Launcher bootstrap version,
installer version and published update metadata must be identical.

Promote by publishing the identical verified artifact to `stable`. Metadata has
version, channel, platform, relative file, SHA-256, Ed25519 signature,
`minimum_supported_version` and `mandatory`.

The Ed25519 signature covers a canonical `AMALIA-UPDATE-V1` payload containing
version, channel, platform, relative file, package SHA-256, minimum version and
mandatory flag. This prevents version/channel rewriting or replaying an old
signed package under a new version. The Launcher validates HTTPS, the canonical
signature, package SHA-256 and safe ZIP
paths before extraction. Local `file://` repositories work only when the explicit
test setting is enabled. Copy and edit `launcher/launcher.local-test.json.example`
as `launcher.json` for this isolated test; never deploy that flag in production.
If the repository is offline, the current version is
started. During update, `previous.json` is retained. The desktop writes the
Launcher's one-time startup token to an acknowledgement file after its main
window is initialized. Timeout/failure restores and launches the previous
version.

Changing channel is a managed configuration change in `launcher.json`. End users
should not receive write access to the installation config or public key.

The Launcher binary, bootstrap desktop and public key remain administrator-owned.
Downloaded versions, `current.json` and `previous.json` are stored beneath Qt's
per-user `AppLocalDataLocation`; this avoids an elevated updater and keeps each
operating-system user's rollback state separate. If the repository or user store
is unavailable on first launch, the administrator-installed bootstrap executable
is started.
