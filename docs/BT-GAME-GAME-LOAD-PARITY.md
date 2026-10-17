# Match loading, the worker handoff, synchronisation display and cursor coordinates parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-MAIN-LOOP-PARITY.md`, `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`
and `TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `game-game-load` (seven services from `0x497180` to `0x498F70`): the
loading-screen callback that starts a worker thread, the worker's match construction (mission-type
policies, pending saves, start positions and commanders, the common tail), the main thread's
synchronisation and progress drawing, the handoff to the battle, and three screen-to-world
coordinate helpers. This is the finding that implementation work cites for any of them, per
`AGENTS.md` and ADR-0004.

Much of the chapter's subject is covered by neighbouring audits and is only cross-referred: the
start-position retry and the commander and storage helpers are `BT-GAME-MAIN-LOOP-PARITY.md`
rules 3, 8 and 9; the saved-game order, the summary account and the between-missions save are
`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rules 3 to 5; the unit limit is `TA-SYSTEMS-PARITY.md`
rule 35 and `BT-UNITS-UNITS-PARITY.md`; the loading screen's static bars were raised as
`BT-GAME-DATA-FILES-PARITY.md` rule 4. What follows is what those leave: the policies by mission
type, the worker's two waits, the start-slot shuffle, the loading screen's geometry and
synchronisation drawing, the handoff, and the cursor-to-world helpers.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-game-load.md`). Nothing else
  under that tree was opened: no source, no decompilation, and none of its source links were
  followed. Addresses are the ones the chapter names; rules are restated in prose. The spec-room
  discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read,
  read-only; no row concerns these services and none is re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says it makes "no claim of runtime timing, successful network handshakes or save
  compatibility tests". Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with
  no winner picked.
- **The RWE half** is a read-only audit of `LoadingScene.cpp` (`init`, `createGameScene`),
  `LoadingNetworkService.*`, `game/GameSimulationLoader.cpp` (the start-position deal),
  `sim/GameSimulation.cpp` (`dealStartPositions`), `game/GameScene_commands.cpp`
  (`getMouseTerrainCoordinate`, `isCursorOverMinimap`), `game/GameScene_input.cpp` and
  `game/GameScene.cpp` (the minimap drag) at the working tree of 2026-10-17. Nothing was built, run
  or changed; another agent had uncommitted edits, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. Two paths, and what the handoff waits for
3. The worker's waits
4. Mission-type policy and the random seeds
5. Start slots: fixed and shuffled
6. The loading screen: geometry and synchronisation drawing
7. The handoff to the battle
8. The cursor-to-world helpers
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| scope, two execution paths | none | rule 2 |
| main-thread init, worker entry, mission settings | `BT-GAME-MAIN-LOOP-PARITY.md` rule 7 (option flags); `TA-SYSTEMS-PARITY.md` rule 35 (limit) | rules 3, 4 |
| pending saves, battle foundations | `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rules 3 to 5 | covered; one quirk in rule 4 |
| skirmish starts, mission units, restored state | `BT-GAME-MAIN-LOOP-PARITY.md` rules 3, 8, 9; `BT-GAME-PLAYERS-PARITY.md` | the shuffle is new; rule 5 |
| worker tail, completion signal | `BT-GAME-MAIN-LOOP-PARITY.md` rule 6 | rule 7 |
| main-thread sync, drawing, handoff | `BT-GAME-DATA-FILES-PARITY.md` rule 4 (bars never move) | geometry and sync drawing are new; rules 6, 7 |
| coordinate helpers | `BT-MAP-RADAR-PARITY.md`, `BT-INGAME-BUILD-PLACEMENT-PARITY.md` rule 1 | rule 8 |

## 2. Two paths, and what the handoff waits for

**Reference.** Loading runs on two cooperating paths. The main thread displays progress, services
the network and tries to assign start positions; a **worker** builds the match and finally sets a
loaded bit. The six progress bytes (from `+0x38D6F`) are followed immediately by a flags word at
`+0x38D75`: bit zero started, one loaded, two start-assignment requested, three synchronised.
**Six bars at 100 is not the condition for the handoff**; the loaded bit is. The worker is started
with the C runtime's thread creation, which returns a success flag and no join handle; a failure is
fatal. There is no mutex and no join; the cross-thread fields are read through volatile views.

