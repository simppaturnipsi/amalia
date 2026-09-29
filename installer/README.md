# Qt Installer Framework staging

Build the Windows release, run `windeployqt` for the desktop with explicit
WebEngine/SQL modules and for the launcher. Ensure the `qsqlite.dll` SQL driver is present
under `bin/sqldrivers/`. Include the QtWebEngine process, resources and
locales, plus staged `bin`, `resources`, `translations`, `share` and QtKeychain
DLLs under `packages/fi.vapepa.amalia/data/`. Copy `config/launcher-windows.json` as
`share/amalia/config/launcher.json` and provide the public verification key.
The installed desktop beside the Launcher is the administrator-owned bootstrap;
downloaded versions and rollback state use the operating-system user's
AppLocalDataLocation. Then run:

```powershell
binarycreator.exe -c config/config.xml -p packages AmaliaSetup.exe
```

Sign `AmaliaSetup.exe`, `amalia-launcher.exe` and `amalia-desktop.exe` with the
organization's code-signing certificate after the build. Shortcuts deliberately
target only the Launcher.
