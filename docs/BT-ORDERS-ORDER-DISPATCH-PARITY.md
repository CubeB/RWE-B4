# Order-type resolution (GetOrderType) parity audit

Spec-room finding, 2026-10-16. Like `TA-SYSTEMS-PARITY.md` and
`NAVAL-SYSTEMS-PARITY.md`, it allocates no section number in the main register.
This is the finding that implementation work cites for what order a command mode
turns into, which capability and target tests gate it, and where the original
returns "no order" instead of an order, per `AGENTS.md` and ADR-0004. The default
action ladders, the cursor-is-the-decision rule and the mission names are
`TOTALA-EXE-INTERFACE.md` (the command 1, 2 and 5 listings and "The cursor is the
decision"), `TOTALA-EXE.md` section 103 and `TOTALA-EXE-MISSIONS.md`; this audit
restates none of them.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\orders-order-dispatch.md`).
  `units-unit-types.md` was read for the flag names. Nothing else under that tree
  was opened: no source, no decompilation, and none of its source links were
  followed. Addresses are the ones the chapter names; rules are restated in prose.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed. Weaker than `TOTALA-EXE*.md`, which were read from the retail
  executable. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation was used.** No original-game play and no demo oracle
  was run. The Gaps table says which items a play-test settles.
- **The RWE half** is a read-only audit of `src/rwe/game` and `src/rwe/sim` at the
  working tree of 2026-10-16. Nothing was built, run or changed, and tests were not
  run; a MATCHES below means the code reads as the rule says. Another agent had
  uncommitted edits in `GameSimulation.cpp`, `GameSimulation.h`, `UnitState.h`
  and related files; none of the files cited here are among them except
  `GameSimulation.cpp` (line numbers may have moved).
- **A census of the 272 shipped FBIs** (retail install through `tools/hpi.py`,
  totala1 to totala4, ccdata and rev31) supplies the "what would this change on
  shipped data" figures. It is a count of keys in files, not a play result.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape: what the original function returns, and where RWE decides instead
2. Alliance, target liveness
3. Mode 3: explicit attack
4. Modes 4, 5, 6, 7, 8: special attack, unload, pickup, guard, repair
5. Mode 9: patrol variants
6. Modes 12 and 13: reclaim, resurrect and capture
7. Mode 2: the move ladder
8. Mode 1: the default ladders (CONFLICT)
9. Modes 10, 11, 14 and unknown modes
10. No sim-side capability gate
11. Gaps to fix

---

## 1. Shape: what the original function returns, and where RWE decides instead

**Reference (sections 1, 2, 9).** `GetOrderType` (`0x43f0e0`) takes a command
mode, the acting unit, an optional target unit and an optional position, and
returns a mission type or **zero when no branch permits an action**. It inserts
nothing, spends nothing, reserves no site and finds no path. Fourteen modes
exist (default, move, attack, special attack, unload/landing, pickup, guard,
repair, patrol, stop, teleport, reclaim/resurrect, capture, mobile build);
anything else returns zero. The aircraft-versus-ground name pair is chosen by
`canfly` (flag bit 11). Registry initialisation and name resolution still
decide whether a name resolves.

**RWE.** There is **no single function**. The default and MOVE ladders live in
`computeDefaultAction` (`DefaultAction.cpp:248-369`), each armed command's click
in a lambda in `GameScene_input.cpp:595-800`, the button list in
`unitOffersOrderButton` (`OrderButtons.cpp:90-135`), and the order is then handed
straight to the simulation by `localPlayerIssueUnitOrder`. **DIVERGENCE
(structural)**, and the consequence is the root of rules 3 and 10: the original's
"zero, no order" outcome has no counterpart. An armed click either does nothing
(the handler declined it by its own test) or issues an order the simulation then
accepts and runs.

## 2. Alliance, target liveness

**Reference (section 1).** A supplied but not-live target returns zero before the
mode switch. Friendliness is read from **the acting player's alliance entry for
the target's player**; non-friendly is enemy. It is directional and not derived
from shared colour or side.

**RWE.**

- **Dead target: MATCHES in effect.** `computeDefaultAction` returns nothing for
  an absent unit; the simulation drops orders to dead targets.
- **Directional alliance: DIVERGENCE (documented, S:88 area).** `arePlayersAllied`
  (`GameSimulation_visibility.cpp:180-189`) is the lobby's team id and is
  symmetric; the one place that mirrors the register (`GameScene::isEnemy`,
  `GameScene_commands.cpp:1494-1500`) cites the original's ally byte and then asks
  RWE's team relation of the **local** player rather than of the orderer. For a
  selection the two are the same player. `TOTALA-EXE-INTERFACE.md` already records
  the byte; nothing new.

## 3. Mode 3: explicit attack

**Reference (section 3).**

1. Requires `canattack` (flags2 bit 4). Runtime bit 31 admits the weapon-bearing
   branch; a unit without it takes the final zero path.
2. **No enemy target** (a ground point): a primary weapon that is anti-air
   rejects the order. A non-flying actor then gets SUPPRESS; flying actors get
   AIRSTRIKE for a dropped primary weapon, else AIRTOGROUND.
3. **Against an enemy:** a primary to-air weapon rejects a target that is not in
   runtime mode 2 (flying). A target whose **whole world Y plus model-top height
   is below sea level** needs a **water primary weapon or an enabled water
   secondary**. A target whose top is at or above sea level takes an extra hover
   test: a water primary rejects it, and an enabled water secondary can also yield
   zero. The two sea tests are separate and use the **model top**, not the origin.
4. Flying actors: a dropped primary weapon against a nonflying target gives
   AIRSTRIKE; a nondropping attack against a flying definition gives AIRTOAIR;
   other nonflying targets give AIRTOGROUND or AIRTOGROUNDHOVER by `hoverattack`;
   some combinations fall through with no order.
5. A nonflying actor with motion gets ATTACK_CHASE; without motion, the immobile
   runtime flag gives ATTACK_NOMOVE; `kamikaze` can select ATTACK_KAMIKAZE
   "according to the remaining control flow".
6. These are command-selection gates only: range, fire orders, sensors, cloak,
   weapon categories, aim, reload and collision still constrain actual fire.

The air-variant choice in step 4 is `TOTALA-EXE-MISSIONS.md` (the mission-selection
listing around its lines 300-335, including "attacking a bare ground position never
produces `AIRTOGROUNDHOVER`") and is not re-audited here.

**RWE.** `attackTarget` (`UnitBehaviorService.cpp:3596-3680`),
`weaponCanHitUnit` and `modelTopIsAboveSea` (`GameSimulation_visibility.cpp:347-430`),
the armed attack click (`GameScene_input.cpp:595-627`).

- **The sea tests as arithmetic: MATCHES in the helper.** `modelTopIsAboveSea` uses
  the whole parts of origin height and model height and requires strictly above
  sea; the water-weapon branch has the floater exemption and the half-height hover
  rule. These were read against the weapon-eligibility routine
  (`NAVAL-SYSTEMS-PARITY.md` rules 8 and 9) and are the same numbers the chapter
  states for the dispatcher. The chapter gives the dispatcher's own copy of that
  test its own explicit order of branches (below sea, then at or above sea), and
  RWE has one helper for both; no case was found where they split.
- **There is no "zero, no order" result: DIFFERS.** RWE issues the `AttackOrder`
  at the click, and `attackTarget` returns `true` (finished) the first tick the
  weapon cannot reach (`:3625-3640`). The order has by then **replaced the
  unit's queue**. In the original the click yields no order at all (the unit
  carries on with what it had, and the game plays its refusal). A play-visible
  difference: a destroyer told to attack a submerged submarine stops what it was
  doing.
- **Weapon slot 0 only: DIFFERS, with shipped consequences.** `attackTarget`
  tests `weapons[0]` alone (`:3617-3640`, and again for the chase range). The
  chapter's tests name the **primary and the enabled secondary** (the secondary
  water weapon passes a sunk target). Census of shipped FBIs: the Crusader and
  the Enforcer (`ARMROY`, `CORROY`) have a gun in slot 1 and a depth charge
  (`ARMDEPTHCHARGE`, `COREDEPTHCHARGE`) in slot 2, so in RWE an explicit attack
  on a submarine ends at once and the ship never closes, where the chapter's
  rule accepts the order because slot 2 is a water weapon.
  **Not established: whether RWE's per-weapon automatic fire still engages a
  submarine within range.** The ordered chase is what is missing.
- **Anti-air primary against a ground point: MISSING.** For a ground point,
  RWE checks only the shooter's half of the water rule (`weaponCanFireFrom`,
  `:3636-3642`). An anti-air-only unit ordered at bare ground is accepted and
  stands still; the original gives no order. Against a unit target RWE rejects a
  to-air weapon for a non-flying target (`weaponCanHitUnit`), which is the
  chapter's step 3 for the primary.
- **Hover-actor test, step 3: UNCLEAR.** The chapter says "an additional
  hover-actor test" without saying whether the hover property belongs to the
  actor or the target. RWE has the target version (`canHover` target, half model
  height). The register's weapon-eligibility routine
  (`TOTALA-EXE-WEAPONS.md` section 12 area) already settles the per-weapon test as
  a target property; the dispatcher's wording needs the same re-read. Open.
- **Kamikaze first: MATCHES the register, not obviously the chapter.** RWE sends
  a `kamikaze` unit to `kamikazeRun` before any weapon test (`:3610`), which is the
  register's ATTACK_KAMIKAZE (`0x43F38A`). The chapter places the kamikaze arm
  after the weapon-bearing branch's early exits. All 14 shipped FBIs that set `kamikaze` (the six mines of each side, `ARMVADER`,
  `CORROACH`) name no `Weapon1`, so they never meet those tests in either
  reading and no shipped difference is expected. **UNCLEAR** for mods.

## 4. Modes 4, 5, 6, 7, 8: special attack, unload, pickup, guard, repair

**Reference (section 4).**

- **Mode 8 (repair/assist):** delegates to CanRepair (the unit-targets
  predicate): an unfinished qualifying target gives HELPBUILD (VTOL_ variant for
  fliers); a finished one gives REPAIRUNIT. **No separate friendly test** in the
  dispatcher.
- **Mode 7 (guard):** `canguard` and a friendly target give FOLLOW (VTOL_FOLLOW
  for fliers).
- **Mode 6 (pickup):** a target and CanLoad.
- **Mode 5 (unload):** a flying load-capable actor aimed at an airbase gives
  VTOL_LANDING; otherwise `canload` gives VTOL_UNLOAD or GROUND_UNLOAD. **No
  target is needed** for the latter.
- **Mode 4:** `candgun` gives ATTACKSPECIAL and charges nothing at selection.

**RWE.**

- **CanRepair is `canreclamate` copied to flags2 bit 9: the buttons MATCH, the
  default ladders DIFFER.** `unitOffersOrderButton` (`OrderButtons.cpp:107-112`)
  has the comment and tests `canReclamate` for the Repair button. The default
  action ladders (`DefaultAction.cpp:123`, `:128`, `:179`, `:195`) test
  `builder` (flags1 bit 6). The two sets differ on shipped data: of 272 FBIs,
  24 have both keys, **27 have `builder` and not `canreclamate`**, and none has
  the reverse. The 27 are the factories (`ARMLAB`, `ARMVP`, `ARMAP`, `ARMHP`,
  `ARMSY`, the advanced labs and yards, `CORLAB` and the Core equivalents, and
  `CORGANT`), the mine layers (`ARMMLV`, `CORMLV`), the two carriers
  (`ARMCARRY`, `CORCARRY`) and the two platforms (`ARMPLAT`, `CORPLAT`). So in
  RWE a factory (whose default action is the MOVE-button ladder only) or a
  carrier offered a damaged friend gets a repair order and the repair cursor; in
  the original it gets neither. This is the register's own finding being
  mis-ported rather than a new reference conflict
  (`TOTALA-EXE-INTERFACE.md` lines 1315, 1372, 1498; `TOTALA-EXE-ECONOMY.md`
  line 262).
- **Armed repair does not split by target state: DIVERGENCE (structural).** The
  armed click issues one `RepairOrder` to any friendly (`GameScene_input.cpp:775-791`)
  and `handleRepairOrder` finishes an unfinished frame itself
  (`UnitBehaviorService.cpp:4669`). The original names the two orders apart and
  gives **no order** for a full-health finished target. Presentation-visible only.
- **Mode 7 guard: MATCHES.** `DefaultAction.cpp:145` (`allied && canGuard`).
- **Mode 6 pickup: DIVERGENCE (documented).** Own units only, S:88
  (`DefaultAction.cpp:63-80`). The chapter confirms the original has no ownership
  test ("Pickup is not preceded by a separate friendly test").
- **Mode 5: MATCHES.** The landing arm requires `canFly`, allied, not itself and
  a usable airbase (`DefaultAction.cpp:133`); unload offered by `canload`
  (`OrderButtons.cpp:115-117`, `canLoad`).
- **Mode 4: MATCHES** (`OrderButtons.cpp:118-122`: both halves, `candgun` and a
  command-fire weapon).

## 5. Mode 9: patrol variants

**Reference (section 5).** `canpatrol` required. Without motion: QPATROL. With
motion and flags2 bit 9: REPAIRPATROL or VTOL_REPAIRPATROL; otherwise PATROL or
VTOL_PATROL. **Bit 9 is the `canreclamate` copy**, not an informal "builder" class.

**RWE. MATCHES.** `UnitBehaviorService.cpp:4890-4965` keeps a patrol for an
immobile unit (the QPATROL behaviour), and the repair-patrol decision is keyed on
`canReclamate` (`:4912`), with the comment placing the decision at `0x43F3E6`.
The earlier reading in `TOTALA-EXE-INTERFACE.md` and the chapter agree.

## 6. Modes 12 and 13: reclaim, resurrect and capture

**Reference (section 6).**

- Feature selection needs the feature definition's **reclaimable flag 0x80** and
  the **position visible in the viewing player's mask** (a 32-world-unit grid
  projected from whole X and height-adjusted Z; the explored byte alone does not
  suffice).
- **Mode 12** requires `canreclamate`, does the feature lookup **before** it tests
  the position for null (the source marks the null dereference as a suspected
  original bug). Given a position: `canresurrect` and a qualifying visible
  feature give RESURRECT before RECLAIM; otherwise a qualifying feature gives
  RECLAIM (VTOL_ for fliers); with no feature and a target, **live-unit reclaim**
  without repeating CanReclaim.
- **Mode 13:** `cancapture`, a target and a **different owning player**; it does
  **not** require the alliance entry to say enemy.

**RWE.**

- **Reclaim and resurrect gating by flag: MATCHES** (`DefaultAction.cpp:31-61,
  331-342`; the resurrect test also needs the corpse's name to resolve, which the
  original does too per the comment at `:41-47`).
- **Visibility test: UNCLEAR.** The hovered feature comes from the scene's
  hover-pick; whether a feature under fog can be hovered was not traced.
  `BT-MAP-FEATURES-PARITY.md` covers feature visibility; the 32-unit grid in the
  chapter matches the register's vision grid (`TOTALA-EXE-VISION.md`).
- **The null-position dereference: do not port.** The chapter's suspected original
  bug (a mode-12 call without a position crashes) has no RWE counterpart, and
  should not get one (`docs/SECURITY-AUDIT.md`).
- **Capture accepts any other owner in the dispatcher: DIFFERS in the armed UI,
  MATCHES in the sim.** The armed capture click is gated on `isEnemy`
  (`GameScene_input.cpp:699`); `handleCaptureOrder` refuses only a target of the
  same owner and an unfinished frame (`UnitBehaviorService.cpp:5107-5135`). The
  chapter's dispatcher accepts a teammate's unit; whether the original's cursor or
  click handler narrows that to enemies is the cursor question of
  `TOTALA-EXE-INTERFACE.md` ("the cursor is the decision"), and the register's
  ladder tests ENEMY at the capture arm. No difference is expected in play.
  Recorded because it is where an ally-capture exploit would enter a network game
  (rule 10).

## 7. Mode 2: the move ladder

**Reference (section 7).** `canmove` required. No motion: QMOVE. Motion and no
target: ground or VTOL move. With a target, in order: capture an enemy (`cancapture`);
reclaim an enemy (`canreclamate`); assist a friendly unfinished target; repair a
friendly damaged target; land a flyer at a friendly airbase; pick up a CanLoad
target; follow a friendly target with `canguard`; else move. "Capture takes priority
over reclaim."

**RWE. MATCHES the register's listing and the chapter** (`unitLadderRightClick`,
`DefaultAction.cpp:93-151`), with three known departures: the attack arm RWE adds
between reclaim and repair (see rule 8); the repair/assist arms keyed on `builder`
(rule 4); and the own-units pickup (S:88). The factory's rally point from the MOVE
button alone (QMOVE) is `DefaultAction.cpp:345-355`.

## 8. Mode 1: the default ladders (CONFLICT)

**Reference (section 8).** The two interface variants must not be collapsed.

- **Interface type 1 (chapter's wording):** `canattack` on an enemy first, which
  **returns its result directly** whether or not mode 3 yields an order; then enemy
  live-unit reclaim; friendly help-build; friendly repair; friendly airbase
  landing; pickup; friendly follow; visible feature resurrection; visible feature
  reclaim; movement. **No capture arm is listed.**
- **The other variant:** enemy attack and enemy reclaim redirect through modes 3
  and 12; an unfinished repair-eligible target **without a friendly test**; a
  selectable local completed unit yields no order; feature resurrection and
  reclaim; movement needing `canmove` and motion.

**Register.** `TOTALA-EXE-INTERFACE.md` ("Command 1, the default action" and "What
is unsettled", the second bullet): the right-click chain at `0x43FA00` begins at
**capture** and has no attack arm; the register itself says the chain "was read
from its capture arm onward; an attack arm ahead of it ... would not have shown up
in that reading", and RWE "keeps its own attack arm until this is settled". The
left-click chain at `0x43FE35` is as the chapter's second variant.

**CONFLICT (partial, and it answers the register's open bullet).**

1. **The second variant agrees with the register in every arm** (attack and
   reclaim by recursion into commands 3 and 12, repair/help-build, the select
   arm, features, move).
2. **For the right-click scheme the chapter says an attack arm exists, and first**,
   where the register found capture first and no attack. The chapter lists **no
   capture arm** where the register lists one. Both lists agree on everything
   after the enemy-reclaim arm. The chapter's "a failed attack resolution returns
   directly" is the recursion the register could not see.
3. RWE has capture, then reclaim, then attack (`DefaultAction.cpp:104-121`), which
   is neither list. Concretely the commander (`canattack`, `cancapture`,
   `canreclamate`) right-clicking an enemy unit: the chapter predicts an attack,
   the register predicts capture, RWE issues capture.
4. **What settles it.** The ladder is selected per `Interface Type` and the
   shipped default is the left-click scheme, so this is the non-default option.
   A play-test with `Interface Type = 1` and a commander right-clicking an enemy
   unit settles it in one click: the cursor and the order are meant to come apart
   there (`TOTALA-EXE-INTERFACE.md`, "the cursor is the decision"), so the order
   must be read from what the unit does, not from the cursor.
5. **The "failed attack resolution returns directly" half is independent of
   capture**: for an enemy that mode 3 rejects (an anti-air unit against a ground
   target, a ground gun against a submerged submarine), the chapter gives no order,
   where RWE falls through to repair/pickup/guard (none apply to an enemy) and ends
   at a Move order for a mobile unit. So RWE walks a Peewee next to an enemy it was
   told (by right-click) to engage and cannot; the chapter does nothing.
6. **The left-click chain in RWE adds `allied &&` to the help-build arm**
   (`DefaultAction.cpp:179`), which neither the register nor the chapter has. No
   shipped unit can reach the difference (anything with `CanRepair` also has
   `canreclamate`, so an enemy frame meets the reclaim arm first).

## 9. Modes 10, 11, 14 and unknown modes

**Reference (sections 2, 4).** Mode 10 returns STOP and mode 11 TELEPORT with **no
capability test in this function**; "interface exposure or execution restrictions
elsewhere must not be attributed to nonexistent checks here". Mode 14 (mobile
build) requires a non-null buildable-ID list and motion and **does not validate the
requested build ID or site**. Unknown modes return zero.

**RWE.**

- **Stop: MATCHES.** `canStop` gates the button (`OrderButtons.cpp:103-104`); the sim
  takes the stop.
- **Teleport: MISSING.** There is a mission name in the table
  (`LoadingScene_util.cpp:464`) and no handler anywhere in `src/rwe`. Census of
  the 272 shipped FBIs: **two set `teleporter`** (`ARMGATE` and `CORGATE`), and
  RWE does not read the key at all (`io/fbi/io.cpp` has no
  `Teleporter` read). Low priority; listed so nobody assumes the key is handled.
- **Build: DIFFERS.** The build menu is the unit's GUI pages plus download menus
  (`BuildCursorMode`, `GameScene_input.cpp:821`); the simulation takes a
  `BuildOrder` for any known unit type (`refusalOf`, rule 10). The original's
  buildable-ID list (the sidedata `CANBUILD` entries plus download menus) is not
  consulted. See `BT-UNITS-UNIT-TYPES-PARITY.md` rule 9 for the census.

## 10. No sim-side capability gate

**Reference.** Every capability test in rules 3 to 9 happens when the order is
**chosen**, at the issuing machine; execution restrictions are elsewhere.
Nothing in the chapter says a receiving peer repeats them, and the original is
owner-authoritative and not lockstep (`TOTALA-EXE-MOVEMENT.md` section 118,
`TA-NETWORK.md`).

**RWE.** `refusalOf` (`PlayerCommandApplication.cpp:245-294`) checks the issuing
player owns the unit, that a build names a known unit type and one the mission
offers, and that queue counts are in range. It does **not** check `canattack`,
`canmove`, `canreclamate`, `cancapture`, `canload`, `candgun` or the builder's
buildable list. RWE is lockstep, so every peer applies every peer's commands;
a modified client can therefore order any owned unit to do anything the
simulation's own handlers permit (a factory to build another side's units, a unit
without `canreclamate` to be sent to reclaim, a unit with `canstop=0` to
stop). The handlers have some of their
own guards (`handleCaptureOrder` tests `canCapture`, `:5109`). **MISSING** as a
class, and a **security-audit finding in the sense of `docs/SECURITY-AUDIT.md`**
("a fault in content costs that content and never every peer"): a refusal at
`refusalOf` is the same refusal on every peer, so it is desync-safe.

## 11. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play
or capture run on retail Total Annihilation before the code is changed.

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Use the `canreclamate` copy (CanRepair) in the default-action ladders, not `builder`: factories, mine layers, carriers and platforms must not get repair/assist cursors or orders. Rule 4 | `src/rwe/game/DefaultAction.cpp:123,128,179,195`, `DefaultAction.test.cpp` (fixtures currently set `builder`) | No: the register says it already (`TOTALA-EXE-INTERFACE.md:1315,1372,1498`). A one-click play-test on a factory next to a damaged unit would confirm. |
| 2 | An armed or default attack order a unit has no weapon slot to carry out must yield **no order** and leave the unit's queue alone, and slot 2 must count for the water test (Crusader and Enforcer against a submarine). Rule 3 | `src/rwe/game/DefaultAction.cpp` (attack arms), `GameScene_input.cpp:595-627`, `src/rwe/sim/UnitBehaviorService.cpp:3596-3680` (`weapons[0]` only), `GameSimulation_visibility.cpp` | **Yes.** Order a Crusader at a submerged enemy submarine in the original and see whether the ship closes and drops charges; the register's weapon notes do not say. |
| 3 | A sim-side capability gate in `refusalOf`: `canattack`, `canmove`, `canreclamate`, `cancapture`, `canload`, `candgun`, the builder's buildable list. Rule 10 | `src/rwe/game/PlayerCommandApplication.cpp:245-294`, `src/rwe/sim/UnitDefinition.h` for a buildable-list field, `LoadingScene_util.cpp` | No. Hashed: refusal changes what orders are applied; apply on every peer alike and note replay compatibility. |
| 4 | Resolve the right-click ladder conflict (commander right-click on an enemy: attack, capture or reclaim) and, if the chapter holds, the "attack resolution returns directly" half. Rule 8 | `src/rwe/game/DefaultAction.cpp:93-151` | **Yes, one click.** `Interface Type 1`, a commander, an enemy unit; also a ground-only unit against a submerged submarine. The cursor does not show the order; watch the unit. |
| 5 | Anti-air-only unit ordered at a ground point: no order. Rule 3 | `GameScene_input.cpp:617-621`, `UnitBehaviorService.cpp:3636-3642` | No (static, low impact). |
| 6 | Teleporter key and TELEPORT mission. Rule 9 | `src/rwe/io/fbi/io.cpp`, `LoadingScene_util.cpp`, `UnitBehaviorService.cpp` | Spec-room: what the original's handler does for `ARMGATE` and `CORGATE` (the only two shipped FBIs that set the key). |
| 7 | Capture of a teammate's unit from the armed button. Rule 6 | `GameScene_input.cpp:699` | Light: try it with the CAPTURE button on an ally. |

### Documented divergences this audit leaves alone

Own-units-only pickup (S:88); the lobby-team alliance (symmetric, not the
directional byte); one `RepairOrder` for both help-build and repair; the absence of
a unified order-type function.

### Conflicts with existing RWE findings, in one place

1. **The right-click default ladder (`0x43FA00`).** Register
   (`TOTALA-EXE-INTERFACE.md`): capture first, no attack arm, marked unsettled.
   Reference: an attack arm first, no capture arm. RWE: capture, reclaim, attack.
   Open until a play-test.
2. **Hover rule for the mode-3 sea test.** Register (`TOTALA-EXE-WEAPONS.md`
   section 12 area): a target property, half model height. Reference: "hover-actor
   test". Wording only.
3. **Kamikaze position in mode 3.** Register: ATTACK_KAMIKAZE at `0x43F38A`, taken
   first by RWE. Reference: after the weapon branch. No shipped unit is affected.
