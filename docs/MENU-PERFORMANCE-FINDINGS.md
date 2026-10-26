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
