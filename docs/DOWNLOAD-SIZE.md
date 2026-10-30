# Download size and launcher runtime

## DOWNLOAD-001: distribution-only reference boundary

2026-10-30. This change follows INSTALL-UX-001 and INSTALL-UX-002 in
`INSTALLER-UX-FINDINGS.md`. Their dedicated spec-room comparison used
byte-tactics revision `e89721c46c3fa06e59558419b54fe26061fafb73`, matching
GOG v3.1 SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
The static evidence establishes consumption of an existing installation,
including archive registration at `0x41D4C0`, rather than a contract for
RWE's separate distribution, process launcher or import presentation.

Replacing Electron with an installed-browser frontend and a bundled Node
backend is RWE-original distribution/presentation work. It does not implement
or deliberately depart from a decoded original behaviour. No new reference
inspection or original-game execution is claimed. The external historical
installer remains outside the reference's scope; its size and presentation
are unknown. Engine, import, archive precedence and simulation contracts are
outside this change. Existing launcher logic is retained in the backend.

## Contract

Keep the Play screen, player profile, Community Patch switch, mods, settings,
importer, update checks and online lobby. Open the local UI in the installed
browser; ship no Chromium/Electron runtime and require no separately installed
Node. Bind the backend to IPv4 loopback on an ephemeral port, with a random
session token and strict host/origin checks. Do not expose arbitrary file
serving, arbitrary shell commands or remote browser access. Minimap delivery
must only serve paths supplied by the backend's current map cache. Native
folder selection and opening the known mods directory remain backend actions.
Close an unused backend after a grace period, retaining it while a game runs.

Production bundles use minification and omit source maps and development
dependencies. Ship Node's licence. Use maximum ZIP compression and solid LZMA
for NSIS; preserve existing downloadable filenames and entry points.

## Baseline

GitHub nightly at `05f7973b`, inspected 2026-10-30:

| Download | Bytes |
| --- | ---: |
| Windows Release installer | 127946566 |
| Windows Release ZIP | 176625829 |
| Linux AppImage | 129559032 |

