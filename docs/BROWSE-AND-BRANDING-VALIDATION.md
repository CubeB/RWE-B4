# Browse repair and B4 icon validation

2026-10-21. Sources: INSTALL-UX-001/002 in
[INSTALLER-UX-FINDINGS.md](INSTALLER-UX-FINDINGS.md). The original comparison
is static reference scope analysis; no original game or historical installer
was run. No new gameplay or data-copy behavior is introduced.

## Browse

The old handler calls Test-Path with the empty text box before opening the
picker. With the assistant's Stop error preference, Windows PowerShell 5.1
raises `Cannot bind argument to parameter LiteralPath because it is an empty
string`. This was reproduced. Blank/whitespace now bypasses that check.
Folder-picker lifetime is protected by finally; errors are shown in the
assistant with a typed-path fallback instead of escaping the WinForms event.
The importer arguments, file selection/copy rules and destinations are unchanged.

The real-importer setup harness passes with the new Browse regression cases:
blank, whitespace and missing initial paths still reach the picker; Cancel
preserves the source; selection is applied; a simulated OS picker exception
is handled and the picker is disposed. Only the OS dialog is substituted;
the actual Browse handler/path checks run. The existing failed-import,
retry, spaced/trailing paths, disk-copy contents and Play-now checks also pass.
An interactive OS folder-picker session was not automated locally.

## Brand assets and built artifacts

The copied source PNG's SHA-256 matches Desktop/b4logo.png exactly. Its
256-pixel PNG was visually inspected. The seven-size ICO retains the full
image; the large source image is not installed in runtime packages.

Both complete local Release rwe and rwe_setup targets built successfully.
All seven embedded RT_ICON PNG payloads in each executable match the supplied
ICO's frames byte for byte. A hidden SDL window, using the same resource,
was created and its class icon handle matched the embedded B4 resource.
This executes SDL's icon selection without launching or observing a match.

The launcher passed TypeScript checks, production bundling/packaging and
lint (zero errors; two pre-existing warnings). All seven icon payloads in the
packaged Electron executable match the ICO. The PNG exists in app.asar and
its webpack-copied bytes match the canonical asset. BrowserWindow references
that packaged PNG. A live lobby session was not opened.

CMake/CPack/NSIS compiled an explicitly named branding-smoke installer using
stub game targets. Its installer resource group 103 contains all seven B4
frames, and generated setup/uninstall icon definitions and import-shortcut
icon paths were inspected. It is a resource/packaging check, not a playable
release, and was not installed into the user's system.

The AppImage workflow now copies the canonical PNG instead of generating a
placeholder. A fresh AppImage has not been built locally; CI must validate
that package. No signed release was produced: see
[WINDOWS-SIGNING.md](WINDOWS-SIGNING.md).

Evidence is under D:/tmp/rwe-branding-20261021, including package/build logs,
icon smoke checks and the named smoke installer. The Windows setup harness
runs in the existing MSVC CI job. The published v1.1.0-pre3 assets were not
replaced; these source changes need a subsequent installer build/release.
