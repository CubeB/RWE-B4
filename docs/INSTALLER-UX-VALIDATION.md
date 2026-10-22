# Installer UX validation (2026-10-21)

Implements [INSTALL-UX-001](INSTALLER-UX-FINDINGS.md). Distribution and import
presentation are RWE-original. The reference comparison is static only; no
original game or historical external installer was executed.

## User flow

Windows NSIS welcomes the player with the two steps: install RWE, import
files from their own Total Annihilation copy. The checked finish-page option
opens a WinForms assistant. A blank source asks the existing importer to
search automatically; Browse selects an explicit folder. The assistant drains
both output pipes asynchronously, reports the real exit code, permits retry
on error and offers Play now only after successful completion. Details stay
out of the main flow until requested or needed for an error. Copying is still
performed by rwe_setup with no new file rules or destination override.

The Start menu has Play Robot War Engine, Multiplayer Lobby and Import Game
Data. The desktop shortcut launches the game. The portable ZIP contains
Import Game Data.cmd, which opens the same assistant with Windows PowerShell
and the built-in .NET WinForms assemblies; it needs no separate Python install.
The game-data-missing error now points to this assistant before CLI commands.

The Windows install directory and uninstall registry key become stable Robot
War Engine names, rather than versioned names. Existing installations made
with the old versioned registry keys are not automatically migrated or
removed by this change. Data and saves under the existing user-data directory
are not part of the installer file-removal list. Elevation behavior remains
CPack's existing NSIS behavior; no new all-user/current-user policy is claimed.

AppImage setup with no additional arguments uses a Zenity folder picker,
confirmation, progress and a success/error dialog on graphical desktops.
Play is an explicit choice after import. Headless setup, missing Zenity and
explicit CLI arguments retain the original CLI route. The desktop's Zenity
installation is used; no bundle library search path is introduced. This does
not add an AppImage first-run installer or desktop integration service.

## Executed checks

- Windows PowerShell 5.1 parsed and ran the assistant. An offscreen render was
  visually inspected for the initial form's text, controls and spacing.
- `tools/test-installer-setup.ps1` passed using the real rwe_setup binary:
  failing source, actual failure status, visible details, enabled retry,
  successful retry, package/source paths with spaces, trailing separator,
  copied file contents, and Play now gating. APPDATA was isolated in a unique
  temporary directory; the user's RWE data was not changed.
- The same assistant imported the installed TA data into a separate probe
  directory: 30 archives, five films and 18 music tracks, approximately 1 GB.
  SHA-256 of both base archives matched the source. A second import reported
  53 already present and zero bytes copied. This executed the RWE importer
  with shipped data; it did not run the original game.
- Six AppImage routing tests passed with real shell processes and fake
  importer/Zenity processes: quoted folder with spaces and ampersand,
  cancellation without import, failure details/status, explicit Play,
  CLI arguments and headless fallback. A live Linux desktop/Zenity session
  was not available, so actual GTK rendering remains unverified locally.
- CMake/CPack and NSIS 3.12 generated and compiled an installer smoke package.
  Its generated project.nsi was inspected: stable directory/key, finish-page
  callback, quoted PowerShell script paths, correct game/lobby/import targets,
  desktop creation/removal and start-menu cleanup. The smoke package used
  real engine/importer executables and an explicitly substituted launcher
  file; it is not a release and was not installed into the user's system.
  Full Electron packaging and installed-shortcut execution remain CI/manual
  package checks, not locally executed results.
- The full active-tree Release rwe target rebuilt and linked successfully
  with the updated first-run help. Normal game behavior was not changed.

The Windows assistant test is wired into the MSVC job. AppImage routing tests
run before AppImage creation. No production simulation tests were added for
these presentation-only changes.

Local evidence: D:/tmp/rwe-installer-20261021 (setup render, shipped-import
log, isolated copied data, generated NSIS script/log, smoke executable and
Release build log). The smoke executable is named Installer-Smoke-Not-A-Release.

Re-run Windows checks with:

```powershell
powershell -NoProfile -STA -ExecutionPolicy Bypass -File tools/test-installer-setup.ps1 -Importer build-release/rwe_setup.exe
```

Re-run the shell routing checks with `python3 tools/test_installer_appimage.py`.
On Windows/MSYS2, set RWE_TEST_BASH to the MSYS2 bash executable first.
