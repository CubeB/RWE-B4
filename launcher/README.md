# Robot War Engine Launcher

Launcher application for Robot War Engine.
This optional application provides a Play screen, a saved player name, mod
selection and load order, game settings, data import, and the online lobby.
The normal engine entry point still launches the game directly.

For development, use Node.js 22.22.2+ or 24.15+, run `npm ci` and
`npm run build:all:prod`, then set `RWE_HOME` to the directory containing
`rwe`, `rwe_setup` and `rwe_bridge` and run `node dist/main.js`.
The launcher opens in the installed browser. Its local backend keeps file
access, game processes and lobby connections outside the browser.

`npm run package` builds a package for the current platform. Linux AppImages
include it behind the `launcher` argument; Windows releases put it under
`launcher/`. The launcher uses the engine's user-data path and imported `Data`
directory. Each additional mod belongs in a separate directory under `mods/`;
archive-only mod folders work without a manifest.

Packages include a Node single executable and the frontend assets; players
need neither Electron nor an installed Node. The backend listens only on
loopback with a fresh private session URL. Closing its browser tabs stops it
after a one-minute grace period, unless a game is still running. To suppress
opening the browser when testing, set `RWE_LAUNCHER_NO_OPEN=1`; the backend
prints its URL. See [download measurements and verification](../docs/DOWNLOAD-SIZE.md).
`node scripts/browser-smoke.cjs <printed-url>` checks the production React
bundle in jsdom against a running backend; it does not replace a real browser
or platform folder-picker check.

`launcher.json` stores the profile and selected load order. It is separate from
the engine's `rwe.cfg`; changing video settings preserves unrelated native
settings. The saved profile is passed to a launched engine using
`RWE_PLAYER_NAME`, and the native menu uses it for the local player and default
LAN nickname.

The Play screen's **Check for updates** queries this repository's GitHub
releases. The build's own version is injected at build time from
`git describe`; when the newest release is newer, the launcher offers the
platform's asset (the Windows installer or the Linux AppImage) through the
system browser. It does not replace files in place: there is no signed update
feed, and the release is a per-platform installer/AppImage that the user runs.
