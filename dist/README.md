# Local build artifacts

This directory is reserved for locally built Amalia packages.

Packages whose filename contains `devkey` trust the temporary development
update key generated under `build/dev-keys/`. They are suitable only for local
testing. Production packages must be rebuilt with the protected production
public key configured through `AMALIA_UPDATE_PUBLIC_KEY`.