**RWE: not applicable (architectural).** `LoadingScene::init` builds the whole `GameScene` on the
calling thread (`createGameScene`, `LoadingScene.cpp` about 129-159), hands it to the scene manager
and waits for the other peers (`networkService.waitForAllToBeReady()`, about 162-165). There is no
worker, no flag word and no shared progress byte; the five-or-six bars are static
(`BT-GAME-DATA-FILES-PARITY.md` rule 4). The "handoff" is `setNextScene`. The chapter's
cross-thread hazards (volatile reads, no join, an unsynchronised handoff) have no counterpart.

## 3. The worker's waits

**Reference.** In a network game (mission type three) the worker does two unbounded waits. First, if
the local keepalive flags contain mask two, it loops: flush queued packets (when the packet manager
is on), receive packets, refresh the host slot and **sleep 50 ms**, leaving only when the host slot
is not the sentinel ten and the local player-info colour is not 255 and a byte at `+0x8F` is
nonzero; then another 50 ms. Second, after setting the start-assignment-requested bit, it waits in
50 ms sleeps for the synchronised bit **without servicing packets**, relying on the main thread to
process traffic and call the assignment. **Neither wait has a deadline or a cancellation check**, so
loss of progress on the network side leaves the worker waiting for ever.

**RWE: DIFFERS (hardened).** `waitForAllToBeReady` polls every 200 ms for the peers' "done loading"
and **throws after `defaultLoadingTimeout` (120 seconds)**, naming the players who never answered
(`LoadingNetworkService.cpp` about 68-90; the header explains why: an unbounded wait "does not wait:
it hangs"). A peer rejoining a game in progress skips the wait deliberately (`LoadingScene.cpp`
about 164-176). Nothing to port; the chapter's hang is the failure RWE was changed to avoid.

## 4. Mission-type policy and the random seeds

**Reference.** The loading worker seeds the **game generator from the performance counter** (the sum
of its low and high halves) and the **C runtime generator from calendar time**, and resets the game
tick. These are two distinct states. By mission type:

| Type | Initial policy |
| --- | --- |
| 1 (campaign) | sets the non-campaign state to zero, applies the single-player settings and the use-only-these-units restrictions |
| 2 (skirmish) | copies the preference unit limit into the effective maximum, sets non-campaign to one, applies the option block's mission flags |
| 3 (network) | copies the preference limit, sets non-campaign to one, clears pause bit zero, and gets its mission and settings from the **host player's** record |

The flag mapping for types one and two is the mission-option helper (`BT-GAME-MAIN-LOOP-PARITY.md`
rule 7). The effective multiplayer unit limit can later come from the host's data, so the
preference is not the final authority. For a type-three game the mission is loaded from the host's
player-info; host fields supply the non-campaign and cheating state, the commander-death mode, the
two visibility flags and the limit; **some host accesses precede the sentinel test**.

**RWE: DIVERGENCE (architectural) and gaps already held.**

- **Seeds.** RWE's simulation generator is seeded once from the game's seed (`--seed`, the lobby's
  agreed value) so that every peer draws the same sequence (`GameSimulation::rng`); the original's
  per-machine counter seeds are exactly what a lockstep engine cannot have. The C-generator
  consumers are `BT-UTIL-MATH-PARITY.md` rule 4 (the wind interval is the one that matters).
- **Policies by type.** RWE has one load path with options carried in `GameParameters`
  (commander-death mode, start-location mode, the two visibility flags, the schema). There is no
  host-record lookup; every peer builds from the same parameters. The unit limit's absence is
  `TA-SYSTEMS-PARITY.md` rule 35.
