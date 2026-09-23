#!/bin/sh
set -eu
export LC_ALL=C
export DEBIAN_FRONTEND=noninteractive

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this script through sudo or pkexec." >&2
    exit 1
fi

apt-get update
apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    debhelper \
    fakeroot \
    git \
    ninja-build \
    pkg-config \
    qt6-base-dev \
    qt6-base-dev-tools \
    qt6-webengine-dev \
    qt6-webengine-dev-tools \
    qt6-websockets-dev \
    qt6-tools-dev-tools \
    qtkeychain-qt6-dev \
    libqt6sql6-sqlite \
    libssl-dev \
    libsecret-1-0 \
    python3-dev \
    python3-venv \
    unzip \
    zip
