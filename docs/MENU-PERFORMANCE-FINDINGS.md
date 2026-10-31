# Menu responsiveness investigation

## MPR-001: Cache the per-scene skirmish map list

Date: 2026-10-26. Status: implementation verified in a Release build.

Scope: the menu's map enumeration, network-schema filter and selected OTA parse.
The console probe calls the same VFS and parser operations used by
`MainMenuScene::getMapNames`, `hasMultiplayerSchema` and
`setCandidateSelectedMap`. It is a diagnostic harness; it does not change menu
input dispatch, file ordering, map selection, rendering or simulation.

### Reference comparison and boundary

Reference: the documented byte-tactics corpus is based on the GOG v3.1
`TotalA.exe`, MD5 `8e74a1dffa1f5988624c52048f5b20cd`, read at
`e89721c46c3fa06e59558419b54fe26061fafb73` under ADR-0004. The interface
register notes that SELMAP uses its shipped GUI and bitmap
(`docs/TOTALA-EXE.md`, interface summary); it does not establish the original's
map enumeration/filter lifetime or a corresponding cache. No original runtime
or map-selector trace was run for this issue. No original parity is claimed for
RWE's cache policy.

The selected-map list is derived from the active VFS: its ordered `maps/*.ota`
names filtered by the existing network-schema parser. RWE caches that result
only in the owning `MainMenuScene`, whose VFS is fixed for its lifetime. The
scene releases the vector when it is destroyed; a new menu scene reads the
current VFS again. This retains archive precedence, sorted listing order,
schema filtering, the default-map fallback, displayed names and map-selection
behavior. There is no runtime archive-mount refresh on this scene. This is a
bounded RWE-original performance policy, not a change to the TA data rules and
not a gameplay divergence.

### Baseline evidence

The pre-change Release object library used for the console probe is from build identity
`3e47fc1ce9468edafa97d7a4a7aad2dd43d10147` (2026-10-25). The issue branch is
`a1c633357825013a5fc7eb934a483bdfe9e0dfb7`. A source comparison found no
changes between those revisions in OTA parsing or the non-recursive VFS
operations used here. Their VFS difference is restricted to case-insensitive
recursive filename collection; the map menu calls the non-recursive method.
The UI changes between those revisions affect dialog layout and the title,
not this path. This defines the limits of the old object library's relevance.

On 2026-10-26, `menu_lag_probe.exe` measured stock data from `D:\RWE-Data`.
It found 99 map names with a network schema among 275 OTA candidates. The
pre-change first in-process map-list operation took 13,830 ms; the next nine
took 148-154 ms each. In another process with the OS file cache warm, five
calls took 150-159 ms each. Reading and parsing the `Coast To Coast` OTA took
842 ms in the first sample and 0.394 ms when warm. The first sample overlapped
the #850 parallel build, so CPU and disk contention are a confound; cold and
warm figures are not a controlled hardware benchmark. The repeated 150 ms map
list work is from actual VFS archive reads and schema parsing, not a simulated
delay.

The hidden-GL `ui_probe --map-layout` baseline was attempted, but did not
complete or write screenshots within about 65 seconds; it was stopped. No
skirmish launch was executed. A separate hidden-GL in-repo harness drove real
menu mouse-down/up/update dispatch and asserted the expected active panels:
MAINMENU to SINGLE was 33.6 ms, Options to STARTOPT 43.9 ms, SOUND first visit
157.9 ms, and repeated SOUND 47.7 ms. This identifies another slow first
visit, but does not separate GUI/resource binding costs. The same harness
opened the actual map selection dialog and asserted that its displayed list
matched the menu's list: dialog open was 201.9 ms and a repeated cached query
was 0.0014 ms. The direct probe after the fix found the same 99 accepted maps;
its warm raw operation still took 149-169 ms, showing the saved work is
avoided on repeated access rather than making the initial load cheaper.

The hidden-GL atlas probe called the same `createTextureAtlases` function used
by `LoadingScene::createGameScene`: 92.7 ms first call and 39.2 ms repeated
call. It does not cover terrain/unit media or the full skirmish startup.
These harnesses do not measure SDL event queue latency or all rendering work.