- **The "presence, not value" quirk.** The pending-save step tests whether the `BetweenMissions`
  item *exists*, not what it holds (`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 5 covers the
  between-missions save). RWE stores a boolean and reads it, so a save carrying
  `betweenMissions = false` is a live save in RWE and the original's presence test would treat a
  written-false item as between-missions. Unreachable from RWE's own writer, which writes the item
  only when it means it.

## 5. Start slots: fixed and shuffled

**Reference.** A fresh skirmish with no pending save has two placement policies. **Fixed**: walk
the active controller-type-one, two and three players whose kind is not the sentinel ten, and spawn
each commander **at its own slot number**. **Not fixed**: collect those players' slot numbers into
an array, **shuffle the populated prefix**, and assign the entries back to the qualifying players in
ascending slot order. The shuffle runs unconditionally for **more than two** players; for zero, one
or two it first takes a **C-runtime half-range decision**, and calling the shuffle "still does not
guarantee the order changes". The slot numbers can be **sparse**. It is not the networking module's
start-assignment candidate generation (`BT-GAME-MAIN-LOOP-PARITY.md` rule 3).

**RWE: MATCHES in the set, UNCLEAR in the distribution.** `GameSimulationLoader.cpp` (about
1071-1095) collects the filled slots, numbers their candidate positions `slot + 1`, and
`dealStartPositions` (`GameSimulation.cpp` about 2593) returns them as they stand for the fixed mode
and a Fisher-Yates permutation drawn from the simulation generator otherwise, then assigns by
ascending slot. So the positions used are the occupied slot numbers, sparse sets included, each
once, as in the original. What cannot be compared:

- **The two-player case.** The original decides by a half-range test and then shuffles by an
  algorithm the chapter does not give; whether two players swap with probability one half, less, or
  more is unknown. RWE's Fisher-Yates swaps with probability one half exactly. Likely the same;
  UNCLEAR. A player can see it only over many games.
- **The generator.** The original's decision uses the C generator, which is not synchronised and so
  cannot be matched; RWE's uses the simulation's, which it must.
- **Spawn after the deal.** The chapter says the fresh-skirmish path initialises player resources
  after spawning and that the common tail does so *again*. RWE initialises starting storage once
  (`BT-GAME-MAIN-LOOP-PARITY.md` rule 8 holds the resource helper).

## 6. The loading screen: geometry and synchronisation drawing

**Reference.** The loading display is 640 by 480 whatever the configured mode (the offscreen
surface is swapped in and out). The background is the `loadgame2bg` picture. For a mission other
than type one a **centred, translated map-name label** is drawn (the name bounded to 99
characters, lowercased for non-English preferences, in a fixed 128-byte buffer). The six bars use
the `LIGHTBAR` frame zero with its hotspot offsets cleared; labels at X 90 and Y 135, 177, 218,
262, 304 and 347 for Textures, Terrain, Units, Animation, 3D Data and Explosions; the fill runs
from X 205 to **205 plus the percentage times seven over two** (350 pixels at 100), **20 high**,
coloured from GUI palette entries 12 and 10 by completion; a bar newly seen at 100 sets a flash
alpha of 30 that falls by two on qualifying timer changes. Values are bytes with no clamp, so an
invalid percentage can exceed the width. A network game also draws a synchronisation status and
sends the **average of the six bytes** as a two-byte progress packet (type `0x2A`) recorded per
qualifying player. `DrawSyncStatus` renders a GUI layer; when synchronised it shows the completion
text, otherwise it counts the players who are active (types one, two, three, not kind ten) and those
**ready** (percent exactly 100 and a nonzero loaded entry), divides **620 pixels** among the
qualifying players, draws their bars at Y 420 to 435 labelled with the names and the ready count
(singular or plural) at Y 400. There is no zero-player guard before the width division. Every pass
that is still loading sleeps **200 ms**, including a failed screen lock.

**RWE: DIFFERS (presentation; the bars are the register's open item).** `LoadingScene::init`
(about 44-79) builds the same background (`Loadgame2bg`, 640 by 480), the same six category labels
at X 90 and the same bars from X 205 (width 351, height 21, set to `i / 5` "for demo/debugging
purposes"), at Y `136 + 42 i` (136, 178, 220, 262, 304, 346) against the chapter's 135, 177, 218,
262, 304, 347: within two pixels. Differences:

- **No percentage, no flash, no completion colour**: `BT-GAME-DATA-FILES-PARITY.md` rule 4.
- **No map-name label** for a skirmish or a network game.
- **No synchronisation drawing and no per-player bars**: RWE shows nothing of the other peers'
  progress; a slow loader on the other machine is a static screen until the 120-second timeout
  names it. The ready count and the progress packet have no counterpart in RWE's loading protocol
  (`LoadingNetworkService` carries a "ready" heartbeat and nothing graded).
- **Where the loading screen is drawn at all.** `init` builds the scene synchronously, so the
  panel cannot be painted until the load returns (the load runs inside `init`, not traced to the
  frame level); a progress display would need the load moved off the calling thread or split across
  frames.

## 7. The handoff to the battle

**Reference.** The worker's common tail clears the cached picture, picks the side's name prefix by
the **local player index** and formats the main HUD layout as the prefix plus `MAIN2.GUI` (the
packed side-name view starts at the name-prefix field; commander lookup adds four bytes: the
offsets match the side-data table), loads that layer with flag `0x20`, sets the local player-info
**started** mask `0x10`, broadcasts it, updates the network game info, initialises the player
resources, closes and deletes any pending save bank, rebuilds the AI feature cells and **only then**
sets the loaded bit. When the main thread sees it: stop sounds, blank the display, free the loading
picture, restore the configured battle resolution, draw the light bars, call the **main-loop tick
once** directly, show the cursor, install the battle state with its quit-prompt close handler,
zero the progress bytes and flags, select CD category zero and start the next track if none is
playing. The first main-loop tick therefore runs before the normal battle callback is installed.

**RWE: MATCHES in effect.** `createGameScene` builds the HUD from the local player's side
(`BT-GAME-SIDE-DATA-PARITY.md`), the scene starts the building-mood music itself on its first
update (`updateMusic`, `BT-INGAME-DIALOGS-PARITY.md` rule 7), and there is no separate "first tick
before the callback". The started mask and the player-info broadcast have no counterpart (the
lobby is the ready handshake). Nothing observable differs.

## 8. The cursor-to-world helpers

**Reference.** `OffsetWorldPosFromView` adds the view origin less the left and top battle borders
(128 and 32) to a point and forwards it to the terrain clamp; it is not a height sampler.
`MinimapCursorToWorldPos` subtracts the minimap gadget origin, scales by the map pixel size over the
gadget size, zeroes the output and stores the **integer** results in the X and Z whole halves
(fractions zero), taking Y from the ground height; no zero-size guard. `UpdateCursorWorldPos`:
**inside the minimap hit rectangle and with input bit three clear** it scales the offsets straight
to world size, sets input bit zero and clears bit one; **otherwise** it **clamps the point to the
visible battle rectangle**, adds the scroll offsets, clears bit zero and sets bit one according to
whether the *original* point lay inside that rectangle; **bit two becomes bit zero OR bit one**,
not their conjunction. The coordinates go through the terrain clamp and the terrain projection
(height and sea level), are stored, and cell X and Z are the unsigned 16.16 values shifted right by
twenty (so **16-world-unit cells**; a nearby comment calling the format 20.12 is wrong), and the
cell's feature id is stored. The state feeds placement, interaction and command selection; it
authorises nothing.

**RWE: MATCHES in the two regions, UNCLEAR at the edge.** `getMouseTerrainCoordinate`
(`GameScene_commands.cpp` about 258-282) returns the minimap position through
`minimapToWorldMatrix` when the pointer is over the minimap, the world ray's terrain intersection
when over the view, and nothing otherwise; the minimap drag recomputes it every frame (about 695-703
of `GameScene.cpp`). The chapter's "bit two is bit zero OR bit one" is RWE's "over the minimap or
over the world". Differences:

- **No clamping outside both regions.** The original still produces a world position from the
  clamped point when the pointer is over neither (the panel, the top bar), with bit two clear.
  Anything that needs only the position, not the "valid" bit, sees it. The case that could matter:
  **ending a selection drag over the side panel**. RWE builds the band box from the unclamped
  viewport position (`GameScene_input.cpp` about 1060-1062), so the box can extend past the view
  edge; the original's box ends at the view edge. Not traced to a visible result. UNCLEAR.
- **Sub-pixel position.** RWE adds half a pixel to the minimap coordinates and keeps floats; the
  original truncates to integers. A fraction of a world unit, below anything an order shows.
- **Input bit three** (a drag in progress that blocks the minimap region) is RWE's separate drag
  states: while a world selection is being dragged, the minimap is not consulted.
- **The feature id and the cell** are computed on demand in RWE (`getFeatureUnderCursor`), not
  stored per cursor update.

---

## 9. Gaps to fix

Ranked by what a player would notice. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **No loading progress and no peer display**: static bars, no map name, no per-player bars or ready count; the load blocks the first frame. | MISSING (presentation) | code, if the screen is wanted (`BT-GAME-DATA-FILES-PARITY.md` rule 4 holds the bars) |
| 2 | **Two-player start-slot shuffle probability**: the original's half-range decision and then an unspecified shuffle, against RWE's exact coin flip. | UNCLEAR | a spec-room read of the shuffle, or counting games |
| 3 | **A selection drag ending over the panel** extends past the view edge in RWE; the original clamps the end to the view. | UNCLEAR | play-test |
| 4 | **Bar and label placement** is within two pixels of the chapter's. | DIFFERS (trivial) | none |
| 5 | **The between-missions presence test** reads a boolean in RWE. | DIFFERS (unreachable) | none |

No source file was changed.
