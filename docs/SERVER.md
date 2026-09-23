# Server installation

These steps add only Amalia-owned resources.

```bash
sudo apt update
sudo apt install -y python3 python3-venv sqlite3
cd /path/to/amalia
sudo AMALIA_SOURCE_DIR="$PWD" deploy/install/install-server.sh
```

The script is idempotent: it creates the `amalia` account and Amalia directories,
installs the API virtual environment and the new `amalia-api.service`. It does
not edit nginx or any existing unit. Review `/etc/amalia/amalia-api.env`, set the
real OIDC issuer/audience, restart only Amalia, and verify:

```bash
sudo systemctl restart amalia-api
systemctl status amalia-api
curl http://127.0.0.1:18080/health
```

Copy only the reviewed locations from
`/etc/amalia/nginx-amalia-locations.conf.example` into the existing HTTPS server
block or include a new Amalia-only snippet. Before reload:

```bash
sudo nginx -t
sudo systemctl reload nginx
```

Capture existing services before and after with `tools/regression_snapshot.py`.
Use the real authenticated/health URLs for Ilona Admin and HBMonitor. The script
compares status, content type, length and body SHA-256 and returns non-zero on a
difference.

## Authentication modes

- `oidc` (default): verifies JWT signature through the issuer JWKS, issuer,
  audience, expiry and required claims.
- `proxy`: only for a localhost reverse proxy that has already completed
  Kerberos/SPNEGO. Set a long random `AMALIA_PROXY_SHARED_SECRET`; strip all
  client identity headers and inject identity/secret internally.
- `test`: test suite only. Never use in a deployed environment.

## Keycloak/AD

Deploy Keycloak separately with HTTPS and federate it to Samba AD over the
private network. Configure a public desktop OIDC client with loopback redirect
URIs (`http://127.0.0.1:*`), Authorization Code Flow and PKCE S256; disable the
implicit and resource-owner-password flows. Map Amalia roles into signed token
claims. Do not expose AD LDAP, SMB or Kerberos ports to internet.

## Uninstall

```bash
sudo deploy/install/uninstall-server.sh
# Deliberate irreversible data purge only:
sudo deploy/install/uninstall-server.sh --purge-data
```

Remove only the manually added Amalia nginx include/location and run `nginx -t`.
No Ilona Admin/HBMonitor file, database, account, port or unit is touched.