Both platforms currently package Electron. The latest merges add the quiet
startup update check (#941), simplified launcher/Community Patch switch
(#940), and Community Patch simulation changes (#939). Only distribution and
launcher hosting are in scope here.

The downloaded nightly ZIP's central directory attributes 163197911 compressed
bytes (410768482 installed bytes) to `launcher/`: about 92% of the download.
It also contains dependency development files: `lib/` is 31402945 installed
bytes / 6748466 compressed bytes, and `include/` is 515324 / 141421 bytes.
The two large development archives are `rapidcheck.lib` (27898250 bytes) and
`lzma.lib` (3495208 bytes). Bundled RapidCheck/xz subdirectories must build their linked targets without
installing their development archives, headers, CMake or pkg-config metadata
into the game package. Their explicitly installed copyright notices remain.

## DOWNLOAD-002: further compression, 2026-10-30

The user requests another size reduction. This remains the RWE-original
distribution scope and documented static reference boundary of DOWNLOAD-001;
it does not change gameplay, archive resolution or engine file formats.

The controlled preview ZIP attributes 33642412 compressed bytes to the
launcher out of 39891437 total. The current runtime is an economical migration
from Electron, not the minimum possible native implementation. A system
webview can replace the window host, but retaining a Node sidecar retains
the main remaining runtime cost. Removing it means porting the backend's
filesystem/process services and existing lobby logic to native code. That is
a larger implementation and compatibility project, not a packaging switch.

For this pass, add a maximum-compression 7z portable download alongside ZIP
and the installer. Keep ZIP for existing PowerShell installation and preserve
all existing entry points and functionality. Set the NSIS LZMA dictionary to
64 MiB rather than its 8 MiB default. This trades build/extraction memory for
download size; no executable packing or runtime feature removal is proposed.
Archive contents must be verified against the same staged distribution.

Also offer a labelled engine-only portable archive, since the existing engine
entry point already runs without the optional launcher. Keep the native game,
setup/import controls, runtime libraries, shaders and copyright notices. Omit
only the optional `launcher/` tree, its `RWE Launcher.cmd` entry and the
launcher-only `rwe_bridge.exe` helper. The full package retains these features.
The engine-only option does not include the browser launcher's profile/mod
screen, update check or online master-server lobby. It is an explicit download
choice, not a silent removal from the existing full installer or ZIP.

References: [CPack archive formats](https://cmake.org/cmake/help/latest/cpack_gen/archive.html),
[NSIS dictionary size](https://nsis.sourceforge.io/Reference/SetCompressorDictSize),
[Tauri system webviews](https://v2.tauri.app/reference/webview-versions/).

Executed against the same first-pass ZIP using
`tools/package-portable.ps1`, 7-Zip 26.02, solid compression level 9:

| Windows portable download | Bytes | Included interface |
| --- | ---: | --- |
| Previous ZIP | 39891437 | Full browser launcher and native game |
| Full 7z | 25595729 | Identical full package |
| Engine-only 7z | 3539926 | Native game/setup; optional launcher omitted |

The full package is another **35.8% smaller** as a download, with no payload
changes. This is compression, not a smaller installed footprint. The
engine-only option removes the optional runtime and related controls and is
therefore a different, explicitly labelled product choice.

Both archive CRC checks passed. After extraction, SHA-256 and relative-file
comparisons matched every selected source file: all 44 files in the full
package, and all 29 retained engine-only files. No extra files appeared.
The engine-only archive extracts its game files directly into the chosen
directory; extract it into a folder of its own. The full archive retains the
existing top-level distribution folder.

The script parsed under Windows PowerShell; both workflow YAML files parsed
successfully. Windows Release jobs now create/test/upload both variants,
and tagged, nightly and weekly publishing collect both. The existing ZIP,
PowerShell installer and full NSIS installer remain available. The NSIS
dictionary change is configured but has not been measured with a built
installer locally. No Linux or game simulation changes were made in this pass.

## First-pass verification

The self-contained Windows package uses local Node v24.19.0; its launcher tree
is 94824501 bytes, including the runtime, frontend and licences. The branded
GUI executable is 93440512 bytes. Release CI uses the latest Node 22, so its
exact output size will differ.

A controlled Windows preview repacks the downloaded `05f7973b` nightly with
that launcher, updated README and without `lib/`, `include/` and `share/`.
SHA-256 comparisons confirm `rwe.exe`, `rwe_bridge.exe` and `rwe_setup.exe`
are unchanged. Standard ZIP/Deflate at level 9 produces **39891437 bytes**,
against **176625829 bytes** downloaded: **77.4% smaller**. The ZIP integrity
check passed. This is a repacked preview, not a freshly compiled MSVC release
or a published download. Local artifact:
`.local/download-size/RobotWarEngine-05f7973b-browser-preview-Windows.zip`.

Executed checks:

- TypeScript passed; ESLint passed with one existing unused-variable warning
  in `ArchiveFingerprintsEpic.ts`. All 141 Jest tests and four packaging tests
  passed. Coverage includes session/Host/Origin checks, invalid/oversized
  requests, inherited RPC rejection, JSON responses, minimap access limits,
  SSE, idle shutdown during/after a game, and backend digest equivalence.
- All three production webpack bundles built. Optional WebSocket native
  accelerators, the existing yargs dynamic require and the frontend asset-size
  advisory produce build warnings. No Electron runtime is packaged.
- The packaged Windows SEA ran from the preview layout with `RWE_HOME`
  unset. It resolved its sibling engine, discovered shipped data through an
  isolated profile's `Data` junction to `D:/RWE-Data`, obtained seven display
  modes from the real bridge and connected to an isolated local master server.
  The Community Patch toggle crossed the HTTP boundary and persisted in that
  isolated profile, then was restored to off. No user profile was changed.
- The production React bundle mounted against that packaged backend in jsdom:
  Play, Community Patch, import and player-name controls rendered without
  runtime errors. This used a stub EventSource; the separate HTTP/SSE tests
  exercise the stream. No connected real browser was available for visual QA.
- CMake configure and a real `cmake --install` of the existing Release tree
  passed. The staged game contains no dependency development archive/header
  trees, and the explicit xz/libarchive copyright files remain. No engine
  rebuild was required for these packaging changes.

Windows resource editing must precede SEA injection: editing the injected
executable stalled in local testing. The resource editor now has a 60-second
timeout. The final executable retains the RWE product metadata and B4 icon
and successfully starts after injection and the GUI-subsystem update.

Not executed: a Linux SEA/AppImage build, an NSIS installer run, native folder
picker interaction, real-browser visual inspection or an original-game parity
check. Linux packaging has a CI smoke check; ZIP compression and solid LZMA
are configured for subsequent release builds. Linux folder selection uses
the host's Zenity. These are distribution checks, not original-game conformance.
