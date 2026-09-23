#!/bin/sh
set -eu

PURGE_DATA=false
if [ "${1:-}" = "--purge-data" ]; then PURGE_DATA=true; fi

systemctl disable --now amalia-api.service 2>/dev/null || true
if [ -e /etc/systemd/system/amalia-api.service ]; then unlink /etc/systemd/system/amalia-api.service; fi
systemctl daemon-reload

# Remove only Amalia-owned application files. Existing proxy configs and other
# services are never touched. The administrator removes their manual include.
if [ -d /opt/amalia/server ]; then
    find /opt/amalia/server -depth -type f -exec unlink {} \;
    find /opt/amalia/server -depth -type l -exec unlink {} \;
    find /opt/amalia/server -depth -type d -exec rmdir {} \; 2>/dev/null || true
fi
if $PURGE_DATA; then
    echo "Purging /var/lib/amalia, /var/log/amalia and /srv/amalia/updates was requested."
    find /var/lib/amalia /var/log/amalia /srv/amalia/updates -depth -type f -exec unlink {} \; 2>/dev/null || true
    find /var/lib/amalia /var/log/amalia /srv/amalia/updates -depth -type d -exec rmdir {} \; 2>/dev/null || true
fi
