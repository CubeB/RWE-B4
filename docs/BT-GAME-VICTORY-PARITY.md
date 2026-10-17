# Mission conditions, elimination tests and replicated movement goals parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GAME-PLAYERS-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `game-victory`: how a campaign mission's win and lose conditions are
registered, polled and notified; how a skirmish and a multiplayer game decide a local victory
or defeat; the copy-protection defeat deadline; and the replicated movement-goal classes that
share the address range. This is the finding that implementation work cites for any of them,
per `AGENTS.md` and ADR-0004.

What the countdown between a decided result and the latch does, the three commander-death modes
and the watching flow are **not** re-audited: they are `BT-GAME-PLAYERS-PARITY.md` rules 6 to 8
and the resolved rows BT-82, BT-83 and BT-89 of the spec-room register. This audit only adds
what the victory chapter says about the *tests* that feed that countdown. The end-of-game screens
are `BT-FRONTEND-ENDGAME-PARITY.md`; the statistics tables are `BT-GAME-STATISTICS-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-victory.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names; rules are restated in prose. The
  spec-room discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) and the
  findings for BT-82, BT-83 and BT-89 were read, read-only, so those rows are not raised again.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No original-game play.
- **The RWE half** is a read-only audit of `src/rwe/sim/MissionRules.*`,
  `sim/GameSimulation.cpp` (`computeWinStatus`, `processVictoryCondition`, `killPlayer`),
  `game/GameSimulationLoader.cpp` (`buildMissionRules`), `io/ota/ota.cpp`
  (`parseOtaMissionRules`), `game/save_util.cpp` and `game/GameScene_endgame.cpp`
  (`localPlayerWon`) at the working tree of 2026-10-17. Nothing was built, run or changed.
  Another agent had uncommitted edits in the tree, so every line number is "about".
- **Documents read first:** `TOTALA-EXE.md` (index, section 88, section 91),
  `TOTALA-EXE-DATA.md` section 113 (the mission rules at runtime), `TOTALA-EXE-MISSIONS.md`,
  `TOTALA-EXE-INTERFACE.md` and the ADRs.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Registration, defaults and the parsing grammar
3. Victory predicates
4. Defeat predicates
5. Event timing: death, capture and creation
6. Persistence of conditions
7. The skirmish and multiplayer tests, and the precedence of defeat
8. The copy-protection defeat deadline (do not port)
9. The packed movement-goal family (not applicable)
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| condition storage, lifecycle, polling order | `TOTALA-EXE-DATA.md` section 113 "The manager" and "How often, in what order" | agrees; the 16-entry cap and the reuse hazards are the chapter's only additions (rule 2) |
| registration fields and parsing | section 105 (the eighteen keys) | agrees; two parse details differ in RWE (rule 2) |
| the eleven victory and seven defeat predicates | section 113, rule by rule | **MATCHES** throughout, with three small points (rules 3 and 4) |
| unit-death and capture timing | section 113 ("kill event before the unit leaves its owner's array") | MATCHES (rule 5) |
| persistence accounts | section 113 table, slots 4 and 5 | RWE saves more than the original does (rule 6) |
| `CheckVictory` types 2 and 3, `CheckAlliedVictory`, `CheckDefeat` | `BT-GAME-PLAYERS-PARITY.md` rule 6 (only the "team game" divergence and the Draw question) | **new detail**; rule 7 |
| mismatch deadline | `BT-GAME-CD-CHECK-PARITY.md` rule 5 (the flag) and `TOTALA-EXE-DATA.md` section 113 ("do not port") | rule 8 |
| packed goal, lite goal, position goal | none | not applicable (rule 9) |

`TOTALA-EXE.md` section 91's first bullet still says the campaign's win and lose rules are
"decoded and not ported", waiting on campaign piece 4. That is **stale**: `MissionRules` is
installed by the loader and ticked by the simulation (`GameSimulation.cpp` about 5323), and the
campaign flow is shipped (ROADMAP, `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`). The bullet should be
struck when that file is next edited.

## 2. Registration, defaults and the parsing grammar

**Reference.** The mission's header is read once and instances are appended: eleven victory
kinds and seven defeat kinds, each registered by a test on its key. Integer kinds register on a
nonzero value; the two timers register on a value above zero and store the time times thirty
as an absolute game tick; the two "any unit passes" defeat kinds register on a value of zero or
more with a default of minus one. **String kinds register on the presence of the key, not on a
non-empty value.** Parameterised kinds are read by a formatted scan whose type name accepts
**letters only**, followed by a comma and integers; the scan's return count is ignored and the
integer locals are not initialised first, so a malformed value registers with whatever the
stack held. The word `ANYTYPE`, ignoring case, is a wildcard **only** in the radius rule and the
two victory pass rules; the build, capture and kill kinds look the name up as a type and
`ANYTYPE` matches nothing. An empty victory list gets a destroy-all rule and an empty defeat
list an all-units-killed rule, added when the lists are first used. The manager holds sixteen
victory and sixteen defeat slots; registration does not check the bound and does not clear
existing counts, and the free routine releases each rule without resetting anything.

**RWE.**

- **Defaults and numeric thresholds: MATCHES.** `buildMissionRules`
  (`GameSimulationLoader.cpp` about 1600-1725) registers the eleven and seven kinds in the
  builder's order, the timers on a value above zero scaled by thirty (saturating instead of
  overflowing), the pass kinds on a value of zero or more with the `>> 4` cell conversion, and
  adds the two defaults when a list is empty. `parseOtaMissionRules` (`ota.cpp` about 387-420)
  defaults the pass keys to minus one.
- **`ANYTYPE`: MATCHES.** Only the radius rule and the two victory pass rules turn it into the
  empty wildcard (`GameSimulationLoader.cpp` about 1620-1630, with the reason in the comment).
- **The letters-only name scan: MATCHES.** `scanLetters` (`ota.cpp` about 153) accepts a
  leading run of letters, so a type name with a digit or underscore is cut where the original
  cuts it (the shipped unit names have neither).
- **String presence versus a non-empty value: DIFFERS.** `readName` (`ota.cpp` about 255-263)
  returns nothing for an empty string, so `BuildUnitType=;` registers no rule in RWE and one
  rule naming nothing in the original. The original's rule can never be satisfied (no type is
  called the empty string), which makes the mission unwinnable; RWE drops it, which makes the
  mission easier. **Not censused:** the shipped campaign OTAs were
  not re-read for an empty string key, and the register's section 105 says nothing either
  way. A mission with one is a content error in either engine.
- **A scan that gets fewer integers than it names: DIFFERS in effect, UNCLEAR for the
  original.** `readTypeAndNumber` (`ota.cpp` about 240-253) and the radius reader reject the
  rule when an integer is missing. The original registers it with uninitialised stack values;
  what a mission then does is undefined, so there is nothing to match. Rejecting is the safe
  choice (`docs/SECURITY-AUDIT.md`: an input fault costs that content).
- **The sixteen-slot bound and the double free: not applicable.** RWE keeps two
  `std::vector`s; a mission with a seventeenth rule is not expressible in the file format
  (eleven plus seven kinds, one of each).

## 3. Victory predicates

**Reference (table in the chapter).** All victory rules are required, polled in order, stopping
at the first that does not hold. Rules in the order the builder makes them:

- **KillEnemyCommander**: the dying unit belongs to slot one and its type is its side's
  commander. Latches.
- **DestroyAllUnits**: slot one's unit count is zero. Not latched: a unit built later makes it
  false again.
- **KillAllMobileUnits**: on a slot-one death of a unit with a motion object, count the
  remaining slot-one units with one and stop after more than one; latches when at most one
  (the dying unit included). No poll of an initially empty army.
- **BuildUnitType**: a slot-zero unit of the type with no build left; latches.
- **CaptureUnitType**: the event unit's old owner is slot one and the name matches; latches
  whatever the destination.
- **KillAllOfType**: as KillAllMobileUnits, for one type.
- **KillUnitType**: a matching slot-one death decrements a strictly positive count; zero latches.
  A configured zero or negative count never succeeds on its own.
- **MoveUnitToRadius**: a slot-zero unit of the type (or any) that is selectable, finished,
  not held by an ownership-transfer lock, and not carried (or carried by an air base),
  with its centre inside the circle measured on the **horizontal** plane; latches.
- **UnitTypePassesX and Z**: a slot-zero unit of the type (or any) whose cell is within two of
  the threshold cell. A band, not a crossing; a unit already in the band satisfies at once.
- **VictoryTimerRunsOut**: the game tick has reached the stored tick. Nothing is stored and no
  sound is played.

Every latching victory rule except the timer plays the "Victory Condition" sound once.

**RWE: MATCHES, rule for rule** (`MissionRules.cpp` about 216-310 for the polls and 380-490 for
the events). Points worth recording:

- **The dying unit is counted.** `Dying` and `countUnits` (about 8-60) keep the unit being
  killed in its owner's count, which is what the "at most one left" tests mean; a captured
  unit is counted for both owners, as in the original.
- **The circle centre** is found by `missionPointOnGround` (about 150-195), which follows the
  chapter's description of the projection helper (clamp, shift, search with half-height
  projection). The test is flat squared distance, edge included (about 255).
- **The usable-unit test omits the ownership-transfer lock.** The chapter lists "transfer
  holdoff zero" among the filters for MoveUnitToRadius and for the defeat rule below; RWE's
  `isUsable` (about 75-90) tests the held-by-mission flag (the selectable bit), the build state
  and the carrier, and its own comment says RWE has no transfer lock. **UNCLEAR, no effect:**
  RWE's gift path does not exist (BT-88), so the lock is never set.
- **`KillAllMobileUnits` uses the definition's mobile flag** where the chapter tests for a
  motion object. The two are the same for every shipped unit type the register examined
  (section 113 says "a nanoframe of a mobile type is mobile"), and the chapter does not give a
  case where they differ. **UNCLEAR**, low value: a census of units with a motion object and a
  zero speed (a few transports) would close it.
- **`DestroyAllUnits` announces once and recomputes**: `celebrate` is guarded by the flag, the
  rule itself is not latched (about 221-230). MATCHES.

## 4. Defeat predicates

**Reference.** Any defeat rule loses; polled in order, stopping at the first.

- **CommanderKilled**: slot-zero commander death; latches.
- **AllUnitsKilled**: recomputed every poll; starts satisfied and is cleared by any slot-zero
  record that is selectable, finished, free of a transfer lock and not (normally) carried.
- **UnitTypeKilled**: any matching-type death, **no ownership test**, decrementing a count and
  latching at zero or below with no floor on the count.
- **AllUnitsKilledOfType**: any matching-type death, then count that type across **both**
  slot ranges; latches at one or fewer.
- **AnyUnitPassesX and Z**: a slot-one unit of any type within two cells of the threshold.
- **DeathTimerRunsOut**: the game tick has reached the stored tick.

None plays the victory sound.

**RWE: MATCHES** (`MissionRules.cpp` about 284-312 and 449-488). The no-floor count, the
two-range recount and the ownership-free death test are all reproduced, with the comments
naming the original's addresses.

## 5. Event timing: death, capture and creation

**Reference.** The death notification runs **before** the dying unit leaves its owner's storage,
orders are cleared or footprints unlinked, so "at most one" counts the unit itself. The capture
notification runs **before** the owner changes and before several validity checks, so it can
fire for a hand-off that then does not complete. No registered rule listens for creation: a
build condition is found by polling. Notifications go to every rule of both lists, without
testing the manager's active flag.

**RWE.**

- **Death before removal: MATCHES.** `unitDying` is called from the kill sites
  (`GameSimulation.cpp` about 1172, 1270, 1375, 3008, 3404) while the unit is still in the map.
  It refuses a unit that is already dead (about 388) so a path that reaches the same unit
  twice in a tick does not count it twice. The original's own guard (the death handler turns
  away a dead unit) is the same rule.
- **Capture as two events: MATCHES** (about 1263-1270). RWE notifies the owner change, changes
  the owner in place, then notifies the cause-4 death of the old owner's unit. This is the
  register's reading (section 113).
- **Notification when the hand-off fails: UNCLEAR, no effect.** RWE tests "already owned by the
  captor" and returns before notifying (about 1236). The original's gift path (BT-88) is the
  one that can notify and then fail on a destination check, and RWE has no gift path at all.
- **The active flag is not tested by notifications: DIFFERS in a harmless way.** RWE returns
  from `unitDying` and `unitChangingOwner` when `enabled` is false. A disabled manager is one
  with no `[units]` records (the register's section 113) or after the `Kill` cheat; the rules'
  state is never read again, so the difference is not observable.

## 6. Persistence of conditions

**Reference.** Each rule saves its satisfied and celebrated flags under an account named after
its class (and the two count-down kinds their remaining count, plus the mobile-army rule its
stored count) and **nothing else**: not the type names, coordinates, radii, timer ticks, not
the number of rules or the manager's active flag. A restored game depends on the mission
configuration being registered again identically, and two rules of one class share one account.

**RWE: DIFFERS, safer.** `saveMissionRules` / `loadMissionRules` (`save_util.cpp` about
142-200, called at 692 and 870) write the whole rule list, every threshold and the countdown,
and the manager's `enabled` flag. A restored RWE mission does not depend on re-reading the
OTA and cannot disagree with it. This is the divergence `TOTALA-EXE.md` section 88 implies for
all saved state (RWE's saves are its own format) and needs no change. The `operator==` default
on the structure means the hash and the round-trip test see every field.

## 7. The skirmish and multiplayer tests, and the precedence of defeat

**Reference.** The top-level check returns false when the manager is inactive. By game type:

- **Type 1 (campaign)**: all victory rules; defeat is any defeat rule. Victory is polled first.
- **Type 2 (skirmish)**: victory is "every other physical slot that the local player is **not
  allied** with has a unit count of zero", with **no filter** on whether the slot is active,
  its type, whether it is an observer or whether it ever created a unit. Defeat is the local
  unit count being zero.
- **Type 3 (multiplayer)**: `CheckAlliedVictory`, with the same local-count test for defeat.
  `CheckAlliedVictory` returns **false whenever the commander-death mode is 2** (the
  Deathmatch rule: elimination there is a respawn, not a defeat). Otherwise it examines every
  other active slot of type 1, 2 or 3 that is not the sentinel and not an observer:
  - a qualifying slot that **has never created a unit blocks victory** even with a zero count
    (so a player still to start cannot be beaten by a game that began without them);
  - a slot that created units and now has none is skipped;
  - every remaining live participant must be allied with the local player **both ways**
    (the local player's table lists them and theirs lists the local player), must carry the
    allied-participant flag, and every other still-holding-units or never-started active slot
    must appear in **that participant's** allied table too;
  - there is **no check that the local player has a unit left**.
- In a non-campaign game defeat is checked before victory (BT-82), so a local machine that
  has lost its last unit in the same observed second that the last enemy dies is **defeated**,
  not victorious.

**RWE.** The simulation decides once for the whole game and not once per local machine
(`computeWinStatus`, `GameSimulation.cpp` 2615-2680); a mission is decided by the mission
rules alone (2619-2631). Rule by rule:

- **Mission: MATCHES.** Victory first, defeat only when victory does not hold, five-second
  countdown (`MissionRules::update`, `MissionRules.cpp` about 330-375; the countdown is the
  register's section 113 and `BT-GAME-PLAYERS-PARITY.md` rule 6).
- **Perspective: DIVERGENCE, not documented as one.** A TA machine decides its own local
  outcome; the lockstep game decides one outcome for everybody. Nothing visible follows from
  that in a game with one local human, but `TOTALA-EXE.md` section 88 does not list it, and
  it is why the next four points are phrased as differences in the tests rather than in the
  results.
- **Elimination test: DIFFERS in the corner cases, MATCHES in the common one.** RWE marks a
  player Dead when a commander dies in Game Ends mode or, in Continues mode, when the player
  "had a unit and now has none" in the same tick (`processVictoryCondition`, about 4396-4470),
  and the game is won when every Alive player is on one team. In the original the test is a
  unit count of zero for every non-allied slot. The two agree whenever a player's last unit
  dies; they differ for a player that **never had a unit**. RWE's "had one, has none" guard
  (the comment about the battle harness's first tick) means such a player stays Alive for ever
  and, in a two-player game, **blocks the victory** — the original, for a type 2 game,
  would count that slot as eliminated; for a type 3 game it would block the victory exactly as
  RWE does. RWE has no `unitsCreated` counter (the field does not exist in `GamePlayerInfo`),
  so it cannot tell "not started" from "eliminated" other than through this guard. No shipped
  setup produces such a player (every seat starts with a commander).
- **Alliance direction: DIVERGENCE (documented).** RWE's alliance is the lobby's symmetric team
  id; the original's tables are directed and can be changed in game by a gift of alliance or by
  the share commands. `BT-GAME-PLAYERS-PARITY.md` rule 6 and the closing list of documented
  divergences already hold this. A consequence not stated there: the original's both-ways test
  and the sub-table test of `CheckAlliedVictory` exist only because alliances can be one-sided
  or partial; with symmetric team ids they reduce to RWE's "all survivors on one team".
- **Draw: DIFFERS, and the register can now settle it.** RWE returns `WinStatusDraw` when no
  player is Alive (about 2678). The original has no draw: with defeat checked first (BT-82),
  the local machine whose last unit dies in the same observed second as the last enemy
  loses. `BT-GAME-PLAYERS-PARITY.md` rule 6 lists Draw as "UNCLEAR"; with the BT-82 ordering
  it is a **defeat for each machine that observes its own count at zero**. The result is
  hash-visible only through the banner, the statistics header and the `GameEndedReport`
  outcome string, none of which touches the tick stream. **Original check:** two commanders
  killing each other in one second in a skirmish, and which banner appears.
- **"Allied victory blocked by Deathmatch": MISSING with the mode itself** (BT-83 is recorded
  against `GameParameters.h`; nothing to add here).
- **The local player's own survival: DIFFERS.** `CheckAlliedVictory` never tests the local
  count; the defeat test above has already caught that case. RWE's `computeWinStatus` returns
  `Won` naming the living team's lowest id, and `GameScene::localPlayerWon`
  (`GameScene_endgame.cpp` about 540) answers "did the named winner equal the local player".
  **A probable code fault, not a reference conflict:** in a team game where the local player's
  team wins, and a teammate with a **lower player id** is still alive, the local player is
  told **Defeat** (the banner, the chart title and the glamour-picture decision all read
  `localPlayerWon`). The game does end for the right team; only the local reading is wrong.
  Traced by reading, not run. The fix is to ask whether the local player's team is the
  winning team, using the same team rule as `computeWinStatus`; the existing
  `WinStatusWon` shape carries one id on purpose (the comment at about 2672), so the question
  has to be asked of the simulation, not answered from the id alone.

## 8. The copy-protection defeat deadline (do not port)

**Reference.** The defeat check first reads the CD path mismatch flag. If it is set and the
stored deadline is zero, the deadline is set to an absolute game tick between 9000 and 17999
(a random number scaled by 9000, plus 9000); when the tick reaches it the deadline resets and
defeat is returned. Clearing the flag does not clear the deadline.

**RWE: not applicable and correctly omitted.** `TOTALA-EXE-DATA.md` section 113 says not to
port the copy-protection branch; the flag itself has no counterpart because RWE has no disc
(`BT-GAME-CD-CHECK-PARITY.md` rule 5). The 9000-17999-tick range (five to ten minutes) is worth
one line in the register next to the existing remark, since the earlier reading gave no
numbers; nothing in RWE changes.

## 9. The packed movement-goal family (not applicable)

**Reference.** The end of the address range holds three goal classes derived from the path
goal: one that keeps a position, velocity and heading copied from its owner and moves them
toward a target; a "lite" goal that adds a dirty bit and the unit's flight mode; and a "position"
goal that reads them back from a bit stream. Their wire form is a two-bit target discriminator
(none, a type-two order effect, a type-three order effect), then the payload, then two bits of
flight mode; the reader replaces its target destructively and the writer emits no discriminator
for an unsupported target type. Distances above 160 world units replace the goal height with
sea level or the owner's bucket height plus the definition altitude; above 320 the heading is
turned toward the goal. A memory-status probe at the end returns nothing.

**RWE: not applicable.** The lockstep game sends commands, not unit state (`CLAUDE.md`
"Determinism"). The TA-host path (`net/ta/TaHostGame`) reconstructs gameplay from a joining
TA's full-state records, and whether any of this bit form appears in those records was not
traced; `TA-DEMOS.md` records the subpacket families RWE reads. The one use of the chapter would
be to decide that a record's trailing bits are "target discriminator plus flight mode", which
is a question for the demo reader and not for the simulation.

---

## 10. Gaps to fix

Ranked by what a player would notice. Rules 3 to 5 need nothing.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A teammate with a lower id leaves the local player with a Defeat banner after a team win.** `localPlayerWon` compares the single named winner with the local id. | DIFFERS (probable code fault, traced by reading) | code: ask the simulation whether the local player's team won; then a test beside `computeWinStatus`'s team cases |
| 2 | **No draw in the original.** A simultaneous wipe is a defeat for the machine that observes its own count at zero (defeat is checked first outside a campaign). RWE shows a draw. | DIFFERS; the register's open "Draw" row is answered by BT-82 | original check (one skirmish), then map the draw to a local defeat in the scene (presentation only) |
| 3 | **A player that never held a unit is not eliminated** and blocks a victory in RWE (the guard against a first-tick elimination has no `unitsCreated` to test). | DIFFERS, no shipped setup reaches it | none for now; record next to the battle harness comment |
| 4 | **`BuildUnitType=;` and its string-key siblings** register an unsatisfiable rule in the original and no rule in RWE; a scan with too few integers registers garbage in the original. | DIFFERS, mod content only | none; the safe reading stands |
| 5 | **`TOTALA-EXE.md` section 91's first bullet is stale** (the rules are ported). | documentation | edit the register |
| 6 | **The 9000-17999-tick mismatch deadline** has numbers the register lacks. | documentation | add one line to section 113 |
| 7 | **`KillAllMobileUnits` tests a motion object in the original and the mobile flag in RWE.** | UNCLEAR, low value | a census of shipped definitions that differ |

No source file was changed.