### Implementation verification

The cache is in `MainMenuScene` and stores the exact ordered, filtered result
from the existing VFS/parser path. The map-selector harness confirms the
rendered list equals that result; selection behavior and simulation are not
changed. The Release build used `build-882` at source revision
`a1c633357825013a5fc7eb934a483bdfe9e0dfb7` with local modifications (build
identity `a1c633357825013a5fc7eb934a483bdfe9e0dfb7-d`). Its output was checked
with the actual Release CLI probes above. General between-button latency and
the complete pre-first-loading-frame/skirmish path remain unresolved; this
change only removes repeated map enumeration/filtering within a menu scene.

## MPR-002: Reuse whole GAF files across entry lookups, and present the loading screen before the load

Date: 2026-10-31. Status: implementation verified in a Release build.

Scope: the front end's panel construction (every button press rebuilds a panel
from its GUI and looks up each gadget's art one entry at a time) and the
loading scene's first presented frame. Input dispatch, panel order, GUI data,
map selection, error reporting and the simulation are unchanged.

### Reference comparison and boundary

Reference: the documented byte-tactics corpus of the GOG v3.1 `TotalA.exe`,
MD5 `8e74a1dffa1f5988624c52048f5b20cd`, read at
`e89721c46c3fa06e59558419b54fe26061fafb73` under ADR-0004. This change uses two
accepted findings in `MENU-LOADING-REFERENCE-FINDINGS.md`; no decompilation was
opened by this implementation, and no original runtime was executed.

- **MLR-001** records that the original's ordinary screen binding retains one
  whole-file screen GAF allocation and resolves gadget entries inside it; the
  ordinary lookup path does not reopen and decompress the same screen/common
  GAF separately for each button. It explicitly permits an RWE-original,
  bounded cache of successful whole-GAF decoding, owned no longer than a
  resource service whose VFS/mount set is fixed. It requires keeping absent
  file, absent entry and decode failure distinct, and forbids changing archive
  precedence, retaining a result across a VFS change, or altering pixels,
  geometry, captions or states.
- **MLR-002** records that the original maintains a loading presentation while
  battle construction proceeds, and that an RWE policy which presents a loading
  frame before its expensive phases is an RWE-original scheduling choice serving
  that intent. It is explicitly **not** an exact original first-frame ordering
  rule.

`TextureService` is constructed after the one `CompositeVirtualFileSystem` in
`GameLaunch` and lives until shutdown; the menu never mounts or removes an
archive at runtime, so the cache's lifetime is inside MLR-001's boundary. The
cache is RWE-original performance policy: no original parity is claimed for its
lifetime, budget or for the loading frame's exact ordering, and no gameplay
divergence follows because every resolved file, entry, pixel and failure message
is the same as before.

### Measured cause

Baseline build: `9b55470acfeaa1fdb5a1dc888984d69504ffd776`, Release, stock data
from `D:\RWE-Data` (TA plus Core Contingency). Machine: Windows 11 Pro, Intel
Core i5-10400F, GeForce GTX 1060 6 GB, NVIDIA 582.66.

