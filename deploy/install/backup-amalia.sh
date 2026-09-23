#!/bin/sh
set -eu

DATABASE=${AMALIA_DATABASE:-/var/lib/amalia/amalia.db}
BACKUP_DIR=${AMALIA_BACKUP_DIR:-/var/backups/amalia}
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
install -d -o root -g amalia -m 0750 "$BACKUP_DIR"
sqlite3 "$DATABASE" ".timeout 30000" ".backup '$BACKUP_DIR/amalia-$STAMP.db'"
sha256sum "$BACKUP_DIR/amalia-$STAMP.db" > "$BACKUP_DIR/amalia-$STAMP.db.sha256"
echo "$BACKUP_DIR/amalia-$STAMP.db"
