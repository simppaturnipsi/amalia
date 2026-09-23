#!/bin/sh
set -eu

SOURCE_DIR=${AMALIA_SOURCE_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}

if ! getent group amalia >/dev/null; then
    groupadd --system amalia
fi
if ! getent passwd amalia >/dev/null; then
    useradd --system --gid amalia --home-dir /var/lib/amalia --shell /usr/sbin/nologin amalia
fi

install -d -o root -g root -m 0755 /opt/amalia /opt/amalia/server
install -d -o amalia -g amalia -m 0750 /var/lib/amalia /var/log/amalia
install -d -o root -g root -m 0755 /srv/amalia/updates /etc/amalia

cp -a "$SOURCE_DIR/server/amalia_api" /opt/amalia/server/
cp -a "$SOURCE_DIR/server/migrations" /opt/amalia/server/
install -m 0644 "$SOURCE_DIR/server/requirements.txt" /opt/amalia/server/requirements.txt

if [ ! -x /opt/amalia/server/venv/bin/python ]; then
    python3 -m venv /opt/amalia/server/venv
fi
/opt/amalia/server/venv/bin/pip install --disable-pip-version-check --requirement /opt/amalia/server/requirements.txt

if [ ! -e /etc/amalia/amalia-api.env ]; then
    install -o root -g amalia -m 0640 "$SOURCE_DIR/deploy/systemd/amalia-api.env.example" /etc/amalia/amalia-api.env
fi
install -o root -g root -m 0644 "$SOURCE_DIR/deploy/nginx/amalia-locations.conf.example" /etc/amalia/nginx-amalia-locations.conf.example
install -o root -g root -m 0644 "$SOURCE_DIR/deploy/systemd/amalia-api.service" /etc/systemd/system/amalia-api.service

chown -R root:root /opt/amalia/server
systemctl daemon-reload
systemctl enable --now amalia-api.service

echo "Amalia API installed. Review /etc/amalia/amalia-api.env and add the nginx locations manually after nginx -t."
