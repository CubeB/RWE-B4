# Unit storage, creation, death and terrain helpers parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for how a unit is allocated,
created, killed and removed, how kills and losses are counted, and the small
terrain-height helpers, per `AGENTS.md` and ADR-0004. The wreck itself, the
death causes and the damage pipeline are `TOTALA-EXE-WRECKS.md` and
`TOTALA-EXE-WEAPONS.md` section 6; they are cross-referenced, not restated.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-units.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none
  of its source links were followed. Addresses are the ones the chapter names;
  rules are restated in prose, with constants and arithmetic, and nothing is
  copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable and, for the death record, checked against 60,075 recorded deaths in
  the demo corpus. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo
  oracle was run for this audit. The Gaps table says where the corpus could.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/io/fbi` and
  `src/rwe/collections` at the working tree of 2026-10-16 (HEAD `7c163690`, branch
  `ai-envoy-unload-commander-flee`). Nothing was built, run or changed, and tests
  were not run; a MATCHES below means the code reads as the rule says.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Runtime identity and allocation
2. Definition-based initialisation
3. Script, model, weapon and motion components
4. Ordinary creation and its rejection paths
5. Replicated creation
6. Producing a death event
7. Applying death and detaching dependencies
8. Losses, kills and ranking
9. Reclaim, explosion, corpse and final release
10. Bulk destruction
11. Terrain and coordinate helpers
12. Gaps to fix

---

## 1. Runtime identity and allocation

**Reference (section 1).** A unit record is 0x118 bytes. The pool holds
`maxUnits x 10 + 1` slots; slot zero is reserved with player index 0xff. **Each of
the ten players receives a disjoint inclusive range of `maxUnits` slots**, assigned
after the players are sorted (by network id in a multiplayer game, otherwise by
record address). A unit's id is its slot index, so ids are reused and an old
reference could name a later occupant. Definition index zero marks an unused
slot; a runtime flag guards live lifecycle operations. The pool count is an
unsigned short.

**RWE.**

- **Capacity: MISSING, and the question is half answered.**
  `TA-SYSTEMS-PARITY.md` rule 35 left open whether the unit limit is per player or
  per game. This chapter says the arrays are **per player**: each player owns a
  range of `maxUnits` slots and creation fails when that player's range is full.
  RWE has no cap (`GameSimulation.cpp:1683-1749`; the limit is read at load and
  used only by the classic AI, `GameSimulationLoader.cpp:1010`). The source of
  `maxUnits` (map, lobby or preference) is still not settled by the chapter.
- **Ids: DIVERGENCE (structural), and safer.** `VectorMap` hands out ids that carry
  a generation (`src/rwe/collections/VectorMap.h`), so a stale `UnitId` cannot name
  a later occupant of the same slot. The hazard the reference's reference-list
  machinery exists to prevent does not arise by the same route in RWE; the
  references that remain (orders, the AI blackboard) are cleaned by their owners.
- **Player sort and id ranges: N/A in the simulation.** They matter on the wire
  and in recorded demos; `DemoRecorder` and the TA live sender own that mapping
  (`TA-DEMOS.md`, `TA-NETWORK.md`).

**Remaining uncertainty.** Whether a defeated or departed player's range stays
reserved; only relevant once the cap exists.

## 2. Definition-based initialisation

**Reference (section 2).** A new unit is initialised from its definition. A
completed unit has `buildLeft` 0 and full health; **an under-construction unit has
`buildLeft` 1 and health 0**. `buildLeft` is the fraction remaining, so progress is
one minus it. Initialisation clears the script low-state bits, activation, work
time, damage timers, transfer holdoff, sight caches, kills and attacker metadata;
derives the footprint-origin cell from the world position and half footprint;
resets all three weapon targets; copies the standing move and fire orders and the
attack capability; marks the transport piece 0xff; randomises a hover phase and
sets squad 0.

**RWE.** **MATCHES.** A new `UnitState` has `hitPoints` 0 and is made complete by
`finishBuilding` (`UnitState.cpp:448-453`); health follows build progress
(`addBuildProgress`, `UnitState.cpp:167-192`). Standing orders are seeded once from
the definition (`GameSimulation.cpp:1537-1538`), as the register records. Squads
are not modelled. The hover phase is presentation. One stale comment is worth
fixing: `UnitState.h:708` says friendly-fire kills are counted, and
`GameSimulation.cpp:3309-3323` (correctly, per the register) does not.

## 3. Script, model, weapon and motion components

**Reference (section 3).** A unit with a script allocates it, builds the model
state, links the two and **starts `Create`**; without a script the pointer stays
null. Motion state is created only when the definition's mobile code equals 1, so
a visible model or an attack flag alone does not allocate it. Initial heading is
randomised and then replaced for mobile code 1 by the definition's build angle.
The deleting destructor and the script callbacks belong to the script-interface
module.

**RWE.** **MATCHES in shape**: `Create` is started in `UnitBehaviorService::onCreate`
(`UnitBehaviorService.cpp:137-190`) and motion is the physics variant chosen at
creation. The heading is **DIVERGENCE (documented)**: `buildangle` is ignored and a
mobile unit spawns facing a half turn (`GameSimulation.cpp:1584-1589`), section 88.

## 4. Ordinary creation and its rejection paths

**Reference (section 4).** Creation rejects definition zero and definitions
lacking the **availability bit** (0x800000). If a **per-type limit** differs from
minus one it counts that type in the player's inclusive range (unfinished units
count) and refuses at the limit. Without an explicit id it takes the first free
slot in the player's range; with an id the slot must belong to the player and be
unused; no capacity returns null. After initialisation it sets the mode, updates
height, inserts map occupancy, sends the new-unit event, broadcasts a self builder
link for a completed immobile unit, applies activation, reveals the unit, bumps
the counters and notifies the victory conditions. **It does not run the build
placement validator**; site legality belongs to callers.

**RWE.**

- **Availability bit and per-type limit: MISSING.** The per-type limit is, by
  `TOTALA-EXE-MOVEMENT.md` (the `norestrict` readers), the lobby's **Unit
  Restrictions** editor; RWE hides that control (`MainMenuScene_multiplayer.cpp:1470,
  1504`) and the simulation has no count. Together with rule 1 this is one gap:
  see the Gaps table. The availability bit is the "may this side build this at
  all" gate; RWE's equivalent is the builder's menu, which is data-driven
  (`GameSimulationLoader.cpp:902` and the download-menu entries).
- **No placement validation: DIVERGENCE (documented in effect).** RWE's
  `trySpawnUnit` levels a building onto its footprint (`:1688-1697`), a mobile
  unit onto the ground or sea (`:1698-1712`, citing the original's own settle),
  then `tryAddUnit` **refuses on any blocked cell** (`:1767-1775`). The original
  never asks. The register's treatment of this is in section 88 (the build-site
  waits, the mission scenery clear).
- **Events, counters, victory notification: MATCHES in shape.** `UnitSpawnedEvent`,
  the demo and live-sender hooks (`:1719-1745`), and the mission rules'
  notification.

## 5. Replicated creation

**Reference (section 5).** The packet path uses a 0x11-byte spawn record (player
byte, type id, slot id, three fixed-point positions) and a separate player
argument; the body does not use the record's player byte. It needs the player to
have a range, **kills any existing occupant of the slot with kill type 0**,
initialises the type as unfinished, updates height, inserts occupancy, reveals,
counts and notifies victory, and **does not retransmit the new-unit event**. It
skips the availability, per-type, free-slot and range-membership checks and relies
on validated inputs; id zero yields a null that later code does not accept.

**RWE.** **Not audited here.** This is the TA-host and puppet path
(`src/rwe/net/ta/`, `src/rwe/puppet/TadPuppetDriver.cpp`), which has its own
register (`TA-NETWORK.md`, `TA-DEMOS.md`). The one point the chapter adds is that
the original **trusts** its inputs on this path; RWE's equivalent must not
(`docs/SECURITY-AUDIT.md`). **N/A** for parity.

## 6. Producing a death event

**Reference (section 6).** `IsUnitCommander` compares the definition's **name**
case-insensitively with the commander name of the owner's **side**. The kill
routine ignores non-live records; for a commander it clears an owner flag; it then
computes script severity and wreckage mode by kill type and health. Type 7 uses
severity 0 and wreckage mode 1; types 4, 5 and 9, or a unit with positive health,
skip the lethal severity query and start at 0; other lethal paths **compute a
percentage from the negative health, the maximum health and the previous health
percentage**, clamp to 1..100, then query `Killed`, whose result can set the
wreckage. An unfinished unit forces wreckage 0. The event is type 12 (unit id, last
attacker network id, optional attacker unit id, signed severity byte, four-bit
wreckage and kill-type fields), 0xb bytes. A commander kill under an active
commander-death rule also destroys the player.

**RWE.**

- **Commander identity: DIFFERS, no shipped difference.** RWE tests the FBI's
  `Commander` key (`UnitDefinition.h:177`, `GameSimulation.cpp:4243,4359`); the
  original, per both this chapter and `TOTALA-EXE-WRECKS.md` ("The game-end wipe"),
  tests the unit's name against the side's commander name. In the shipped data
  both pick ARMCOM and CORCOM. A mod that marks a second unit `Commander=1`, or a
  side whose commander name differs, parts them.
- **Severity: a candidate answer to an open question, and it matters.**
  `TOTALA-EXE-WRECKS.md` (and `TOTALA-EXE-WEAPONS.md` section 22) read the
  severity as one half of the sum of two terms: the overkill as a percentage of
  maximum health, and a byte at `unit+0xF7` that "has no known writer" and which
  RWE takes as **zero** (`GameSimulation.cpp:3250-3254,3328-3335`). The chapter
  names that second term: **the unit's previous health percentage**. If so, a
  healthy unit killed by one hard blow has a severity near 100 and a unit
  whittled down has a severity near its overkill, both far higher than RWE's: for
  a victim at full health with a blow of twice its health, the original gives
  (100 + 100) / 2 = 100 (the third band, no wreck for most ladders) where RWE gives
  100 / 2 = 50 (the second band, a heap). With the term at zero every kill in RWE
  reads half its original severity, so corpses come out systematically more
  intact. **UNCLEAR**, not MATCHES: the register could not separate the two terms
  ("the distribution does not require a large `F7`"), and the chapter's phrase is
  a reading of a routine, not a measurement. It is the highest-impact open item
  in this chapter. See Gaps rank 1 for a corpus test.
- **Kill types 4, 5, 7, 9: MATCHES** the register (the cause table in
  `TOTALA-EXE-WRECKS.md`; `GameSimulation.cpp:3374-3394`: cause 7 forces level 1,
  an unfinished unit leaves nothing).
- **The kill event record: MATCHES in the demo recorder** (`notifyUnitDied` takes
  severity, cause and level). RWE does not keep a "last attacker" on the victim;
  see rule 8.

## 7. Applying death and detaching dependencies

**Reference (section 7).** The death handler locates the slot, ignores a non-live
record, **may create temporary sight at the death position for the viewing
player**, restores the attacker metadata from the event, and notifies victory
before destroying the components. Cleanup then deletes orders, removes queued
speech, leaves the squad, removes owned projectile associations, detaches from a
carrier and handles carried children: **cargo takes 30000 damage of type 3 when the
parent's kill type is 3, otherwise type 6**, and is detached; the loop consumes the
changing head rather than the original chain. It clears map footprint and
spatial membership, conditionally removes line of sight, and for a **received**
event with positive severity starts `Killed` (the local path already ran it).

**RWE.**

- **Cargo: MATCHES** the register's section 38 (`GameSimulation.cpp:1077`,
  `recordUnitDeath(... "carrier_died" ...)`).
- **Temporary sight at the death position: UNCLEAR / MISSING.** The chapter says
  "may" and gives no condition or duration. RWE creates no sight at a death. If
  the original shows an enemy's last moments in fog, a unit killed beyond the
  player's sight would show an explosion there; RWE draws the explosion only where
  the viewer already sees. Not in the register.
- **Two-phase death: DIVERGENCE (documented as section 132).** The original's
  damage routine, for an active owner, marks a **pending-death bit** and returns,
  and the kill proper happens when the unit's own turn comes
  (`TOTALA-EXE-WEAPONS.md`, `TOTALA-EXE-MOVEMENT.md` section 132). RWE kills inside
  the damage call (`GameSimulation.cpp:3616-3645`). Deferred on purpose.
- **Order callbacks and footprint release: MATCHES in effect.** `deleteDeadUnits`
  (`:4672-4770`) releases the footprint and spawns the corpse after the tick's
  behaviour pass.

## 8. Losses, kills and ranking

**Reference (section 8).** Kill types 1 and 6 enter ordinary loss accounting; type
5 only with a distinct known attacker slot; the owner's losses go up, a known enemy
attacker's player credit goes up **only for a completed victim**, and the commander
losses and kills move when the victim's name matches the side commander. An
attacker unit gets a kill-count increment for a completed enemy victim. Type 3 has a
separate path gated by the local player's **share-vision relationship** with the
owner. Eligible credited attackers move up a **kill ranking** (by commander kills
when the commander-death mode is 2, otherwise total kills); moving to rank zero
emits a lead announcement; a UI flag flashes the kill and loss panel.

**RWE.**

- **Losses and kills: MATCHES the register's table** (section 6 and
  `TOTALA-EXE-WEAPONS.md`, "The player-level counters"): unit and player kills are
  credited only for a finished enemy victim, never for friendly fire, and losses
  count every death (`GameSimulation.cpp:3256-3330`, `quietlyKillUnit` with
  `countAsLoss`, `:2901-2935`). The chapter's gating is the same as the register's.
- **The attacker is the last damager, not the killing blow: UNCLEAR, DIFFERS in
  narrow cases.** The original's death event carries the **last attacker**
  recorded on the victim (the chapter: "last attacker network ID, optional attacker
  unit ID"), and a damage event without an attacker does not clear it
  (`TOTALA-EXE-WEAPONS.md` section 6: the kill count is credited "through the
  victim's last-damager pointer"). RWE credits the attacker named at the killing
  blow and keeps no last-damager field (nothing under `src/rwe/sim` stores one).
  The cases that part: a unit hurt by an enemy and then finished by acid water
  (cause 11, no attacker) or by an environmental blast; in the original the earlier
  enemy is credited, in RWE nobody is.
- **Commander kills and losses, the share-vision gate, the ranking and the lead
  announcement: not found in the simulation.** `GamePlayerInfo` has `unitsKilled`
  and `unitsLost` only (`GamePlayerInfo.h:120-122`). The register says the other two
  counters are displayed nowhere in the shipped interface. The ranking and the
  announcement are HUD and audio and were not audited. **MISSING / UNCLEAR**,
  presentation-side, low.

## 9. Reclaim, explosion, corpse and final release

**Reference (section 9).** Kill type 5 with an attacker credits metal equal to the
completed fraction times the metal cost to the attacker's production accumulator,
scaled to 0.5 or 0.7 for an active type-2 computer owner at difficulty 0 or 1. A
positive script severity on a **completed** unit triggers the death explosion; a
positive wreckage level creates the corpse, independently. The corpse routine walks
the `featuredead` chain, passes position and rotation, gives a downward velocity
over ground at or below sea level unless the unit is flagged as a floating feature,
and suppresses its fallback smoke on a successful water placement. Final release
clears references, deletes script, model and motion, clears the definition index,
decrements the owner's unit count; at zero it announces departure in multiplayer or
**forces "destroyed" in skirmish**.

**RWE.**

- **Reclaimed metal: DIVERGENCE (documented)** (section 88: RWE pays per bite and
  both resources). Difficulty scale: `TA-SYSTEMS-PARITY.md` rule 20.
- **Corpse, depth chain, sink, smoke: MATCHES** (`deleteDeadUnits` `:4687-4705`,
  `trySpawnFeature` `:4595-4633`; `BT-MAP-FEATURES-PARITY.md` rules 5, 12).
- **Zero units means defeat in skirmish: MATCHES**, with RWE's deliberate caveat:
  `processVictoryCondition` (`:4224-4290`) removes a player who "had one and has
  none", so a player who has not been given a unit yet cannot be eliminated.
- **Explosion only for a completed unit: UNCLEAR, and RWE's own comment disagrees
  with the chapter.** A comment in `applyDamage` says units "in TA that are not
  actively receiving build input die with an explosion, even though they leave no
  corpse" (`GameSimulation.cpp:3621-3624`), while the chapter ties the explosion to
  a completed unit. RWE's nanoframes die quietly (`:3631`). There is no register
  finding either way. Listed as an RWE-comment-versus-reference disagreement in
  `BT-NEW-CONFLICTS.md`.
- **Explosion on every `Killed` severity: UNCLEAR.** RWE fires the unit's
  `explodeAs` for every ordinary kill (`:3396-3411`); the original fires it when
  the script's severity is positive, and the severity query is skipped for types 4,
  5 and 9 and for a unit with positive health. RWE reaches the same by sending
  those through `quietlyKillUnit`. No difference found.

## 10. Bulk destruction

**Reference (section 10).** Killing all units of a type, all units, or freeing the
pool uses **kill type 8**. The player wipe visits the player's range and, for
active type-1/type-2 owners, applies **30000 damage of type 3 to the unit itself**;
other roles detonate directly, mark the bit and kill with type 3.

**RWE.** The wipe: **MATCHES** the register (`TOTALA-EXE-WRECKS.md`, "The game-end
wipe"), which already notes that RWE kills outright where the original applies
30000 damage (`killPlayer`, `GameSimulation.cpp:4205-4222`). Type 8 (teardown) has
no counterpart; teardown frees everything.

## 11. Terrain and coordinate helpers

**Reference (section 11).** The helpers have deliberately different failure
results:

| Helper | Result | Off the map |
| --- | --- | --- |
| Cell height (0x485010) | the stored height byte | 0 |
| Ground height (0x485070) | **integer bilinear** from four neighbouring samples, cell size 16, the low four bits of each world coordinate as the within-cell offset; the 16-bit fraction is not used | **-1**, including a missing right or bottom neighbour |
| Cell mean height (0x485140) | average of the cell's low and high bytes | -1 |
| Cell position (0x485330) | cell to world position at the stored height | output unchanged |
| Clamp to map (0x4853B0) | clamps X and Z to whole-unit limits | outside values go to 0 or size minus 1, Y untouched |

Mean height is not interchangeable with the interpolated surface or with a
footprint's support height. **The highest-point-on-line routine (0x4851C0)** takes a
sample count from the larger horizontal delta over 16 plus one, samples both ends,
compares terrain plus feature height and the primary occupant's model top against a
running maximum that starts at 0, and is an estimate, not an intersection; its
feature lookup indexes the definition table with the raw cell field without
normalising markers. **The map-info share routine (0x485420)** ORs the recipient's
bit into every quarter-resolution cell that holds the source's bit; it does not
clear existing bits and sets up no continuing relationship.

**RWE.**

- **Ground height: DIFFERS, not in section 88.** `MapTerrain::getHeightAt`
  (`MapTerrain.cpp:253-277`) intersects a vertical line with a mesh of **four
  triangles per cell meeting at a centre vertex** and returns 0 off the map. At a
  cell centre it equals the bilinear value and on the edges it is linear, so the two
  agree on the lines and differ inside a cell by up to a unit or two on steep
  ground. `TOTALA-EXE-DATA.md` (near line 1412) records the original as "a bilinear
  over the height map in sixteenths, each step truncated toward zero, and -1 off the
  map"; `TOTALA-EXE-WRECKS.md` calls the difference "a unit or two". The value feeds
  every ground unit's Y (`UnitBehaviorService.cpp:1898-1927`), the wreck wet-or-dry
  decision at a shoreline (`GameSimulation.cpp:4617`) and the landing test
  (`:4650`), all hashed. **Replay-breaking if changed.**
- **Mean height: DIFFERS.** RWE has one height function, and the wreck landing uses
  it (rule 12 of `BT-MAP-FEATURES-PARITY.md`); the register asked for a line in
  section 88 and there is none.
- **Clamp and cell position:** RWE has its own bounds helpers (`AiMapBounds.h`,
  `MapTerrain::isInHeightMapBounds`); not compared.
- **Highest point on a line: MISSING.** Nothing in RWE computes it and the register
  names no caller. **UNCLEAR** what it gates in the original (a direct-fire
  clearance check is the natural reading). A spec-room read of its callers is the
  next step; do not build it first.
- **Map-info share: DIVERGENCE (ROADMAP).** Allied teams share vision continuously
  in RWE ("allies already share their map", `GameScene_chat.cpp:157-158`;
  `ROADMAP.md` fog leftovers). The original's routine is a **one-shot copy**; what
  triggers it (the alliance change, the `+ShareMapping` toggle of
  `TOTALA-EXE-COMMANDS.md`) is not in this chapter. **UNCLEAR** whether the original
  also shares continuously by another route (the register's "one shared bitmask with
  a bit per LOS group").

## 12. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed.
The demo corpus oracle is `tad_probe`/`tad_episodes` over the recorded `0x0c`
death records (`TOTALA-EXE-WRECKS.md`, "What 60,075 recorded deaths say").

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Severity's second term: the previous health percentage, taken as 0 in RWE, so every corpse level reads low. Rule 6 | `src/rwe/sim/GameSimulation.cpp:3250-3254,3256-3359`; if it must be remembered between hits, a new `UnitState` field in `UnitState.h`, `game/save_util.cpp`, `sim/GameHash_util.cpp`, `game/dump_util.cpp` | **Yes, and cheap.** Spec-room first: does the term mean the health percentage before the lethal blow, or a byte updated on each hit? Then the corpus: for victims known to be at full health, severity should never fall below 50. Hashed. |
| 2 | Per-player unit cap, the 300-tick factory wait, and per-type limits from the Unit Restrictions editor. Rules 1, 4 | `GameSimulation.cpp:1683-1749`, `GameSimulationLoader.cpp:1010`, `MainMenuScene_multiplayer.cpp:1470,1504`, the HUD at `GameScene_render.cpp:609` | **Yes**, as `TA-SYSTEMS-PARITY.md` rule 35 says, but the per-player half is now answered. Where `maxUnits` comes from in a skirmish is still open. |
| 3 | Kill credit goes to the victim's last damager. Rule 8 | `GameSimulation.cpp:3256-3330`, `:3424-3468` (acid water has no attacker); `UnitState.h` plus the three serialisation files | Light. The register's section 6 already reads the pointer; the case is rare (acid-water maps). |
| 4 | Ground height by integer bilinear in sixteenths, -1 off the map; cell mean height for wreck landing. Rule 11 | `src/rwe/sim/MapTerrain.cpp:253-277` and every caller | Light for the bilinear (decoded in the register); a replay-breaking change, so version-gate. The unit-Y effect is small and mostly invisible. |
| 5 | Temporary sight at a death; kill ranking and lead announcement; commander kill and loss counters. Rules 7, 8 | `GameSimulation.cpp:3256-3330`, `GamePlayerInfo.h`, HUD | Spec-room: condition and duration of the death sight. Presentation items need no original check. |
| 6 | Commander identity by side name. Rule 6 | `GameSimulation.cpp:4243,4359`, `UnitDefinition.h:177`, `io/fbi/io.cpp:105` | No. Needs the side table (`SIDEDATA.TDF`) exposed to the simulation; mod data only. |
| 7 | Unfinished units and the death explosion. Rule 9 | `GameSimulation.cpp:3619-3636` | **Yes**, a retail play: shoot a nanoframe and watch. Whichever is right, fix the wrong one of the chapter and the code comment. |
| 8 | Stale comment about friendly-fire kills | `UnitState.h:705-711` | No. |
| 9 | Allied map sharing as a one-shot copy | `GameSimulation_visibility.cpp`, `PlayerVisibility.cpp` | Spec-room: who calls the share routine. |

### Documented divergences this audit leaves alone

`buildangle` ignored (section 88); two-phase death (section 132, deferred); unit
reclaim paying per bite (section 88); the player wipe killing outright (recorded in
`TOTALA-EXE-WRECKS.md`); mission buildings clearing scenery and placement refusal
(section 88).

### Conflicts with existing RWE findings, in one place

1. **Severity's second term.** Register: `unit+0xF7` has no known writer, taken as
   zero in RWE. Reference: it is the previous health percentage. Not a
   contradiction of a finding, but an open question answered the other way from
   RWE's assumption. Open.
2. **Unfinished unit and the death explosion.** The chapter ties the explosion to a
   completed unit; an RWE code comment says the original explodes nanoframes. No
   register finding. Open.
3. **Commander identity.** Chapter and register agree (side name); RWE differs. Not
   a conflict with the register, listed because the two documents are one reading.
