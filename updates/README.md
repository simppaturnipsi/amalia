# Amalia update repository

`stable/` and `test/` contain signed platform packages. `metadata/` contains
`<channel>-<platform>.json`. Publish only with `tools/publish_update.py` and keep
the Ed25519 private key outside this repository and outside the Ilona server.

The server stores only signed packages, metadata and the public verification key.
