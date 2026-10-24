# Optional RWE launcher

The existing Electron multiplayer launcher now opens on a Play screen with a
saved player name, local mod selection and load order, game settings and game
data import. The online lobby remains on the Multiplayer tab. Starting `rwe`
continues to launch the native game directly.

The profile lives in `launcher.json` in the engine's user-data directory. The
launcher passes the saved name to the child engine with `RWE_PLAYER_NAME`; the
native menu applies it to the human skirmish seat and the default LAN nickname.
Host/join forms also prefill that name. Mod folders containing archives can be
used without a manifest, and imported `Data` appears as the base `ta` entry.
The base entry is placed after selected overlays so mods override base files.

Linux AppImages include the launcher at the explicit `launcher` route, and the
shell installer adds `rwe-launcher`. Windows installers and portable packages
provide a separate launcher shortcut/command. The launcher uses its own import
folder picker and invokes the native importer without a shell.

Validation:

- TypeScript check and ESLint: no errors (one existing unused-variable warning
  in archive fingerprinting).
- All 86 launcher tests pass, including profile persistence, Unicode name
  limits, base/mod discovery, load-order arguments, preserving native config,
  offline Play availability and preventing a duplicate launch.
- All three webpack bundles build and the Windows Electron package builds.
- Windows Release engine builds with native profile-name support.
- The native JSON bridge builds and its three process-level tests pass:
  clean EOF, multiple requests and controlled invalid-JSON failure. Its old
  JSON stream loop threw at EOF; the line-based protocol now exits cleanly.
- Eight AppImage routing tests, the terminal installer fixture suite and the
  PowerShell installer harness pass, including explicit optional-launcher
  routing. Normal game launch still bypasses the launcher.

The local Windows package is staged under `build/Release/launcher`, beside the
engine and bridge, with `build/Release/RWE Launcher.cmd` as its entry point.
Automatic approval review blocked starting the desktop preview without a
specific reason, so the running window was not visually inspected. Linux
Electron runtime behavior requires validation on a Linux desktop; the tests
here exercise its entry-point routing with fixtures.
