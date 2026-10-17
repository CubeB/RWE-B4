# Mission command text, ownership transfer and unit categories parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for how a mission unit's
`InitialMission` text becomes orders, how the first units of a mission are made, what
an owner change keeps and loses, how a definition is looked up by name, which energy
use a generator reports, and how category masks are built, per `AGENTS.md` and
ADR-0004. The grammar and the runtime of the mission mini-language are
`TOTALA-EXE-DATA.md` sections 105 and 114, the mission start is the same file's
"Mission start" subsection, and the owner change as a *destroy and replace* with
cause 4 is `TOTALA-EXE-WRECKS.md` ("Cause 4 is a unit changing hands") and
`TOTALA-EXE-DATA.md` section 113. This audit restates none of them beyond a line.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\units-unit-commands.md`,
  sections 1 to 8). Nothing else under that tree was opened: no source, no
  decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle
  was run. The Gaps table says which items a play-test settles.
- **The RWE half** is a read-only audit of `src/rwe/io/ota`, `src/rwe/game`,
  `src/rwe/sim` and `src/rwe/ai/classic` at the working tree of 2026-10-16 (HEAD
  `404f3c4d`, branch `bt-sim-conformance`). Nothing was built, run or changed, and
  tests were not run; a MATCHES below means the code reads as the rule says. Another
  agent had uncommitted edits in `GameSimulation.cpp`, `GameSimulation.h` and
  `UnitBehaviorService.cpp` while this was written, so line numbers in those files
  may have moved.
- **A census of the shipped mission files** (the `.ota` of every shipped archive that
  carries one: totala1 to totala4, ccdata, ccmaps, ccmiss, btdata, btmaps, worlds,
  171 files, 76 of which carry an `InitialMission`; through `tools/hpi.py`) and **of
  the 272 shipped FBIs** supplies the "what would this change on shipped data" figures.
  Both are counts of text in files, not play results.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. The initial-mission clauses
2. The held-unit rule: which clauses mark a unit as busy
3. Making the mission's first units
4. Ownership transfer: what is kept and what is lost
5. Local receipt, capacity and the callers
6. Definition lookup
7. Energy use of a generator
8. Category masks
9. Gaps to fix

---

## 1. The initial-mission clauses

**Reference (section 1).** The parser reads comma-separated clauses, skips leading
whitespace and dispatches case-insensitively on the first letter. Positions parse as
floats and become 16.16 world X/Z, with Y zero. It does not execute anything: it asks
the order-type resolver for a unit-specific mission kind where one is used and hands
the request to `AddOrder` (always with control value 1). Clauses: `O n n` (two
two-bit standing fields; a missing conversion keeps the initialised value), `M x z`
(mode 2), `U x z` (mode 5), `G name` (find the mission unit, mode 7), `P x z [s]`
(mode 9, optional seconds times 30), `A x z` (position attack, mode 3) or `A name`
(`ATTACKUTYPE` with the resolved definition), `BW [n]` (`BUILDWEAPON`, count default
1), `B type count x z` (`MOBILEBUILD` when the unit has motion, otherwise
`BUILDINGBUILD` without a position), `WA [name]` (`WAITFORATTACK`, falling back to the
current unit), `W [s] [n]` (`WAIT`, seconds times 30 plus the second number), `D`
(`SELFDESTRUCTFG`), `S` (`MAKESELECTABLE`), `I name` (attach at once to the named
mission unit, no order). Names accept letters, digits, underscore and period; **`WA`'s
scanner omits the underscore**. Only `A` uses the success count of its two numeric
conversions to choose between the two forms; the other clauses do not consistently
validate conversion counts before using the numbers. Buffers are fixed and unchecked.

**RWE.** `parseInitialMission` (`io/ota/ota.cpp:266-380`) reads the same text "as
`sscanf` would, typos included" (`TOTALA-EXE-DATA.md` section 105 note, "Ported
2026-09-25 (#55)"), and the loader turns the clauses into mission steps
(`GameSimulationLoader.cpp:1130-1262`) which `MissionScripts` drives from the tick.

- **The clause table: MATCHES** the register and the chapter, clause by clause,
  including `A` choosing on both numbers scanning (`ota.cpp:318-335`), `B`/`BW` by the
  second letter (`:373-389`), `W`/`WA` (`:352-371`), unknown letters skipped (`:393-396`)
  and the `O` single-number form (`:346-349`).
- **Orders are appended in string order: MATCHES.** The mission list is RWE's own step
  list above the unit's queue (`MissionScripts.h`), so "the parser queues, it does not
  run" holds by construction; the steps hand ordinary orders to the unit when their turn
  comes.
- **`WA`'s name class: DIFFERS, unreachable on shipped data.** RWE scans a `WA` name
  with the same class as the other names (`ota.cpp:361-364`, `scanName`: letters, digits,
  underscore, period), so a watched unit whose `Ident` or `Unitname` contains an
  underscore would be found by RWE and cut short at the underscore in the original. The
  census finds **2054 `wa` clauses in 76 missions, 971 of them naming a unit and none
  containing an underscore.** Nothing to change unless a mod names one; the register's
  grammar table gives no scan format for the `wa` name, so this is new information.
- **Missing coordinates: MATCHES the register's reading** that an absent number keeps
  what the previous order left (a stack-residue read in the original, RWE's `pointFrom`
  default). The register calls it out (`TOTALA-EXE-DATA.md` section 114, "the shipped
  `m 1557`, `w 444,m`, the 32 bare `u`"). Do not port the residue; RWE answers
  deterministically.
- **Unchecked buffers: do not port.** The chapter notes a name goes into a fixed buffer
  with no length check, and the register (`TOTALA-EXE-DATA.md` section 105, name copied
  into a 32-byte field) says a port should refuse rather than copy. RWE parses into
  `std::string` (`ota.cpp:185-198`) and the OTA reader is covered by
  `io/malformed_input.test.cpp`.

## 2. The held-unit rule: which clauses mark a unit as busy

**Reference (section 1, fourth paragraph).** The parser tracks whether an
order-producing clause was processed and whether one of the selectable-handling forms
was seen. When processed it clears runtime bit 0x20 (selectable) and, unless the second
marker was set, appends `MAKESELECTABLE` at the end. **A, P, D and S (the position
form of A) set the second marker. `O` and `I` do not set processed. `BW` adds its order
without setting processed**, so a list that is only `bw n` does not hold the unit and
does not get the appended `MAKESELECTABLE`.

**RWE. MATCHES the register and the chapter.** `queued` and `sticky` in the loader
(`GameSimulationLoader.cpp:1130-1262`): patrol, position attack, self-destruct and
make-selectable are sticky; `A name`, `G name` and `B name` queue only when the name
resolves (`:1170-1190`, `:1205-1223`); `I` and `O` queue nothing (`:1188-1209`); and
`BW` is pushed as a step without setting `queued` (`:1225-1228`), with the comment
"Queued, but not an order that holds the unit (0x488115)". A held unit is then
`heldByMission` and takes no orders from anyone (`PlayerCommandApplication.cpp:314-320`).
The chapter's statement that `BW` *does* add an order while the register says `bw`
"queues nothing" is a difference of wording about the same flag, not of behaviour: the
census finds `bw` in four clauses (`krogoth encounter.ota` as the whole list `bw 20,`;
`exp1ac12.ota` as `bw 2`, `bw 4`, `bw 10`), and RWE gives the unit the stockpile order
without holding it, which is the chapter's reading.

## 3. Making the mission's first units

**Reference (section 2).** `CreateMissionUnits` finds each definition by name (a missing
name leaves a null entry and continues). For a found definition it converts the
one-based player to zero-based, **requires a valid active controller role 1, 2 or 3 and
a valid slot, and reports a fatal error for invalid ownership**. It snaps the position
through the definition's footprint helper (modifying the record), then creates the unit
completed in mode 1; the record's immunity bit, the percentage-derived health
(`max x percent / 100`, narrowed, with arbitrary percentages not normalised) and the
heading are applied. A second pass interprets `InitialMission` only for records with both
text and a created unit, so names can refer to units listed later. It does not implement
`creationCountdown` or the other record flags. If the mission unit count is zero it
deactivates the mission conditions; missing definitions for *every* record do not trigger
the same test while the count stays positive.

**RWE.**

- **Two passes, later-listed names: MATCHES** (`GameSimulationLoader.cpp:1130` onward
  resolves names against the units made in the first pass, `TOTALA-EXE-DATA.md` section
  114 "Names").
- **Health and immunity: MATCHES.** `hitPoints` is `max x percent / 100` in 64-bit
  integers with a negative percentage floored to zero (`:1502`), unclamped above the
  maximum like the original; `immune` is copied (`:1533`).
- **Position snapping and overlap: DIVERGENCE (documented).** The register's "Ported
  (#294, #377)" note and `TOTALA-EXE.md` section 88: a mobile unit whose spot is taken
  goes to the nearest free cell within eight, a building clears features or moves in from
  the map edge.
- **Invalid player: CONFLICT (new), with RWE skipping the unit.** The chapter says a unit
  naming an invalid or inactive player is a fatal error; the register says the routine
  formats "Player number %d invalid for unit %s" for `0x4B6290` and **the unit is made
  anyway** (`TOTALA-EXE-DATA.md`, "Mission start"). RWE skips the unit because it has no
  player to give it to (the register's own "Two departures" note). Which of the first two
  is right depends on whether `0x4B6290` is a log call or a fatal exit; the chapter does
  not name it. The census finds one unit with `Player=6` (`maps/two continents.ota`,
  a multiplayer map's unit table with `OffMapUnit=1`, not a mission), and every
  mission unit names player 1 or 2, so no shipped mission reaches it.
- **Mission unit count of zero: UNCLEAR.** RWE has no step that "deactivates the mission
  conditions"; `MissionRules` is attached whenever the OTA has a schema
  (`TOTALA-EXE-DATA.md` section 113). The register notes the no-records branch
  (`0x4904B0`). No shipped mission has an empty `[units]`.
- **Unported flags: MATCHES.** `CreationCountdown`, `BuildPriority`, `MissionCriticalUnit`,
  `AiIgnore`, `AiPriorityTarget` and `InitialGroup` are read by nothing after the reader
  in either engine (`TOTALA-EXE-DATA.md` section 114, "The per-record flags").

## 4. Ownership transfer: what is kept and what is lost

**Reference (sections 3, 4).** `GiveUnitToPlayer` ignores same-owner units, non-live
units and units marked 0x4000, **notifies the victory conditions of the capture before
the receiver is checked**, and requires the destination active. Ownership is not changed
by assigning a pointer: unit storage is partitioned into player-owned id ranges, so a
locally controlled receipt **creates a new unit in the recipient's range** through the
completed-creation routine with the same definition, position and low movement-mode bits,
and kills the old one with 30000 damage of kind 4. On success it **clears the standing
move and fire fields in the replacement's runtime flags**; with a packet it restores
health, construction (as an integer), angles and the stockpiles of each enabled weapon
slot; without one it copies health, construction bits and angles directly and each
enabled slot's stockpile. Then the old unit's activation mask is applied to the new unit
by set and clear calls. **Old orders, the exact unit id, kill experience, the script
execution stack and every transient motion or resource field are not copied.** Gifting or
capturing therefore needs recipient capacity and definition availability, changes the id,
rebuilds scripts and model state, and resets selected command state. The remote handoff
(an active local owner of role 1 or 2 giving to role 3) sends a 0x18-byte type-20 record
and sets a 150-tick post-transfer holdoff; its construction field is the integer cast of
the build fraction, and the three stockpile bytes are all gated by slot 0's flag.

**RWE.** `GameSimulation::captureUnit` (`GameSimulation.cpp:1197-1275`) is the only
owner change. It reproduces the register's *events*, in order: the recorder and the TA
live sender are told while the old owner is still readable (`:1217-1226`), the mission
rules hear `unitChangingOwner` then `unitDying` with the old owner (`:1231-1239`; the
register's "a capture is a kill", `TOTALA-EXE-DATA.md` section 113 point 4), the held and
immune bits are cleared as for a new unit (`:1241-1247`), the order list, build memory,
behaviour state and weapon targets are reset (`:1260-1275`), and a `UnitCapturedEvent`
goes out. It does it **in place**: same unit id, same record. Consequences against the
chapter:

- **Kill experience is kept: DIFFERS.** The chapter says kill experience is not copied;
  the new unit is a fresh record. RWE leaves `kills` (`UnitState.h:711`) on the unit, so a
  veteran taken from an enemy stays a veteran, keeps the damage tiers of
  `TOTALA-EXE-WEAPONS.md` and the higher reclaim bite (`computeUnitReclaimStep`,
  `GameSimulation.cpp:430`). The register's cause-4 text lists what `0x488570` copies (the
  three weapon-slot bytes) and is silent on kills.
- **Standing orders are kept: DIFFERS (reference only).** The chapter says the
  replacement's standing move and fire fields are cleared. Whether that leaves a captured
  unit on Hold Position and Hold Fire for a human recipient, or whether a later pass
  (the creation-time stamping of `TOTALA-EXE-INTERFACE.md` section 133, or the computer
  player's once-a-second reset, `TOTALA-EXE-DATA.md` section 114) puts a posture back, is
  not stated. RWE leaves the unit's posture as the previous owner set it
  (`captureUnit` does not touch `moveOrders` or `fireOrders`). If the chapter is read
  literally, a captured human-owned unit stands silent until the player presses its
  buttons. **Original check first** (Gaps rank 1).
- **Scripts and model state are kept: DIFFERS in presentation.** A new unit runs its
  `Create` function and has fresh piece positions; the captured RWE unit carries its
  threads and pieces across. Visible as a turret that does not snap to its rest pose, a
  factory whose yard does not re-close. Not traced.
- **Capacity: MISSING** with the unit limit itself (rule 5).
- **Hold-off and the remote packet: not applicable.** RWE is lockstep (`CLAUDE.md`,
  determinism): every peer applies the capture, and a Remote player's units are
  refused (`UnitBehaviorService.cpp:5152-5157`, the register's owner-authoritative rule),
  so the type-20 record and its 150-tick holdoff have no counterpart. The packet's
  integer construction cast and its slot-0-gated stockpile bytes are quirks of that record
  and are not to be ported.
- **Victory notification before the receiver check: MATCHES in effect.** The register
  notes the capture event fires even when the transfer then fails for want of a free slot
  (`TOTALA-EXE-DATA.md` section 113 point 4). RWE has no failing path, so the event always
  matches the transfer.

## 5. Local receipt, capacity and the callers

**Reference (section 4, first paragraph).** The local branch accepts only destination
roles 1 and 2. Availability, per-type limits and free recipient slots can reject creation;
**then it returns without applying the destruction call to the original**, although the
capture notification has already been sent.

**RWE. MISSING**, and the gap is the unit limit's, not the capture's. There is no
per-player or per-game cap in the simulation (`TA-SYSTEMS-PARITY.md` rule 35), so a
player at the cap captures freely; nothing in `captureUnit` can fail after the
notification. When the cap is ported, a capture that cannot be made must leave the old
unit with its owner and still have notified the victory conditions (the register's point 4
is the model).

**Callers: MISSING.** The register names three callers of the transfer: the capture
mission, an interface path that hands a unit to another player after checking the
recipient is a human or computer (`0x45577B`), and a sweep that transfers every unit of a
category set (`0x493486`) (`TOTALA-EXE-WRECKS.md`, cause 4). RWE has only the capture. No
shipped data or RWE feature reaches the other two: the gift path has no `PlayerCommand`
(`PlayerCommand.h:231` lists the seven alternatives and none transfers a unit), and the
sweep's trigger is unidentified in the register. The resource gift exists and is audited
in `TA-SYSTEMS-PARITY.md` rule 30.

## 6. Definition lookup

**Reference (section 5).** `FindUnitType` and `FindUnitTypeId` binary-search the
case-insensitively sorted definition table **excluding the sentinel entry zero** and
return a pointer or null, or the stored id or zero. They depend on the loader keeping the
order and the count valid. They find definitions, not live instances.

**RWE. DIVERGENCE (structural), MATCHES in effect.** Definitions are looked up by
upper-case name in a map (`simulation.unitDefinitions.find`, `PlayerCommandApplication.cpp:225-228`);
there is no sentinel and no sort order to maintain. Unknown names refuse cleanly at the
command boundary (`refusalOf`, `:257-270`) instead of reaching an unchecked lookup. The
sort-key difference (file name versus internal unit name, `_` after `Z`) is conflict 17 in
`BT-NEW-CONFLICTS.md` and `BT-UNITS-UNIT-TYPES-PARITY.md` rule 2.

## 7. Energy use of a generator

**Reference (section 5, last paragraph).** `GetEnergyUse` returns the explicit nonzero
`energyuse` first. Otherwise a positive wind rating returns **minus the wind fraction times
the rating**, then a positive tidal rating returns **minus the map's tidal strength times
the rating**, otherwise zero. **Wind wins when both ratings are positive and `energyuse`
is zero.** A negative value expresses generation here, not failed spending; settlement is
separate.

**RWE. DIFFERS for a hypothetical unit, MATCHES for every shipped one.** RWE keeps three
independent fields and applies all of them: `energyUse` as a spend, then `windGenerator`
and `tidalGenerator` as separate credits, each only for an activated unit
(`GameSimulation.cpp:4466-4496`). A unit with an explicit `energyuse` *and* a wind rating
would, in the original, report only the explicit use and make no wind power; in RWE it
would pay the use and also make the power. A unit with both ratings would, in the original,
make only wind; in RWE both. The census of the 272 FBIs finds **two units with a wind
rating** (`ARMWIN`, `CORWIN`, `WindGenerator=30`), **two with a tidal rating** (`ARMTIDE`,
`CORTIDE`, `TidalGenerator=1`) and **none with both or with `EnergyUse` beside either**,
so no shipped unit can see the difference. Mod data only. The wind arithmetic is
`TA-SYSTEMS-PARITY.md` rule 18 and the tidal fallback rule 19.

## 8. Category masks

**Reference (section 6, 7).** A global registry maps category names, sorted
case-insensitively, to zero-initialised 512-bit masks. `GetCategoryMask` returns the
existing mask or **allocates and inserts an empty one** (an unknown category is not an
error). `AddTypeOrCategory` first tries a concrete definition (a hit sets that bit and
reports "was a type"), otherwise ORs the whole category mask into the destination
(accumulating, a copy of current bits, not a live dependency). `AddToCategories` scans a
whitespace-separated list, gets or creates each mask and sets the definition's bit, **and
always sets the bit in `ALL`, even for an empty list**. No bit-index bounds check. Consumers
are selection (`CTRL_F` and the other five tokens), the weapon bad-target and no-chase masks,
and the computer player's per-type and per-category commands. A category is shared
classification data, not a weapon permission.

**RWE. DIVERGENCE (structural) with one MISSING.** There is no mask: a definition keeps its
`Category` text and `categoryListContains` (`sim/UnitDefinition.cpp:6-23`) tests a
case-insensitive token, splitting on space and tab. That serves the bad-target and no-chase
tests (`UnitBehaviorService.cpp:1153,3513,3542`) and the selection tokens
(`GameScene_commands.cpp:399`). An unknown category matches nothing, which is the same
answer as an empty mask. The classic computer player resolves a directive's subject as a
type name first, which locks it, and otherwise as a category token
(`ClassicAiScript.cpp:296-358`), as `AddTypeOrCategory` does.

- **`ALL`: MISSING.** The original sets every definition's bit in `ALL`; RWE only has what
  the FBI's `Category` text lists. The census of the 272 FBIs finds **no FBI whose
  `Category` contains the token `ALL`** (every FBI has a nonempty category). So a classic
  profile line `weight ALL n` or `limit ALL n` matches no type in RWE, is added to the
  script's unknown names (`ClassicAiScript.cpp:338-345`), and is a no-op where the original
  would scale every type. Whether any shipped or community profile uses `ALL` is not known
  here (`docs/TA-COMMUNITY-AI.md` does not say); this is an easy fix and an open question
  of use. The same applies to a mod's selection or AI command naming `ALL`.
- **Mask snapshots and teardown: not applicable.** Masks copied at one time and edited
  later, `FreeUnitCategories` and cached mask pointers have no counterpart.
- **Capacity and bounds: do not port.** The 512-bit width and the unchecked index are the
  original's; RWE has neither limit and needs none.

## 9. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play or capture
run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | What a captured unit's standing move and fire orders are (rule 4) | `src/rwe/sim/GameSimulation.cpp:1197-1275` (`captureUnit`), `capture.test.cpp` | **Yes.** Capture an enemy unit with a commander and open its move-order and fire-order buttons; also capture a unit and see whether it fires without being told. |
| 2 | A captured unit is a new unit: kill experience resets (rule 4) | `GameSimulation.cpp:1197-1275`, `UnitState.h:711` | **Yes.** Capture a veteran (five or more kills) and read its rank on the info panel. |
| 3 | Make `ALL` a category every definition belongs to (rule 8) | `src/rwe/sim/UnitDefinition.cpp:6-23`, `src/rwe/ai/classic/ClassicAiScript.cpp:60-84,296-358`, `ClassicAiController.cpp:251,275` | Spec-room: does any profile the classic engine meets use `ALL`? Otherwise no. |
| 4 | Capture refused at the unit cap, with the notification already sent (rule 5) | `GameSimulation.cpp:1197-1275`, the unit-limit work of `TA-SYSTEMS-PARITY.md` rule 35 | With rule 35's original check. |
| 5 | The interface gift and the category sweep as callers of the transfer (rule 5) | `PlayerCommand.h`, `PlayerCommandApplication.cpp:297-443` (a new command), `GameSimulation.cpp:1197-1275` | Spec-room: what the `0x45577B` path and the `0x493486` sweep are triggered by in play. |
| 6 | Reproduce a fresh script instance on capture (rule 4) | `GameSimulation.cpp:1197-1275`, `sim/cob.cpp` | Presentation only; a play-test of a captured turret and factory would show it. |
| 7 | One generator, one energy-use value (rule 7) | `GameSimulation.cpp:4466-4496` | No: mod data only. |
| 8 | `WA` name scan without the underscore (rule 1) | `src/rwe/io/ota/ota.cpp:361-364` | No: unreachable on shipped data. |

### Documented divergences this audit leaves alone

Mission steps above the unit's queue; mobile units moved to the nearest free cell and
buildings clearing features at mission start; skipping a unit whose slot has no player;
definitions in a map, not a sorted table; the lockstep handoff in place of a packet; the
resource-gift hardening in `ResourceSharing.h`.

### Conflicts with existing RWE findings, in one place

1. **A mission unit naming an invalid player.** Register (`TOTALA-EXE-DATA.md`, "Mission
   start"): a message is formatted and the unit is made anyway. Reference
   (`units-unit-commands` section 2): fatal error. RWE: the unit is skipped. Open until
   `0x4B6290` is classified as a log or an exit.
2. **`bw` and the held flag, wording.** Register (`TOTALA-EXE-DATA.md` section 114): `bw`
   "queues nothing". Reference: `BW` adds its order but does not set the processed marker.
   Same behaviour, different words; RWE follows the reference's reading.
3. **What the replacement unit keeps.** Register (cause 4): the three weapon-slot bytes are
   copied across. Reference: health, construction, angles, stockpiles, activation mask; and
   standing move and fire cleared, kills and orders not copied. Not a disagreement about the
   same fact; recorded because RWE's in-place transfer matches neither list on kills and
   posture.