`TextureService::getGafEntryInternal` called `VFS::readFile` for the whole GAF
and rebuilt its entry table for **each entry lookup**. The front end's screen
GAFs mostly do not exist (`anims/STARTOPT.gaf` and `anims/SOUNDS.gaf` are absent
from the stock data, so the page's art comes from `anims/commongui.gaf`), which
means a page build performs that whole-file work repeatedly for the same common
file. Measured with the new `menu_lag_probe --gaf` mode, warm OS/vfs cache:

| Operation | Time |
| --- | --- |
| `anims/commongui.gaf`, 899,912 bytes, whole-file read + inflate | 3.71 ms |
| the same file's entry-table parse alone | 0.010 ms |
| ordinary entry search in the parsed table | 0.09 ms for all 64 entries |
| absent `anims/STARTOPT.gaf` lookup | 0.19 ms |

A STARTOPT page build performs roughly a dozen common-GAF reads, which matches
the measured per-press cost before the change: `ui_probe --menu-input-timing`
gave MAINMENU→SINGLE 87.3 ms, Options→STARTOPT 43.1 ms, first SOUND visit
68.2 ms and a repeated SOUND visit 47.2 ms. The repeat figure is the proof that
this was not one-time texture decoding: the decoded entries were already cached
and the same 47 ms was paid again for the same art.

The map list was measured separately and is unchanged by this work: 147-151 ms
for a warm per-scene rebuild (74 ms archive reads, 73 ms schema parsing over 275
OTA candidates) and 535 ms for the first list in a fresh process, consistent
with MPR-001. Entering SKIRMISH still pays that once per menu scene; it is not a
per-press cost.

### Change

1. `TextureService` keeps the decompressed bytes of each whole GAF it has
   successfully read, keyed by upper-cased name to match the VFS's own
   case-insensitive resolution, and parses entries from those bytes. The cache
   is bounded at 32 MiB with oldest-first eviction; a file larger than the whole
   budget is used for that call and not retained. Absent files are not
   remembered (a miss still asks the VFS next time), a failed decode is not
   cached (it still logs on each attempt), and the existing decoded-entry cache
   is untouched. Eviction therefore cannot change what a caller sees.
2. `LoadingScene::init` now only builds the loading panel and starts its
   network service. The first `update` returns so `SceneManager` can render and
   present that frame; the second `update` runs the previous `init` body
   (`loadGame`), which builds the match synchronously as before. This is the
   MLR-002 scheduling choice: the skirmish screen no longer stays frozen while
   the whole match loads. Nothing inside the load changes order, exceptions
   still reach the same handler with the same scene name, and the simulation,
   RNG, network handshake and start placement are untouched.

### Verification

Release `ui_probe --menu-input-timing` after the change, five runs on the same
data (the one warm outlier is GL/driver noise):

| Action | Before | After |
| --- | --- | --- |
| MAINMENU→SINGLE | 87.3 ms | 8.4-9.0 ms (one 11.9 ms) |
| MAINMENU→Options→STARTOPT | 43.1 ms | 10.7-11.4 ms (one 16.3 ms) |
| first SOUND visit | 68.2 ms | 16.4-17.4 ms (one 26.4 ms) |
| repeated SOUND visit | 47.2 ms | 5.4-5.6 ms (one 10.3 ms) |

The probe drives the real `MainMenuScene` mouse-down/up/update handlers and
asserts each resulting panel, so the panels and their contents are the real
ones. `ui_probe --map-names-cache` still reports the same 99 maps and a cached
repeat query of 0.0014 ms; `--map-layout` still reports the original selector
geometry and all 99 descriptions; `--multiplayer` still builds and clicks the
four screens.

For skirmish start, the same Release build was launched as
`rwe --data-path D:/RWE-Data --map "Coast To Coast" --player You;Human;ARM;0
--player AI;Computer;CORE;1`. With the previous code the window captured 1.4 s
after launch was black: `LoadingScene::init` was building the match before any
frame could be drawn. With this change the same capture shows the loading
background and its bars, while the log still places the first game-side log at
about two seconds after "Entering main loop". The cost itself is legitimate
one-time match construction (atlas creation, map/terrain, unit media, AI
setup); the change makes it visible rather than shorter. The load's total
wall-clock time is not claimed as improved and varies with OS cache state.

`rwe_test` passed (2055 cases, 2050 passed, 5 skipped; 216,168 assertions) and
`scenario --run 342-active-tab-stays-pressed` passed.

### Remaining

- The first entry to SKIRMISH in a menu scene still performs the MPR-001 map
  enumeration (147-151 ms warm, ~535 ms when the archive/file cache is cold)
  plus dialog construction and the selected map's OTA/minimap. It is one-time
  per scene, not per press, and was not made asynchronous.
- The match load itself is 1.9-2.6 s on this machine and remains synchronous.
  No worker thread, progress plumbing or async simulation work was added; the
  bars still show the shipped category positions rather than live percentages.
- No original runtime or menu trace was run. The loading-frame and GAF-cache
  boundaries above are static-reference conclusions, not measured original
  timings.

