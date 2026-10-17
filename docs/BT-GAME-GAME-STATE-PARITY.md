# Application and battle lifetime, wind, requested and effective speed, and interface state parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-MAIN-LOOP-PARITY.md`, `BT-GAME-PLAYERS-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `game-game-state` (22 services from `0x490AC0` to `0x491EC0`): the three
resource layers, the mode-callback table, the offscreen surface, the initialisation and shutdown
sequences of the application and of a battle, the wind selector and its consumers, the requested
and effective game speed with its broadcast, the saved CD category cache, and the cursor and HUD
layer helpers. This is the finding that implementation work cites for any of them, per `AGENTS.md`
and ADR-0004.

Most of this chapter is the original's process lifecycle, which RWE replaces wholesale (SDL, a
scene manager, `GameLaunch::run`, a simulation that is a value). The two parts with a rule in
them are the **wind** and the **speed**, and both have rows in the spec-room register already.
This audit says what the register's resolutions leave open and what this chapter adds; it does not
repeat them.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-game-state.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links were
  followed. Addresses are the ones the chapter names; rules are restated in prose. The spec-room
  discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read,
  read-only. The rows this audit touches are BT-26, BT-46 and BT-130 (the wind selector, the
  generator's argument, the draw range and interval), BT-23 to BT-25 (pacing), BT-1 (the tick
  phase order); none is re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no registry, display, disc or runtime state was queried. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `sim/GameSimulation.cpp` (`updateWind`, the tick order),
  `sim/UnitBehaviorService.cpp` (`updateWind`), `game/GameScene_commands.cpp` and
  `game/PlayerCommandApplication.cpp` (the speed command), `game/GameScene_menu.cpp` (pause),
  `game/GameScene_tahost.cpp` (the peer's `0x19`), `game/GameSpeed.h`, `game/GameScene_audio.cpp`
  (music types) and `LoadingScene.cpp` at the working tree of 2026-10-17. Nothing was built, run or
  changed; another agent had uncommitted edits, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. Mode callbacks, resource layers, initialisation and teardown (not applicable)
3. Wind
4. Requested and effective game speed
5. The saved CD category cache
6. Cursor and HUD-layer control
7. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| mode callbacks, surfaces, init, shutdown, battle init order | none; `BT-GAME-MAIN-LOOP-PARITY.md` rule 6 for the front-end chain | rule 2, not applicable |
| wind sampling | `TA-SYSTEMS-PARITY.md` rule 10 (the retained-state reading, "clearing the notification is not calm wind"); `BYTE-TACTICS-SPEC-ROOM-FINDINGS.md` BT-26, BT-46, BT-130; `BT-UTIL-MATH-PARITY.md` rule 4; `BT-UNITS-UNIT-MOTION-PARITY.md` rule 2 | already held; rule 3 adds only the counter-anchor check |
| requested and effective speed | `TA-SYSTEMS-PARITY.md` rule 2 (the steps), `TOTALA-EXE-INTERFACE.md` section 70 (the keys), `BT-GAME-SETTINGS-PARITY.md` rule 4 (persistence) | the packet is new; rule 4 |
| CD category cache | `TOTALA-EXE-MUSIC.md` sections 43, 44 | a mechanism disagreement; rule 5 |
| cursor and HUD layers | `BT-INGAME-BUILD-PLACEMENT-PARITY.md` rule 1 (shared input state) | mostly not applicable; rule 6 |

## 2. Mode callbacks, resource layers, initialisation and teardown (not applicable)

**Reference.** Eight front-end modes (initial frame, return to menu, menu, pre-battle, campaign
set-up, loading, battle, end game) each name a frame callback; the mode setter installs a close
handler (the battle quit prompt for mode six, the leave-the-network callback otherwise) and calls
nothing. Three resource layers: the process and archives, the application's shared resources
(`InitGame`, `ShutdownGame`), and a battle's (`LoadBattleAssets`, `ShutdownIngameSystems`). The
battle loader runs in a fixed order (light-bar graphics and speech reset, textures; features and
unit categories; particles and weapon definitions; the map and camera reset; unit definitions and
download menus; link resolution and the passability maps; the wind, with its counter at zero;
three scratch buffers; the pacing clock and game tick; meteors, radar, players, the pathfinder,
explosions, commands) and clears the ten players' created-unit counters last. Teardown frees in
the reverse family order, collects the end-game statistics before destroying live state and closes
the network session only for mission type three. None of it is a transaction with unwinding.

**RWE: not applicable.** A scene manager takes the place of the mode table
(`MainMenuScene`, `LoadingScene`, `GameScene`, `MovieScene`), `GameLaunch::run` is the application
layer, and a battle's resources live as long as its `GameScene` and its `GameSimulation` value.
Order-of-initialisation faults the chapter describes (a pointer left dangling by an earlier free,
`InitGame` run twice over live state, a texture freed before the model that borrows it) have no
counterpart. Two rules survive as data rules and are already held elsewhere: the order in which
definitions resolve their names (weapons before units; `BT-WEAPONS-WEAPON-TYPES-PARITY.md`) and
the fact that the end-of-game statistics are gathered once, when the game ends
(`GameEndedReport`, `BT-GAME-PLAYERS-PARITY.md` rule 6).

## 3. Wind

**Reference (wind sampling and consumers).** The selector acts only when its counter is strictly
less than the game tick. It then advances the counter **from its old value** (not from the current
tick) by an interval of five to fourteen seconds drawn from the C runtime's generator (150 to 420
ticks), draws the speed as the map minimum plus a draw below the span (the maximum is excluded),
draws a new 16-bit direction **only when the speed is not zero**, builds the vector as minus twice
the direction's sine and cosine scaled by the speed, stores the wind fraction as the speed over the
denominator **clamped only from above**, and sets the "new wind" flag. On calls before the next
deadline it only **clears the flag**; the speed, fraction and vector are retained. The flag is a
new-sample notification (the wind-generator script callbacks use it to decide whether to send a
direction and a speed), not a statement that the wind has dropped to nothing. There is no
denominator-zero guard and no direction jitter (the routine's overview comment is wrong). The
battle loader calls the selector once with the counter at zero, **before** the game tick is
(re)set, so whether that first call samples depends on the tick it finds; the loading worker has
already zeroed the tick, so the first sample falls on the first real tick.

**Register.** BT-26 (interval 5 to 14 seconds, speed in `[min, max-1]`, direction only if the
speed is non-zero, wind after the player pass), BT-130 (the same range and the C-library interval),
BT-46 (the argument the generator's script receives is the speed shifted left four bits).
`TOTALA-EXE-ECONOMY.md` near 623 holds the fraction and the denominator 5000.

**RWE: MATCHES in the retained-state reading (`TA-SYSTEMS-PARITY.md` rule 10 made the same finding), with the open rows unchanged.** `updateWind`
(`GameSimulation.cpp` about 4468) keeps `currentWindVector` and `currentWindGenerationFactor` as
simulation members between changes, hashed with the rest, and calls the units' script hooks only on
a change, so the notification gate is modelled by the call itself. The factor is the speed over the
constant 5000 with `rweMin(1, ...)`, the upper clamp only, as in the original. The generator hook
now passes the speed times sixteen (`UnitBehaviorService.cpp` about 198-209), which is BT-46's
reading. Still open in the register and in the tree, not re-raised: the speed draw is inclusive of
the maximum (BT-130; the register resolves it as exclusive); the direction is drawn even at zero
speed (BT-26); the interval comes from the simulation's generator (below); the update runs first in the tick, ahead of the player pass (BT-1).

- **The counter anchor: MATCHES in effect.** The original advances the counter from its previous
  value and acts on the first tick *greater* than it; RWE acts when the tick is *at least* the
  stored time and stores the **current tick plus the interval**. The selector runs every tick in
  both, so a change at tick t in RWE is a change at tick (counter + 1) in the original, and the
  next lands at t + interval in both: the spacings agree and so does the first change (tick one,
  the counter standing at zero and `nextWindSpeedChange` at the tick the game starts on,
  `GameSimulation.cpp` about 248). Worked through, not run. The only way the two part is a tick on
  which the selector was *not* called (a lag catch-up in the original, which cannot happen in a
  lockstep tick).
- **The interval source: DIVERGENCE (architectural).** The original draws the interval from the C
  runtime's generator, which is not part of the synchronised stream (BT-26 says it "differs between
  machines"); RWE must draw it from the lockstep generator or desync. Not reproducible by design.
  It does mean the original's wind schedule differs between the peers of a network game and RWE's
  cannot; nothing observable depends on it beyond the generators' output timing.

## 4. Requested and effective game speed

**Reference.** The reset service sets requested and effective speed to ten **only for mission type
three** (a network game) and clears the pacing carry for every type; for the other types the speed
**persists from the preference**. The setter clamps to one to twenty, prints a translated message
**if the requested speed changed**, assigns both requested and effective speed, and, with broadcast
on, sends **three bytes**: type `0x19`, subtype one and the clamped speed, from the local
DirectPlay id. It can broadcast when the requested value is unchanged, since only the message is
conditional; it does not test host authority itself. The increase and decrease helpers step inside
the bounds with broadcast. The frame pacer may lower the effective speed under lag without changing
the requested one; an explicit set resets the effective value at once. Ten is shown as normal and
other values as an offset from ten.

**RWE.**

- **The steps: MATCHES.** `GameSpeed.h` holds twenty steps, 100 to 2000 per mille, default index 9,
  displayed as the step minus ten (`TA-SYSTEMS-PARITY.md` rule 2).
- **Persistence: DIVERGENCE** already held (`BT-GAME-SETTINGS-PARITY.md` rule 4): RWE starts every
  game at normal, where the original keeps the preference outside a network game.
- **The message on a change: MISSING.** Nothing is printed when the speed key or the options
  slider changes the speed (`GameScene_commands.cpp` about 3114 only assigns; the Space strip shows
  it while held). Presentation.
- **Broadcast and authority: DIFFERS (design).** RWE sends a **command** in the lockstep stream and
  drops it unless player 0 issued it (`GameScene_commands.cpp` about 3108-3120; an own-clock game
  lets the local player set it). The original sends a packet that any non-watcher's key may
  produce and does not itself test authority. The chapter does not say what a receiver does with a
  non-host's packet; `BT-INGAME-KEYS-PARITY.md` rule 7 carries the question.
- **CONFLICT: the packet.** The original's packet is three bytes of **type, subtype, value** (pause
  is subtype zero with a pause bit, speed is subtype one with the clamped level). `TA-NETWORK.md`
  and `TA-DEMOS.md` read the bytes after the type as one 16-bit number in 256ths of normal speed,
  and `GameScene_tahost.cpp` (about 313-331) decodes the peer's packets that way. The two readings
  disagree on the game-start record and on every speed change; the consequences are set out in
  `BT-INGAME-KEYS-PARITY.md` rule 7. Kind: register (an inference the register itself marks as
  needing a check).
- **Effective speed: MATCHES in rule** (`TA-SYSTEMS-PARITY.md` rules 3 and 4 hold the adaptive
  arithmetic; BT-24 records RWE's different cap).

## 5. The saved CD category cache

**Reference.** The registry value `CDLISTS` is a block of **twenty records of `0x88` bytes**, read
whole (zeroed only when the registry read fails, not on a short value), keyed by the disc's serial
at record offset `0x20`, with the per-track categories from offset `0x24`. The reopen service moves
a matching record to the front (a move-to-front list) and copies its table into the sound object.
On a miss the records shift down and the new serial goes in front. A local sixteen-byte default
table (seven ones, then nine zeroes) is handed to the sound object **only when the disc has
exactly sixteen tracks and the first is data**; but "independently of that special test" the first
sixteen cached category bytes of the new record are set from the same defaults. The save loop
copies the per-track categories back for tracks one through the track count, unclamped to the
record, and the registry writes discard their result. After a reopen, a network game in the battle
state selects the next track; any other state stops the audio. The comment suggesting insertion
needs a sixteen-track disc is wrong.

**Register.** `TOTALA-EXE-MUSIC.md` sections 43 and 44: an MRU of 20 discs of `0x88` bytes; on a
miss the shipped defaults apply "**if, and only if**" the disc looks like the TA game disc; the
constructor default is `(i mod 4) + 1` (never Building).

- **CONFLICT (mechanism, low effect): when the defaults apply.** The register says only on a
  sixteen-track disc whose first track is data; the chapter says the *object* gets them only then,
  but the *cached record* gets them on every miss, so on a disc that does not match (the GOG
  rip, which has the register's seventeen files, is the case that matters) the first pump leaves
  the object with the constructor default and **the next pump, a hit on that new record, loads the
  defaults into the object anyway**. If the chapter is right, the register's section 44 prediction
  that a non-matching disc plays types one to four in rotation holds for a moment only. The
  outcome RWE chose (tracks two to seven Battle, eight upward Building,
  `defaultMusicTrackType`, `GameScene_menu.cpp` about 1179-1181) is the one that results from the
  chapter's reading, and from the register's reading on a real disc. Kind: register. Settled by a
  spec-room read of the miss path or, on the GOG build, by what the Custom list shows on a fresh
  install.
- **Everything else: not applicable.** RWE has one list for one album, persisted with the other
  settings (`musicTrackTypes`), with no disc serial, no MCI, no registry block and no
  short-registry-value case.

## 6. Cursor and HUD-layer control

**Reference.** The cursor setter compares its input with the stored signed byte and starts the
animation only if it differs (repeating a mode does not restart the animation) and has no range
check. The hover-mode service has three paths: with the placement bit set and the order mode at
build, it updates the ghost's validity and returns; with neither low input flag set (the pointer
over neither the world nor the minimap) it selects cursor `0x13` if needed and returns; otherwise
it picks the unit under the cursor, stores its id and resolves a cursor from the order mode and the
selection. The two early paths do not clear the stored hover id. The layer-pop service defers the
HUD restoration when certain interface masks are set unless forced, otherwise it pops layers until
the named HUD is on top, and a separate service closes just one layer; the preview-cache service
frees four allocations and clears four globals.

**RWE: MATCHES in effect, with one difference.** `GameScene::update` recomputes `hoveredUnit` and
`hoveredFeature` from the pointer **every frame** (about 757-758), and the cursor is chosen from
the cursor mode and what is under the pointer by the cursor service, so a repeat does not restart
an animation that the service re-selects only on change. Differences:

- **The stale hover id.** In the original the two early paths leave the previous hover id in the
  field; RWE recomputes it, so a stale id cannot persist. No visible effect, since the early paths
  are exactly the cases where nothing is hovered.
- **The layer helpers and the preview caches have no counterpart** (RWE has no floating-layer
  stack; the save browser rebuilds its list each time).

---

## 7. Gaps to fix

Ranked by what could matter. None changes a sim outcome except rows 1 and 2, which are already
open in the register.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **The `0x19` payload**: three bytes of subtype and value in the chapter, a 16-bit 256ths number in `TA-NETWORK.md`; RWE's own-clock host decodes the latter. | CONFLICT, register inference | a capture of a real TA changing speed, or a spec-room read of the senders |
| 2 | **Wind: draw range, direction at zero speed, interval source, tick position** (BT-26, BT-130, BT-1). The chapter adds nothing new here beyond the retained-state reading; the counter anchor checks out. | already open | the register's resolutions |
| 3 | **Who may change the game speed**: any non-watcher in the original (broadcast, no authority test), player 0 only in RWE. | DIFFERS (design) | two-machine play-test |
| 4 | **No message on a speed change.** | MISSING (presentation) | code |
| 5 | **When the CD defaults apply on a disc that does not match the TA disc.** | CONFLICT, mechanism, low effect | spec-room read of the miss path |

No source file was changed.
