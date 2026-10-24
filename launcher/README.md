# Robot War Engine Launcher

Launcher application for Robot War Engine.
This optional application provides a Play screen, a saved player name, mod
selection and load order, game settings, data import, and the online lobby.
The normal engine entry point still launches the game directly.

For development, use Node.js 22.15+ or 24, run `npm ci` and
`npm run build:all:prod`, then set `RWE_HOME` to the directory containing
`rwe` and `rwe_bridge` and run `npx electron dist/main.js`.

`npm run package` builds a package for the current platform. Linux AppImages
include it behind the `launcher` argument; Windows releases put it under
`launcher/`. The launcher uses the engine's user-data path and imported `Data`
directory. Each additional mod belongs in a separate directory under `mods/`;
archive-only mod folders work without a manifest.

`launcher.json` stores the profile and selected load order. It is separate from
the engine's `rwe.cfg`; changing video settings preserves unrelated native
settings. The saved profile is passed to a launched engine using
`RWE_PLAYER_NAME`, and the native menu uses it for the local player and default
LAN nickname.
