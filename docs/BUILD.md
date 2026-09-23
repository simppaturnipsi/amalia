# Build instructions

## Ubuntu Desktop

The recommended idempotent installation installs the complete tested Ubuntu
toolchain (Qt 6.5+, WebEngine, WebSockets, SQLite driver, OpenSSL, QtKeychain,
CMake/Ninja, Git, Python venv tools and Debian packaging tools):

```bash
pkexec tools/install-ubuntu-build-deps.sh
```

The equivalent manual package installation is:

```bash
sudo apt update
sudo apt install -y --no-install-recommends build-essential cmake debhelper fakeroot git \
  ninja-build pkg-config qt6-base-dev qt6-base-dev-tools qt6-webengine-dev \
  qt6-webengine-dev-tools qt6-websockets-dev qt6-tools-dev-tools \
  qtkeychain-qt6-dev libqt6sql6-sqlite libssl-dev libsecret-1-0 \
  python3-dev python3-venv unzip zip
```

Configure with the organization's Ed25519 public update key:

```bash
cmake --preset ubuntu-release \
  -DAMALIA_UPDATE_PUBLIC_KEY=/secure/build-input/update-public-key.pem
cmake --build --preset ubuntu-release
ctest --test-dir build/ubuntu-release --output-on-failure
```

Build a Debian package by copying or linking `packaging/debian` to `debian` in a
release source tree, then running:

```bash
AMALIA_UPDATE_PUBLIC_KEY=/secure/build-input/update-public-key.pem \
  dpkg-buildpackage -us -uc -b
```

The package build fails closed when that public key is missing. Never put the
private key in source or on the Ilona server.

## Release version

Amalia uses `YYYY.MM.N` release versions. `N` is a monthly build counter: start
at 1 in a new month and increment it for every subsequent build in that month.
For example, the first and second builds of August 2026 are `2026.08.1` and
`2026.08.2`; the first September build is `2026.09.1`.

Before a release build, keep the version in the root `CMakeLists.txt`, Debian
changelog, Launcher bootstrap configurations, Qt Installer Framework metadata
and update metadata identical. The configuration tests reject the old semantic
version format and mismatched release versions.

## Windows 10/11

Install Visual Studio Build Tools or MinGW, CMake, Ninja, Qt 6 with WebEngine and
WebSockets, OpenSSL, QtKeychain and Qt Installer Framework. In a Qt environment:

```powershell
cmake --preset windows-release `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 `
  -DOPENSSL_ROOT_DIR=C:\OpenSSL-Win64 `
  -DAMALIA_UPDATE_PUBLIC_KEY=C:\secure\update-public-key.pem
cmake --build --preset windows-release
cmake --install build\windows-release
windeployqt --webengine stage\windows\bin\amalia-desktop.exe
windeployqt stage\windows\bin\amalia-launcher.exe
```

Copy QtKeychain and its backend dependencies, then follow `installer/README.md`
to produce `AmaliaSetup.exe`. Sign the installer and both executables with the
organization's Windows code-signing certificate.

## Qt checks

The build requires Qt 6.5 or newer and QtKeychain. Configuration fails if the
QtKeychain Qt 6 target is unavailable; there is no QSettings/JSON credential
fallback. Debian release packaging also fails if the public update key was not
installed by CMake.
