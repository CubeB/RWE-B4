# New conflicts between the byte-tactics module chapters and the register

Hand-off list for the spec room, 2026-10-16. Reference: TA: Byte Tactics at
`97d7ac59b460417bcdfa629cf90da99f31818c65`, static reading, five module chapters
(`map-terrain`, `map-features`, `units-units`, `units-unit-targets`, `units-unit-motion`).
Neither side is chosen below; each line gives the subject, the two readings and where
each is written. The five audits are `BT-MAP-TERRAIN-PARITY.md`,
`BT-MAP-FEATURES-PARITY.md`, `BT-UNITS-UNITS-PARITY.md`, `BT-UNITS-UNIT-TARGETS-PARITY.md`
and `BT-UNITS-UNIT-MOTION-PARITY.md`. The spec-room checkout was not touched.

Kind: **register** means the other side is a `TOTALA-EXE*.md` finding; **design** means
it is an RWE design statement or code comment with no register finding behind it.

## Conflicts

1. **Ground alignment: where the four sample points come from** (register).
   Register: each corner is the definition's corner offset rotated to the heading,
   and RWE implements the footprint's half extents
   (`docs/TOTALA-EXE-MOVEMENT.md` near line 736; `src/rwe/sim/UnitBehaviorService.cpp:1884-1887`).
   Reference: four vertices of the unit model, from a designated row of the model
   (`units-unit-targets` section 8). Audit: `BT-UNITS-UNIT-TARGETS-PARITY.md` rule 8.

2. **Ground alignment: which flag holds a unit at sea level and adds the wave** (register).
   Register: a **floater** is held up to sea level with a wave term folded in
   (`docs/TOTALA-EXE-MOVEMENT.md` near line 739; the `0x43D72E` clamp is gated on
   `Floater`, `docs/TOTALA-EXE-WEAPONS.md` section 12). Reference: definition **bit 0x1000**,
   which the register names `canhover` (bit 12 of the flag word at `def+0x241`,
   `docs/TOTALA-EXE-MOVEMENT.md` section 95; `0x81000` is hover or floater in
   `docs/TOTALA-EXE-WRECKS.md`). RWE clamps for either and has no wave term
   (`UnitBehaviorService.cpp:1887,1898-1903`). Audit: `BT-UNITS-UNIT-TARGETS-PARITY.md` rule 8.

3. **Kill severity's second term, `unit+0xF7`** (register, an open question answered).
   Register: no known writer; "taken as zero" in RWE
   (`docs/TOTALA-EXE-WRECKS.md` section "Loose ends" and "What 60,075 recorded deaths say";
   `docs/TOTALA-EXE-WEAPONS.md` near lines 2226 and 2241;
   `src/rwe/sim/GameSimulation.cpp:3250-3254,3328-3335`). Reference: the unit's **previous
   health percentage** (`units-units` section 6). If so every RWE
   corpse level reads low. Audit: `BT-UNITS-UNITS-PARITY.md` rule 6, Gaps rank 1.

4. **Burning features: duration, spread and damage** (design).
   RWE: `ROADMAP.md` ("Burning features") and `src/rwe/sim/GameSimulation.cpp:573-686`:
   a `burnmin..burnmax` second duration, a repeating spread every `sparktime` seconds over a
   circle of the burn weapon's radius, rolled against the source's `spreadchance`, no wind,
   no damage. Reference (`map-features` section 9): the fire lasts as long as the burn
   animation; one spread at half to full `sparktime`; a 7 by 7 square plus five wind-directed
   samples, rolled against the candidate's own chance; the burn weapon's area damage is
   applied at the spread. Whether the executable reads `burnmin`/`burnmax` at all is unchecked.
   Audit: `BT-MAP-FEATURES-PARITY.md` rules 9, 10.

5. **A nanoframe's death explosion** (design).
   RWE: a code comment holds that in TA a unit not receiving build input "dies with an
   explosion, even though [it leaves] no corpse" (`GameSimulation.cpp:3621-3624`); RWE kills
   nanoframes quietly. Reference (`units-units` section 9): the death explosion fires for a
   positive script severity on a **completed** unit. No register finding. Audit:
   `BT-UNITS-UNITS-PARITY.md` rule 9.

6. **Wind generator `SetSpeed` argument** (design).
   RWE: the normalised fraction `min(1, speed/5000)` as 16.16
   (`UnitBehaviorService.cpp:197-209`, `GameSimulation.cpp:4312-4313`, `cob.cpp:26-29`).
   Reference (`units-unit-motion` section 2): the stored wind speed shifted left four bits.
   No register finding states the scale. Audit: `BT-UNITS-UNIT-MOTION-PARITY.md` rule 2.

## Re-confirmed, not new

7. **Extractor "plus one per existing footprint cell"** is already
   `docs/TA-SYSTEMS-PARITY.md` conflict 3; `units-unit-motion` section 1 states it a second
   time from the same source and adds that off-map cells contribute nothing.

## Register entries the chapters touch that RWE departs from (not reference conflicts)

These are listed so the spec room does not mistake them for chapter-versus-register
disagreements; each is an RWE departure from a register finding the chapter agrees with.

- **`MaxWaterSlope` default and per-cell wet or dry** (`docs/TOTALA-EXE-MOVEMENT.md` section
  95 and `TOTALA-EXE-DATA.md` section 30 say 255 and per cell; RWE defaults it to `MaxSlope` and
  chooses once per footprint, `src/rwe/io/moveinfotdf/io.cpp:14`,
  `src/rwe/sim/movement.cpp:41-70`).
- **Blast damage reaches only the origin cell of a multi-cell feature**
  (`docs/TOTALA-EXE-DATA.md` section 24 step 2 and the chapter agree; RWE damages through
  any covered cell, `GameSimulation.cpp:3788-3830`).
- **Feature reclaim pays in one lump** (`docs/TOTALA-EXE-ECONOMY.md`, "Reclaim and death
  metal", and the chapter agree; RWE pays per step, `GameSimulation.cpp:496-551`).
- **Ground height is an integer bilinear, -1 off the map**
  (`docs/TOTALA-EXE-DATA.md` near line 1412 and the chapter agree; RWE returns a
  four-triangle mesh height and 0, `src/rwe/sim/MapTerrain.cpp:253-277`).
- **Commander identity by side name** (`docs/TOTALA-EXE-WRECKS.md`, "The game-end wipe",
  and the chapter agree; RWE uses the FBI `Commander` key, `GameSimulation.cpp:4243,4359`).
- **The unit limit is per player** (the chapter's `units-units` section 1 answers half of
  `docs/TA-SYSTEMS-PARITY.md` rule 35; RWE has no cap).

## Batch 2 (2026-10-16)

Five more module chapters: `units-movement-class`, `units-unit-scripts`,
`units-unit-position`, `orders-order-dispatch` and `units-unit-types`. Same reference
commit and the same evidence class (static reading). The audits are
`BT-UNITS-MOVEMENT-CLASS-PARITY.md`, `BT-UNITS-UNIT-SCRIPTS-PARITY.md`,
`BT-UNITS-UNIT-POSITION-PARITY.md`, `BT-ORDERS-ORDER-DISPATCH-PARITY.md` and
`BT-UNITS-UNIT-TYPES-PARITY.md`. Neither side is chosen; kinds as above.

8. **The motion timestamp's reader, `mover+0x2A`** (register).
   Register (`docs/TOTALA-EXE-MOVEMENT.md` section 102): stamped when a unit changes
   map square or movement mode, "may be a stuck timer ... or dead". Reference
   (`units-movement-class`, `units-unit-scripts`): the occupancy refresh reads it,
   which is what keeps a recent mover from blocking a search for thirty ticks. The
   two are compatible if the reader is the refresh the register did not trace.
   Audits: `BT-UNITS-MOVEMENT-CLASS-PARITY.md` rule 7, `BT-UNITS-UNIT-SCRIPTS-PARITY.md`.

9. **Units in a pass map's search** (design).
   RWE treats every mobile unit as a wall in the search. Reference: units are not
   walls for thirty ticks after they last moved (an independent second reading,
   `docs/TOTALA-EXE-EXTERNAL.md`, agrees; not read by this project). No register
   finding states it. Audit: `BT-UNITS-MOVEMENT-CLASS-PARITY.md` rule 4 and its Gaps table.

10. **The sweet spot's world position** (register, a silence).
    Register (`docs/TOTALA-EXE-WEAPONS.md` section 126): the `SweetSpot` query is
    "turned into a world position by `0x43E0B0`" with no statement of how. RWE: the
    piece origin through the transform chain. Reference: the midpoint of the piece's
    vertex bounds, zero included, plus the unit position. Audit:
    `BT-UNITS-UNIT-SCRIPTS-PARITY.md` rule 10, Gaps rank 3.

11. **Kill severity's second term, `unit+0xF7`, answered more precisely than item 3**
    (register, an open question). Register: no known writer, taken as zero.
    Reference (`units-unit-position` section 5): the unit pass refreshes a pair of
    health-percentage bytes **every 30 ticks** (retain the old, calculate the new,
    clamped), so the term read at death is the health from 30 to 59 ticks earlier,
    not the health before the lethal blow. This narrows item 3's "previous health
    percentage". Another agent's uncommitted BT-43 change implements a 30-tick
    sample and was not audited. Audit: `BT-UNITS-UNIT-POSITION-PARITY.md` rule 5,
    Gaps rank 1.

12. **Which flag holds a unit at sea level (item 2) reduced to a wording question**
    (register). Both `units-unit-position` and `units-unit-types` put the height
    routine's hover test on the loader's `canhover` key (flag-word bit 12), which is
    the register's own assignment (`docs/TOTALA-EXE-MOVEMENT.md` section 95), and a
    `floater` is placed by a separate fixed branch. What remains of item 2 is whether
    the register's prose for the alignment routine (`docs/TOTALA-EXE-MOVEMENT.md` near
    line 739, "a floater") names `floater` where it tests `canhover`. Audit:
    `BT-UNITS-UNIT-POSITION-PARITY.md` rule 2; `BT-UNITS-UNIT-TYPES-PARITY.md` rule 3.

13. **The right-click default ladder at `0x43FA00`** (register, an open question).
    Register (`docs/TOTALA-EXE-INTERFACE.md`, "What is unsettled"): capture arm
    first and **no attack arm**, read from the capture arm onward; RWE keeps its own
    attack arm after reclaim. Reference (`orders-order-dispatch` section 8): an attack
    arm **first**, whose result is returned directly, and **no capture arm**; every
    arm from enemy reclaim onward agrees. Left-click chain: agrees in every arm.
    Commander (canattack, cancapture, canreclamate) right-clicking an enemy: reference
    attacks, register captures, RWE captures. Audit:
    `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 8, Gaps rank 4.

14. **Repair eligibility in the default ladders** (design; the register agrees with
    the reference). RWE's `DefaultAction.cpp` keys help-build and repair on `builder`
    (flags1 bit 6); the register (`docs/TOTALA-EXE-INTERFACE.md` lines 1315, 1372,
    1498; `docs/TOTALA-EXE-ECONOMY.md` line 262) and the reference both key them on
    the `canreclamate` copy in flags2 bit 9, which RWE's order buttons already use.
    27 shipped FBIs are `builder` without `canreclamate` (factories, mine layers,
    carriers, platforms). An RWE departure from the register that the chapter
    re-confirms. Audit: `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 4, Gaps rank 1.

15. **Manual attack eligibility looks at weapon slot 0 only** (design).
    RWE `attackTarget` tests `weapons[0]`; the reference's dispatcher tests the
    primary and an enabled secondary water weapon, and returns **no order** where RWE
    issues one and ends it. Shipped case: `ARMROY` and `CORROY` (gun in slot 1, depth
    charge in slot 2) against a submarine. No register finding on the dispatcher's
    water test. Audit: `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 3, Gaps rank 2.

16. **`floater` set from a water yard map** (design).
    RWE (`LoadingScene_util.cpp:650-653`) sets `floater` for any unit whose yard map
    holds a water letter. The reference's flag table (and the register's) take bit 19
    from the `floater` key alone. Shipped: 22 FBIs set the key (all ships), 24 have a
    water yard letter (sea-based buildings), none in both. Audit:
    `BT-UNITS-UNIT-TYPES-PARITY.md` rule 3, Gaps rank 1.

17. **Type-index sort key** (register, a detail).
    Register (`docs/TA-DEMOS.md`): byte sort of merged FBI **file** names, `_` after
    `Z`. Reference (`units-unit-types` section 2): case-insensitive sort of internal
    **unitnames** after removing unavailable entries. Identical on all 272 shipped
    FBIs; differs for names with `_` and for version-hidden units. Audit:
    `BT-UNITS-UNIT-TYPES-PARITY.md` rule 2.

### Re-confirmed, not new

- Steering, brake, bank, pitch and move-rate arithmetic agree with sections 1, 24 and
  102 (`BT-UNITS-UNIT-SCRIPTS-PARITY.md`).
- The movement class record, its defaults and the three clamps agree with section 95 and
  `TOTALA-EXE-DATA.md` section 30 (`BT-UNITS-MOVEMENT-CLASS-PARITY.md`).
- Every flag-word bit the register names agrees with the unit-types chapter's tables.
- The quantised construction byte `1 + trunc(254 * remaining)`, the rotating full-state
  slot at `tick % maxUnits` and the 512-byte soft cap agree with `TA-DEMOS.md`.
- Self-repair `healtime * 8 / 30` with the truncation before the conversion is
  `TOTALA-EXE.md` section 94 and RWE's `updateSelfRepair`.

### A naming note for earlier audits

`BT-UNITS-UNIT-TARGETS-PARITY.md` rule 4 says "activation bit 2" gates the damage
reduction. In `units-unit-position` bit 2 of the activation byte is **cloak**, and
armoured is bit 1 (mask 0x02), as the register has it; the earlier text means the bit
whose value is 2. RWE's code follows the register and is right.

## Batch 3 (2026-10-16)

Module chapters `orders-order-list`, `orders-order-queue`, `units-unit-script-calls`,
`units-unit-commands` and `game-players`. Same reference commit and the same evidence
class (static reading). The audits are `BT-ORDERS-ORDER-LIST-PARITY.md`,
`BT-ORDERS-ORDER-QUEUE-PARITY.md`, `BT-UNITS-UNIT-SCRIPT-CALLS-PARITY.md`,
`BT-UNITS-UNIT-COMMANDS-PARITY.md` and `BT-GAME-PLAYERS-PARITY.md`. Neither side is
chosen; kinds as above. Items are numbered from 18; if another Batch 3 author has
appended items, renumber on merge.

18. **Packed X/Z encoding in COB values** (register, a silence; RWE departs).
    RWE (`src/rwe/cob/cob_util.cpp:10-24`): the two halves are OR-ed and unpacked as two
    independent signed 16-bit values. Reference (`units-unit-script-calls` section 2):
    X times 65536 plus Z by **addition**, a negative Z borrows from the X word, and the
    vector and ground queries add the borrow back when the signed low word is negative.
    The register has only the engine-side drop pack (`docs/TOTALA-EXE-TRANSPORTS.md`
    line 250, non-negative operands, no conflict). The Nanolathe summary agrees with the
    reference (`docs/TOTALA-EXE-EXTERNAL.md` line 473; not read by this project). Shipped
    exposure: the four boom transports `ARMTHOVR`, `ARMTSHIP`, `CORTHOVR`, `CORTSHIP`,
    the only scripts that call `XZ_ATAN` or `XZ_HYPOT`. Audit:
    `BT-UNITS-UNIT-SCRIPT-CALLS-PARITY.md` rule 2, Gaps rank 2.

19. **`DROP_UNIT` onto a cell the passenger cannot occupy** (register versus code).
    Register (`docs/TOTALA-EXE-TRANSPORTS.md` section 37, lines 397-402 and 613) and
    reference: the unit stays attached. RWE (`PieceController.cpp:27-35`,
    `GameSimulation.cpp:914-1000`): the unit is placed on the nearest fitting footprint
    within twelve cells, tested with the movement class's placement rule and not the
    section 37 water-depth and slope rule. The reference agrees with the register; the
    mismatch is RWE's. Audit: `BT-UNITS-UNIT-SCRIPT-CALLS-PARITY.md` rule 8, Gaps rank 1.

20. **What an owner change keeps** (register silence; RWE in place).
    Register (`docs/TOTALA-EXE-WRECKS.md`, cause 4): the three weapon-slot bytes are
    copied to the new record and the old one dies with cause 4. Reference
    (`units-unit-commands` sections 3-4): health, construction, angles, stockpiles and the
    activation mask are copied; **standing move and fire fields are cleared; kill
    experience, orders, the id and the script stack are not copied.** RWE
    (`GameSimulation.cpp:1197-1275`) changes the owner in place, so kills and the previous
    owner's posture survive. Whether a human recipient's captured unit really stands on
    Hold Position and Hold Fire is the open question. Audit:
    `BT-UNITS-UNIT-COMMANDS-PARITY.md` rule 4, Gaps ranks 1 and 2.

21. **A mission unit naming an invalid player** (register versus reference).
    Register (`docs/TOTALA-EXE-DATA.md`, "Mission start"): a message is formatted for
    `0x4B6290` and the unit is made anyway. Reference (`units-unit-commands` section 2):
    fatal error. RWE skips the unit. Settled by whether `0x4B6290` logs or exits; no
    shipped mission reaches it (every mission unit names player 1 or 2). Audit:
    `BT-UNITS-UNIT-COMMANDS-PARITY.md` rule 3.

22. **Is there an end-of-game countdown outside a campaign?** (register scope).
    Register (`docs/TOTALA-EXE-DATA.md` section 113): the "set to 4, then five more
    seconds, never reset, economy frozen" word is written under "In a campaign game";
    skirmish is noted only as checking defeat first. Reference (`game-players` section 6):
    the non-campaign path and a multiplayer zero-combat-players fallback drive the same
    shared countdown. The Nanolathe summary says about 150 ticks
    (`docs/TOTALA-EXE-EXTERNAL.md` lines 284-296; not verified here). RWE decides a
    skirmish on the tick `computeWinStatus` answers
    (`src/rwe/game/GameScene_replay.cpp:817-837`). Audit: `BT-GAME-PLAYERS-PARITY.md`
    rule 6, Gaps rank 2.

23. **Commander-death modes** (register; RWE's numbering is its own).
    Register (`docs/TOTALA-EXE-WRECKS.md`; battleroom bits in `docs/TA-NETWORK.md`): three
    modes, Continues, Game Ends and Deathmatch (the respawn). Reference
    (`game-players` section 7) gives the respawn's placement and credit. RWE offers two
    stages in the GUI order (`GameEnds` = 0, `GameContinues` = 1,
    `src/rwe/game/GameParameters.h:124-130`), so neither the mode nor the respawn exists.
    Not a disagreement about the original; recorded because RWE's enum numbers are not the
    original's. Audit: `BT-GAME-PLAYERS-PARITY.md` rule 7, Gaps rank 1.

24. **`StartBuilding`'s argument list** (register silence; RWE passes two).
    RWE passes heading and pitch (`UnitBehaviorService.cpp:5247,6177,6332,6460`).
    Reference (`orders-order-queue` section 3): one unsigned heading. The register names
    the script and that it is queued (`docs/TOTALA-EXE-ECONOMY.md` line 1940) and is silent
    on arguments. A COB function reads its parameters as its first locals and the COB
    header carries no parameter count, so a census could not settle it. Audit:
    `BT-ORDERS-ORDER-QUEUE-PARITY.md` rule 3, Gaps rank 1.

25. **Secondary-list return codes** (register, a wording).
    Register (`docs/TOTALA-EXE-MOVEMENT.md` section 132 stage 7): each secondary mission
    runs "through the same wake test and return-code table". Reference
    (`orders-order-list` section 7): same wake test, different table (6 and 7 delete the
    node and return; an unrecognised result deletes the node) and no dirty-bit merge.
    Also callback result 4 (keep), which the register's table omits. The one shipped
    secondary order type is unaffected. Audit: `BT-ORDERS-ORDER-LIST-PARITY.md` rules 6-7.

26. **Whether an auto-engage attack node is "automatic" (flag 0x4000)** (open).
    Reference (`orders-order-list` sections 3-4): `AddOrder` strips the leading run of
    nodes marked 0x4000 when a new primary order arrives, and the default-mission node is
    so marked. The register records the sighting routine's attack and return-move nodes
    (`docs/TOTALA-EXE-MOVEMENT.md` section 86) without saying whether they carry the mark.
    RWE keeps them as ordinary entries, so a Shift-queued order waits behind them.
    Audit: `BT-ORDERS-ORDER-LIST-PARITY.md` rule 2, Gaps rank 2.

27. **Idle aircraft on Return Fire** (register versus code).
    Register (`docs/TOTALA-EXE-MOVEMENT.md` lines 243 and 287, `0x43B700`): the sight-range
    search runs only at fire mode 2. RWE's patrol and mine polls use that test
    (`UnitBehaviorService.cpp:4925,4737`) but the idle-aircraft hunt uses "not Hold Fire"
    (`:412-413`), so a Return Fire aircraft hunts. Audit:
    `BT-ORDERS-ORDER-LIST-PARITY.md` rule 8, Gaps rank 1.

28. **Four footprint samples in the unit-visibility predicate** (register agrees; RWE
    departs by its own comment). Register (`docs/TOTALA-EXE-VISION.md`) and reference
    (`game-players` section 10): four corners in a fixed order. RWE asks the fog grid once
    at the unit's position (`GameSimulation_visibility.cpp:256-258`). Listed so it is not
    mistaken for a register disagreement. Audit: `BT-GAME-PLAYERS-PARITY.md` rule 9.

29. **The `ALL` category** (design).
    Reference (`units-unit-commands` section 6): every definition's bit is always set in
    `ALL`. RWE tests the FBI's own `Category` text, and **none of the 272 shipped FBIs
    lists `ALL`**, so `weight ALL n` or `limit ALL n` in a classic profile matches nothing
    (`src/rwe/ai/classic/ClassicAiScript.cpp:296-358`). No register finding. Audit:
    `BT-UNITS-UNIT-COMMANDS-PARITY.md` rule 8, Gaps rank 3.

### Re-confirmed, not new (Batch 3)

- The service loop's return codes 0, 1, 2, 3, 5, 6, 7, 8, 9 and the same-tick re-run agree
  with `docs/TOTALA-EXE-MISSIONS.md` section 2 and `docs/TOTALA-EXE-ECONOMY.md` section 110.
- The queue toggle (sixteen units on each axis, first match, primary list only), the
  tail-merge on add and the remove-from-the-back with carry agree with
  `docs/TOTALA-EXE-INTERFACE.md` sections 136 and 137.
- The reclaim bite, `(kills + 5) / 5`, the cost floored at ten and the factor 300, and the
  sixteen-tick cadence, agree with `docs/TOTALA-EXE-ECONOMY.md`; RWE multiplies in
  `double` where the original's 32-bit chain wraps (not worth reproducing).
- `GET` ids 1 to 20 and the SFX ids, thrust parameters 6 and 7, wake amounts 16 and 8 and
  the bubble emitter agree with `docs/TOTALA-EXE-WEAPONS.md`, `-MOVEMENT.md` and `-RENDER.md`.
- The two transfer functions' reversed names, the absent alliance test and the difficulty
  scaling agree with `docs/TOTALA-EXE-COMMANDS.md` section 124 and RWE's `ResourceSharing`.
- The `InitialMission` clause table, the held-unit rule (sticky marker for `p`, point `a`,
  `d`, `s`; `o`, `i`, `bw` hold nothing) and the percentage health agree with
  `docs/TOTALA-EXE-DATA.md` sections 105 and 114 and RWE's loader.

## Batch 4 (2026-10-16)

Module chapters `orders-unit-orders`, `orders-vtol-orders`, `ai-pathfind` and
`orders-order-targets`. Same reference commit and the same evidence class (static reading).
The audits are `BT-ORDERS-UNIT-ORDERS-PARITY.md`, `BT-ORDERS-VTOL-ORDERS-PARITY.md`,
`BT-AI-PATHFIND-PARITY.md` and `BT-ORDERS-ORDER-TARGETS-PARITY.md`. Neither side is chosen;
kinds as above. Items are numbered from 30; if another Batch 4 author has appended items,
renumber on merge.

30. **`Guard_NoMove` searches or does not** (register versus reference).
    Register (`docs/TOTALA-EXE-WEAPONS.md`, the `DefaultMissionType` table;
    `docs/TOTALA-EXE-MOVEMENT.md` section 138, `0x4021F0`): frees the weapons, sleeps 30 ticks
    and never polls. Reference (`orders-unit-orders` section 5): adopts slot 0's target under
    runtime bit 0x10000000, keeps a 3-to-5-pass persistence count, restarts the target cycle
    by an 80 percent draw and recovers to a random visible enemy within 640 units of the
    remembered position. RWE follows the register. Shipped exposure: the 26 FBIs that name
    `GUARD_NOMOVE`, all armed. Audit: `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 4, Gaps rank 5.

31. **The ground reclaim reach "plus target radius"** (register versus reference).
    Register (comment at `UnitBehaviorService.cpp` `reclaimTarget`, from the decode): the extra
    term is read at one instruction and written nowhere, so it contributes nothing. Reference
    (`orders-unit-orders` section 14, `orders-vtol-orders` section 15): the ground live-unit
    handler adds the target radius to the build range and the air handler does not. Compatible
    if the field is never written; a conflict if it is a real radius. Audit:
    `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 13, `BT-ORDERS-VTOL-ORDERS-PARITY.md` rule 18.

32. **Resurrection's last state** (register, a silence).
    Register (`docs/TOTALA-EXE-ECONOMY.md` section 98) stops at the completed unit at one hit
    point and says it has to be repaired afterwards. Reference (`orders-unit-orders`
    section 16): a later state announces and dispatches mode 8 against the new unit,
    prepending repair or service when available. RWE leaves the unit at one hit point and the
    resurrector idle. Audit: `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 14, Gaps rank 3.

33. **Which shipped FBI sets `CanResurrect`** (register versus census).
    Register (`docs/TOTALA-EXE-ECONOMY.md` section 98, reason 1): no FBI sets it. The FBI census
    taken for these audits has `CORNECRO` setting it. Listed so the next author re-reads the
    key before relying on either; the census is a count of keys in files and was not run.

34. **The ring destination's two conventions** (reference-internal; design).
    Reference (`orders-order-targets` section 3): containment is "at most the first radius
    squared and at least the second", the heuristic treats the first as inner and the second as
    outer, and an argument order with the first smaller gives an empty containment interval.
    No register finding; RWE has no ring goal. Recorded because the staged chase search that
    uses ring pairs cannot be ported until the caller's argument order is read. Audit:
    `BT-ORDERS-ORDER-TARGETS-PARITY.md` rule 3, Gaps rank 2.

35. **Final-goal arrival: map square versus eight world units** (register agrees with the
    reference; RWE departs and the register does not list it).
    Register (`docs/TOTALA-EXE-MOVEMENT.md` section 102, "Arrival at the goal") and reference
    (`orders-order-targets` section 2): arrival is the unit's map square within
    `(radius / 16)^2` squares, and a plain Move passes radius plus four, which is zero squares.
    RWE (`UnitBehaviorService_util.cpp` `hasReachedGoal`): flat distance under eight world
    units (aircraft: the larger of eight and four times their top speed). Section 88 lists the
    waypoint advance radius and, as far as it was searched, not this. Audit:
    `BT-ORDERS-ORDER-TARGETS-PARITY.md` rule 2, Gaps rank 1.

36. **Mobile builder with an unreachable site** (reference versus code; design).
    Reference (`orders-unit-orders` section 9): a path failure is tolerated if the distance minus
    both footprint half-diagonals is within the build range. RWE (`createNewUnit`): fails with
    "Target area was blocked" whenever the navigator reports the site unreachable. Audit:
    `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 8, Gaps rank 2.

37. **A guard on an allied unit** (register versus code).
    Register (`docs/TOTALA-EXE-INTERFACE.md` lines 1322 and 1355): the original offers and keeps
    the guard on an allied unit. RWE's default-action ladder offers it (`DefaultAction.cpp`,
    `allied && canGuard`) and `handleGuardOrder` ends the order at once unless the guarded unit
    has the same owner ("TODO: real allied check here"). 150 shipped FBIs carry `canguard`.
    Audit: `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 17, Gaps rank 1.

38. **Stop and the "attack ended" circuit** (probable code fault, read not run).
    Reference (`orders-unit-orders` section 2): StopOrder prepends a landing for a flying unit
    in flight mode 2. RWE's Stop interrupts the task and clears the order list, but
    `UnitState::airLoiter` is not cleared, so the search circuit continues. Audit:
    `BT-ORDERS-UNIT-ORDERS-PARITY.md` rule 2, `BT-ORDERS-VTOL-ORDERS-PARITY.md` Gaps rank 4.

### Re-confirmed, not new (Batch 4)

- The aircraft numbers the chapter gives (3840 pad radius, the 75 percent gate, the bomber's 480,
  2240, 960, 1440 and 128, the release trigger `1 + attackrunlength + trunc(sqrt(2h/g) x 30 x speed)`,
  the 2R/3 hover ring, the patrol's 320 and 336, the `0x5555`/`0x4000`/`0x2000` circuit steps,
  the 8-to-39 idle hop and the 150-tick work-orbit shift) agree with
  `docs/TOTALA-EXE-MISSIONS.md` and `docs/TOTALA-EXE-MOVEMENT.md`.
- The pathfinder's 1333 allowance, the 1.5 base scale and its 9.0/4.5/1.5 re-evaluation every
  150 calls, the 16/22 step costs, the turn table, the 75 bend and 30 grade-one costs, the 64,
  20 and 3 waypoint horizons, the five-unit waypoint advance and the 60-tick claim throttle agree
  with `docs/TOTALA-EXE-MOVEMENT.md` sections 87 and 102.
- Capture's duration chain, the reclaim pulse, factory build cancellation credit, the blocked-site
  retry of 15 and 30, the stockpile cost, the repair patrol's 20 percent test and the self-destruct
  countdown agree with `docs/TOTALA-EXE-ECONOMY.md` sections 94 to 98, 110 to 112, 131 and 134.
- Pickup and unload's three attempts, the 1.5 hover tolerance and "any carrier is success" agree
  with `docs/TOTALA-EXE-TRANSPORTS.md` sections 34, 35 and 40; the teleport mission is the resolved
  row BT-69 and the factory allocation wait of 300 is the open row BT-18.

## Batch 5 (2026-10-16)

Module chapters `ingame-control-panel` and `ingame-build-placement`. Same reference commit and
the same evidence class (static reading). The audits are `BT-INGAME-CONTROL-PANEL-PARITY.md`
and `BT-INGAME-BUILD-PLACEMENT-PARITY.md`. Neither side is chosen; kinds as above. Items are
numbered from 39; if another Batch 5 author has appended items, renumber on merge.

39. **Right button in Interface Type 0: drag-scroll always, or only with a modifier** (register
    versus reference).
    Register (`docs/TOTALA-EXE-INTERFACE.md` lines 1209 and 1544, `0x499100`): an armed command
    is cancelled, otherwise Interface Type 0 starts the screen drag-scroll. Reference
    (`ingame-build-placement` section 3): the scroll starts only when input bit 2 and a key flag
    (8, unidentified; the register's `0xF9` is Shift) are both present; without the modifier
    the same click clears the selection and pops the layout. RWE clears the selection and has
    no right-drag (the pan is on the middle button, `GameScene_input.cpp`). Audit:
    `BT-INGAME-BUILD-PLACEMENT-PARITY.md` rule 4, Gaps rank 2.

40. **The click-or-drag threshold's clock and unit** (register versus reference, and RWE
    against both).
    Register (`docs/TOTALA-EXE-INTERFACE.md`, "The cursor is the decision", `0x4995AE`): a drag
    of under 32 pixels in under `0x19` ticks is a click. Reference (`ingame-build-placement`
    section 4): within 25 clock units of wall-clock `GetTicks` and under 32 **world units** on
    both X and Z. Same number, different unit on both axes. RWE: 30 simulation ticks and 32
    viewport pixels (`GameScene_input.cpp`, `onMouseUp`). Audit:
    `BT-INGAME-BUILD-PLACEMENT-PARITY.md` rule 7, Gaps rank 3.

41. **Mission-excluded build entries: removed or greyed** (register port note versus
    reference).
    Register (`docs/TOTALA-EXE-DATA.md`, the paragraph after the mission unit list, issue
    381): "their buttons come off every build menu". Reference (`ingame-control-panel` section
    5): entries whose definition cannot be resolved are **greyed** in place. RWE erases the
    gadget (`GameSimulationLoader.cpp`, about 1388-1401). A presentation choice, not a decoded
    routine. Audit: `BT-INGAME-CONTROL-PANEL-PARITY.md` rule 8, Gaps rank 7.

42. **What `,`, `.`, NEXT and PREV do at the ends of the page list** (reference-internal;
    register unsettled).
    Register (`docs/TOTALA-EXE-KEYBOARD.md`): `,` calls `0x41BF10` and `.` calls `0x41BDE0`,
    each with argument 1. Reference (`ingame-control-panel` section 6): the forward and backward
    steps have a toggle mode (close at the last page, open page 1 from closed) and a non-toggle
    mode (wrap and stay open); which a caller uses is not stated. RWE always wraps.
    Audit: `BT-INGAME-CONTROL-PANEL-PARITY.md` rule 7, Gaps rank 1.

43. **BLAST greyed or hidden when nothing in the selection can load** (register against RWE;
    the reference agrees with the register).
    Register (`docs/TOTALA-EXE-INTERFACE.md` section 19, `0x41A409`): with no load capability
    LOAD is made inactive, UNLOAD is greyed and BLAST is greyed unless `candgun`. Reference
    (`ingame-control-panel` section 4) says the same. RWE removes the BLAST gadget whenever the
    selection does not offer it (`applyOrderButtonGating`, `doomed`), leaving an empty slot where
    a dimmed face is expected. Audit: `BT-INGAME-CONTROL-PANEL-PARITY.md` rule 4, Gaps rank 3.

44. **The placement-mode test: the product's building code or the builder's mobility**
    (reference versus code; design).
    Reference (`ingame-control-panel` section 5): a product whose definition field `+0x22F`
    (`bmcode`) is zero selects placement mode 14; the count helper keys on the producer's motion
    pointer. RWE keys the panel click on the selected builder's `isMobile`
    (`GameScene_commands.cpp`, `onMessage`). Equal on the retail catalogue as far as known (not
    counted). Audit: `BT-INGAME-CONTROL-PANEL-PARITY.md` rule 6, Gaps rank 8.

45. **An armed Capture cursor and an allied unit** (resolved row against code).
    Spec-room row BT-72 (resolved): the armed button's order-time rule is player identity, so an
    ally may be captured, and "RWE matches". The armed Capture arm in RWE
    (`GameScene_input.cpp`, `CaptureCursorMode`) requires `isEnemy`, which is "not allied", so
    it refuses an ally. The row's clause was true of the default-action path. Audit:
    `BT-INGAME-BUILD-PLACEMENT-PARITY.md` rule 5, Gaps rank 4.

### Probable code faults found on the way (not reference conflicts)

- **`BUILD_PERCENT_LEFT` with a zero `BuildTime`.** `UnitState::getBuildPercentLeft` divides by
  the definition's build time with no guard; it is reached from the COB getters and the AI's
  frame logging. A mod unit with `BuildTime=0` would trap every peer the first time a script asks.
  The original has the same unguarded divisor (`ingame-control-panel` section 7); RWE's own
  rule (`docs/SECURITY-AUDIT.md`) is that content faults cost the content only. Audit:
  `BT-INGAME-CONTROL-PANEL-PARITY.md` rule 9, Gaps rank 2.

### Re-confirmed, not new (Batch 5)

- The build-rate chain (integer `workerTime / 30`, fraction actually applied scaling both
  charges, truncated health differenced across the step), the unfinished-build decay at one energy
  point per tick with its first loss at tick 330, the metal refund on decay and cancellation with
  the 0.5 and 0.7 computer-player factors and no energy refund, and the silent kill of cause 9
  agree with `docs/TOTALA-EXE-ECONOMY.md` sections 23, 93 and 134.
- The repair tick (`0x41BD10`: both clamps upper, one hit point and one energy a tick, energy-only
  request, kind 10) agrees with `docs/TOTALA-EXE-ECONOMY.md` section 94.
- The ten-bit OR of the order-button capabilities, the sentinel accumulators, the cloak
  accumulator's disagreement bug, the repair copy of `canreclamate`, and the four toggle handlers
  agree with `docs/TOTALA-EXE-INTERFACE.md` section 19.
- The left-click dispatcher ("the cursor is the decision"), right-click cancellation without a
  Stop, Shift as the queue and retain key (`0xF9`), the screen shake's accumulate-and-average
  rule and no distance falloff, and the camera follow's halving with a cap of 320 agree with
  `docs/TOTALA-EXE-INTERFACE.md` section 103, `docs/TOTALA-EXE-KEYBOARD.md` and
  `docs/TOTALA-EXE-WEAPONS.md`.

## Batch 6 (2026-10-16)

Module chapters `weapons-weapons`, `weapons-weapon-types`, `weapons-explosions`,
`map-line-of-sight` (with `map-los-tables`) and `ai-ai-player` (with `ai-ai-profile`). Same
reference commit and the same evidence class (static reading). The audits are
`BT-WEAPONS-WEAPONS-PARITY.md`, `BT-WEAPONS-WEAPON-TYPES-PARITY.md`,
`BT-WEAPONS-EXPLOSIONS-PARITY.md`, `BT-MAP-LINE-OF-SIGHT-PARITY.md` and
`BT-AI-AI-PLAYER-PARITY.md`. Neither side is chosen; kinds as above. Items are numbered from 46;
if another Batch 6 author has appended items, renumber on merge.

46. **The launch muzzle correction** (register versus code versus reference).
    Register (`docs/TOTALA-EXE-WEAPONS.md`, section 12): the term is left out of RWE. Code: a
    `ballisticZOffset` exists and shortens the solver's distance. Reference (`weapons-weapons`)
    and `docs/TOTALA-EXE-EXTERNAL.md`: `trunc(1.25 * (Query z - AimFrom z))` applied to the launch
    vertical velocity. Kind: register, with a code contradiction inside the repository. Audit:
    `BT-WEAPONS-WEAPONS-PARITY.md` rule 5, conflict 1.

47. **Fire script per burst or per projectile** (register against itself).
    Register (`docs/TOTALA-EXE-WEAPONS.md`, section 14): the Fire script starts once per
    projectile. The register's own template description, and the reference, start it at the
    template's spawn, once per burst; RWE already does once per burst. Kind: register. Audit:
    `BT-WEAPONS-WEAPONS-PARITY.md` rule 7, conflict 2.

48. **The cruise near branch** (register versus reference).
    Register: the live target unit is followed and the floor applies only with no live unit.
    Reference: a cached destination with no live tracking and a floor at the larger of the ground
    and the sea. Kind: register. Audit: `BT-WEAPONS-WEAPONS-PARITY.md` rule 9, conflict 3.

49. **A small blast with no direct target** (register versus reference).
    Register: any `areaofeffect <= 16` takes the single-target path at full damage. Reference: with
    no direct target the blast falls to the area routine with falloff. Kind: register. Audit:
    `BT-WEAPONS-WEAPONS-PARITY.md` rule 11, conflict 4.

50. **A stale comment about bombs** (code against data, not a reference conflict).
    `parseWeaponDefinition` says shipped bombs set both `ballistic` and `dropped`; the census of the
    184 shipped weapons has them setting `dropped` alone. Kind: design. Audit:
    `BT-WEAPONS-WEAPONS-PARITY.md` conflict 5 and `BT-WEAPONS-WEAPON-TYPES-PARITY.md` conflict 2.

51. **The interceptor's radius** (register wording, ambiguous).
    Register: "within `areaofeffect`", which reads either as the TDF value or as the halved blast
    radius. Reference: the full TDF value (96 on all four shipped interceptors, so RWE's 48 is
    half). Kind: register. Audit: `BT-WEAPONS-WEAPONS-PARITY.md` rule 12, conflict 6.

52. **What triggers the explosion smoke** (register port note versus reference).
    Register (`docs/TOTALA-EXE.md`, section 91 entry for #113): an impact that smokes gets three
    puffs; the port keys that on the `endsmoke` weapon flag, which one shipped weapon sets. Reference
    (`weapons-explosions`): every ordinary explosion effect above sea level requests the smoke.
    Kind: register. Audit: `BT-WEAPONS-EXPLOSIONS-PARITY.md` rule 3, conflict 1.

53. **Normal-directed shard impulses** (register versus reference).
    Register: shards scatter only. Reference: scatter plus a random impulse along the face normal's X
    and Z, on an extruded box rather than RWE's flat quad. Kind: register. Audit:
    `BT-WEAPONS-EXPLOSIONS-PARITY.md` rule 6, conflict 2.

54. **Where the vision cell sits** (code and design comment against register and reference).
    Code (`GameSimulation.h`, `visionCellAt`): the terrain height under the position, "exactly as TA
    indexes it". Register (`docs/TOTALA-EXE-VISION.md`) and reference (`map-line-of-sight`): the
    vision origin uses the eye elevation (the unit's Y plus the model top) and a query uses the
    sample's Y, so an aircraft's sight is centred on its drawn position, not under its shadow. Kind:
    register and design. Audit: `BT-MAP-LINE-OF-SIGHT-PARITY.md` rule 2, Gaps rank 1.

55. **Stale lines in `docs/TOTALA-EXE.md` section 91** (register against the code, not a reference
    conflict). It still says Permanent line-of-sight "has not been looked at", that Circular is
    "understood but not implemented", and that the explored grid is per-player. RWE has
    `LineOfSightMode::Permanent`, `revealCircle`, and one shared explored grid with a bit per
    line-of-sight group. Kind: register. Audit: `BT-MAP-LINE-OF-SIGHT-PARITY.md` conflict 2.

56. **The section 88 reason for the floor of 5 on the circular radius** (design statement against
    data). Section 88 justifies dropping the floor by a blind unit; no shipped unit has a sight
    radius below 32, and the real consequence is 47 shipped units with a sight of 32 to 159 that
    take a smaller disc than the original's `vismasks.gaf` frame (whose ten frames are supersets of
    RWE's exact discs, 8 to 48 more cells each). Kind: design. Audit:
    `BT-MAP-LINE-OF-SIGHT-PARITY.md` rule 5, conflict 3.

57. **Where the mobile builder's range limit applies** (register wording versus reference).
    Register (`docs/TOTALA-EXE-COMPUTER-PLAYER.md`, section 121): a `cancapture` builder "is also
    kept within `(mapWidth + mapHeight) / 3` of the base". Reference (`ai-ai-player` section 7): the
    chosen build site is rejected beyond that range. RWE orders the builder home. Kind: register.
    Audit: `BT-AI-AI-PLAYER-PARITY.md` rule 9, Gaps rank 3.

58. **The extractor search radius** (register silence versus reference).
    Register (section 121): the cached resource cells are walked nearest first, with no range.
    Reference: the cell search is at four times the shared radius (640 units on a first search).
    RWE has no bound. Kind: register. Audit: `BT-AI-AI-PLAYER-PARITY.md` rule 7, Gaps rank 2.

59. **The low-wind test** (register silence versus reference, and the code's guess).
    Register: "the code's low-wind test". Reference: the map's maximum wind under half the
    denominator (2500, the denominator being the economy's 0x1388). RWE (marked `[open]`): the
    mean under 1250. Nine of 271 shipped maps disagree. Kind: design. Audit:
    `BT-AI-AI-PLAYER-PARITY.md` rule 5, Gaps rank 6.

60. **The `MinWaterDepth` default in the classic engine** (resolved row against code).
    Spec-room row BT-8 (resolved): an absent key is the signed value -10000. The classic engine
    ports the comparison `>= 0` on an unsigned field that defaults to 0, so it is always true and
    every type takes the x3 rating and the second placement lattice. Kind: register against code.
    Audit: `BT-AI-AI-PLAYER-PARITY.md` rule 5, Gaps rank 1.

### Probable code faults found on the way (not reference conflicts)

- **A missing argument on a `limit` or `weight` line.** The classic engine ignores the line; the
  original defaults the number to zero. One shipped line has that shape (`Limit ARMSABO`, in the
  `missions` profile). Audit: `BT-AI-AI-PLAYER-PARITY.md` rule 14, Gaps rank 4.
- **The area-damage dedupe lists.** Past 20 units (or 64 features) in one blast, RWE damages the
  overflow once per covered cell; the original stops at the cap. The register notes the cap but not
  the consequence. Audit: `BT-WEAPONS-WEAPONS-PARITY.md` rule 11, Gaps rank 2.
- **The OTA `gravity` key is parsed and ignored.** 83 of 272 shipped maps (31%) carry a value other
  than 112. Audit: `BT-WEAPONS-WEAPONS-PARITY.md` rule 6, Gaps rank 1.

### Re-confirmed, not new (Batch 6)

- The weapon-type loader's numeric tables, offsets and flag positions agree with
  `docs/TOTALA-EXE-DATA.md` (the weapon table), and the 184 shipped weapon names have unique ids
  1 to 227.
- The classic AI's timer cadences, the 30-tick refresh with its 1-in-30 rescore, the three ratings
  and the final-rating arithmetic, the weighted draw with no re-draw on a side mismatch, the assault
  rebalance and its stale-subtraction quirk, the rally order 5, 1, 4, the aircraft routes, the
  empty spatial probe and the retaliation postponement of `tick + 30 + rand(300)` agree with
  `docs/TOTALA-EXE-COMPUTER-PLAYER.md` sections 119 to 123.
- The line-of-sight quadrant replication (the register's rotations, the reference's reflections)
  yields the identical ray set (measured on tables 3, 5 and 8), and the `los.tdf` file defines
  twelve tables but says `numtables=9`.

## Batch 7 (2026-10-17)

Module chapters `util-math`, `util-tdf`, `game-settings`, `sound-sound` and `sound-sound-types`.
Same reference commit and the same evidence class (static reading). The audits are
`BT-UTIL-MATH-PARITY.md`, `BT-UTIL-TDF-PARITY.md`, `BT-GAME-SETTINGS-PARITY.md` and
`BT-SOUND-PARITY.md` (the two sound chapters together). Neither side is chosen; kinds as above.
Items are numbered from 61; if another Batch 7 author has appended items, renumber on merge.

61. **The unit-message table: which column is the queue priority** (register versus reference).
    Register (`docs/TOTALA-EXE-ECONOMY.md`, "`underattack`, `repair` and `cant`"): entries are
    kept in priority order from the table's third dword, with `underattack` 0x14, `cant` 1,
    `select` 0. Reference (`sound-sound` section 8): the third column is the **repeat delay in
    seconds** (20, 1, 0) and the priority is the second (`select` 10, `underattack` 9, `cant` 8);
    the next-allowed time is set only when audio is played. The register's three example
    values are exactly the reference's delay column. The two orderings and the existence of a
    twenty-second "Under Attack" cooldown all turn on this. Kind: register. Audit:
    `BT-SOUND-PARITY.md` rule 6.

62. **The use-only clearing loop: where it starts** (register wording versus reference).
    Register (`docs/TOTALA-EXE-DATA.md`, near 1046): the availability bit is cleared on "every
    definition" (`0x4317AA` to `0x4317D8`) and the array is then compacted. Reference
    (`game-settings` section 10): the clearing loop runs from index one, so definition zero is
    never cleared (and never compacted away). RWE excludes every unlisted unit. Whether any
    shipped mission's list omits whichever unit is stored first is not known. Kind: register
    (unspecified). Audit: `BT-GAME-SETTINGS-PARITY.md` rule 10.

63. **The TDF grammar against a retail file** (reference versus shipped data, and a code
    comment). Reference (`util-tdf` section 5): after a closing brace a stray semicolon starts a
    field whose key runs to the next equals sign, and a missing equals sign ends the process. A
    model of that grammar rejects exactly one shipped file, `gamedata/unitview.tdf` in
    `totala1.hpi` (a `[RESOURCES]` block closed with `};`), which the retail game evidently
    survives: either the file is never read through that parser or the chapter omits a rule. The
    code comment in `src/rwe/io/tdf/TdfParser.h` (about 158) says `Translate.tdf` also closes its
    last block that way; its single `};` is in fact inside a value (`French = ~ (touche};`).
    Kind: design (the comment) and reference against data. Audit: `BT-UTIL-TDF-PARITY.md` rule 4.

64. **A comment inside a token: blanked or removed** (design versus reference). Reference
    (`util-tdf` section 4): a separate pass replaces each comment with blanks, so a comment
    between two halves of a value keeps a gap and a number such as `12 /* c */ 34` reads 12. RWE
    removes the comment inline and reads 1234. No shipped file differs. Kind: design. Audit:
    `BT-UTIL-TDF-PARITY.md` rule 3.

65. **Positional sound is gated by map knowledge and attenuated** (design versus reference).
    Reference (`sound-sound` sections 5, 6): a sound at a position is dropped unless that cell is
    explored (mapped games) or currently visible; without 3D a sound outside the scroll rectangle
    plays 10 dB quieter. RWE (`GameScene_commands.cpp` about 1957, `GameScene_audio.cpp` about
    559) states the impact sound is deliberately "the whole map's noise" and plays everything at
    one level. The register is silent. Kind: design. Audit: `BT-SOUND-PARITY.md` rule 5.

66. **Which spelling wins when a key repeats in different case** (open, from the reference's
    own sentence). Reference (`util-tdf` section 6): differently cased copies of a key coexist
    and "later lookup returns its first member". Twelve shipped aircraft FBIs (`armcsa`, `armfig`,
    `armlance`, `armseap`, `armsehak`, `armsfig`, `corcsa`, `corhunt`, `corseap`, `corsfig`,
    `cortitan`, `corveng`) write `MaxWaterDepth=0;` then `maxwaterdepth=255;`. RWE reads the last
    (255). The reference does not say whether its first member is the newest or the oldest. The
    value is dead for an aircraft. Kind: register (BT-31, open). Audit: `BT-UTIL-TDF-PARITY.md`
    rule 5.

### Probable code faults found on the way (not reference conflicts)

- **Unit Sounds Off does not silence selection and order acknowledgement.** The two paths call
  `getSound` and `playUiSound` directly (`GameScene_commands.cpp` about 312, 346, 2812, 2846)
  and never consult the setting. Audit: `BT-SOUND-PARITY.md` rule 7, Gaps rank 1.
- **The sound category map is case-sensitive.** `corvoyr.fbi` writes `COR_Kbot` against the
  category `COR_KBOT`, so that unit is silent in RWE. Audit: `BT-SOUND-PARITY.md` rule 4, Gaps
  rank 5.
- **Retail unit definitions name categories that do not exist** (`CORE_MEX` on `cormex`,
  `CORE_KBOT` on three units, `COR_TANK` on `corsent`, `NONE` on six walls and teeth). RWE gives
  them silence; the original may fall to category zero (`ARM_KBOT`). Not decided by any source.
  Audit: `BT-SOUND-PARITY.md` rule 4, Gaps rank 4.
- **"Under Attack" prints on every hit** on an off-screen unit, with no cooldown or queue
  (`UnitDamagedEvent` is raised per hit, `GameSimulation.cpp` about 3609; the handler prints at
  `GameScene_commands.cpp` about 1719). Audit: `BT-SOUND-PARITY.md` rule 6, Gaps rank 6.
- **The wind speed draw includes the maximum.** The register draws below `max - min`
  (`docs/TOTALA-EXE-WEAPONS.md` near 494); RWE draws inclusive (`GameSimulation.cpp` about 4383).
  Audit: `BT-UTIL-MATH-PARITY.md` rule 4, Gaps rank 3.
- **The COB `RANDOM` instruction draws an inclusive range** (`cob.cpp` about 291); no source
  gives the original's convention. Audit: `BT-UTIL-MATH-PARITY.md` rule 4, Gaps rank 1.
- **The options pages disable the `LEFTCLICK`, `UNITCHAT`, `TXTSCROL` and `MAXLINES` gadgets**
  (`GameScene_menu.cpp` about 743) and `writeGameOptions` never writes `interface-mode`, so the
  interface-style switch can be set only from the command line or a debug checkbox. Audit:
  `BT-GAME-SETTINGS-PARITY.md` rule 3, Gaps rank 2.

### Re-confirmed, not new (Batch 7)

- The four-byte checksum: the chapter's four accumulators and the register's section 117
  packing are the same function (the chapter's add-the-byte is the register's low byte, and so
  on up to the top byte), and `taChecksum` implements it.
- The random generator (multiplier 16807, modulus 2^31 - 1, the seed rule with `0x66e29572`, no
  advance below a range of two), the 512-entry sine table with its quarter-step bias and
  rounding, and the heading function's operand order agree with `docs/TOTALA-EXE-DATA.md` section
  8, `-MOVEMENT.md` near 1749 and `-MISSIONS.md` near 24. RWE's `std::minstd_rand` (multiplier
  48271) and exact sine are representation differences, the latter already recorded in
  `-WEAPONS.md` near 1438.
- The record checksum range (body from after the opening brace to two bytes before the closing
  brace) is `-DATA.md` section 117.
- The ten-value `track0` to `track9` writer: the chapter names no caller, the register says it has
  none (`-MUSIC.md` section 43); consistent.
- The message table's 23 kinds, keys and default captions are the register's slot table in
  `-ECONOMY.md`. The reference supplies the length of the sound packet `0x13`: **18 bytes** with
  its type byte, which `docs/TA-DEMOS.md` near 445 lists as unknown.
- Retail `sound.tdf` (all four copies) uses only the numbered-1 form for `select`, `ok`, `arrived`
  and `cant`, no second choice, no `load`, `unload` or caption key, so RWE's one-key reading
  loses nothing on retail data. Retail TDF files separate RWE's parser from the chapter's grammar
  by value in no case but the unresolved one of conflict 66.

## Batch 8 (2026-10-17)

Module chapters `ingame-info-panel`, `map-radar`, `graphics-model-render`, `graphics-particles` and
`graphics-palette` with `graphics-palette-tables`. Same reference commit and the same evidence class
(static reading); the palette chapters' generator recipes were additionally replayed against the
shipped palette files (throw-away script, outside the tree). The audits are
`BT-INGAME-INFO-PANEL-PARITY.md`, `BT-MAP-RADAR-PARITY.md`, `BT-GRAPHICS-MODEL-RENDER-PARITY.md`,
`BT-GRAPHICS-PARTICLES-PARITY.md` and `BT-GRAPHICS-PALETTE-PARITY.md`. Neither side is chosen;
kinds as above. Items are numbered from 71; if another author has appended items, renumber on merge.
All of these are presentation except 72 and 73, which reach the simulation.

71. **Hovering a radar or sonar contact on the minimap names it** (design versus register and
    reference). Register (`docs/TOTALA-EXE-VISION.md` section 18) and reference (`ingame-info-panel`
    section 7): the footer's detail is gated on the visibility service, and a contact that fails it
    prints "R: Unidentified object" ("S:" for sonar). RWE picks minimap units with the radar-inclusive
    predicate (`GameScene_commands.cpp` about 45-85) and draws the real name and health bar
    (`GameScene_render.cpp` 314-336); the order-target slot does the same (about 439). The register
    lists only the missing text as not ported. Kind: design. Audit: `BT-INGAME-INFO-PANEL-PARITY.md`
    rule 7.

72. **Allied radar and sonar emitters** (design versus register and reference; narrows an open item).
    Reference (`ingame-info-panel` sections 1, 2): contacts are computed for the displayed player and
    only that player's own units emit; the first pass gives an allied unit the contact bits when the
    ally's record byte `+0x97` bit 6 (the register's `+ShareRadar`, `0x40`) is set, and does nothing for
    the ally's dishes. Register (`docs/TOTALA-EXE-VISION.md` "The visibility pass"): "each of the
    viewer's live units". RWE adds every unit's detector to every allied player
    (`GameSimulation_visibility.cpp` about 580-610), a choice recorded in `docs/ROADMAP.md` (2026-09-03)
    and left **[open]** in `docs/TOTALA-EXE-COMMANDS.md`. The sonar half is hash-visible: an ally's
    sonar lifts the below-surface veto in `canSeeUnit`. Kind: design. Audit:
    `BT-INGAME-INFO-PANEL-PARITY.md` rule 2, Gaps rank 1.

73. **The cloak proximity fuse: who counts** (register wording versus reference wording versus code).
    Register: "any live enemy of that owner within that distance" (`0x40B0D0`,
    `docs/TOTALA-EXE-VISION.md`). Reference: `HasReadyUnitInRange` at `mincloakdistance`. RWE counts any
    unit not owned by the cloaker, so an allied unit or an unfinished enemy decloaks it
    (`GameSimulation_visibility.cpp` about 719-760); the flag feeds `canSeeUnit`. Neither side says what
    "enemy" or "ready" excludes. Kind: register (unspecified). Audit:
    `BT-INGAME-INFO-PANEL-PARITY.md` rule 4, Gaps rank 2.

74. **The reload-timer threshold in the footer's redraw key** (register versus reference). Register
    (`docs/TOTALA-EXE-INTERFACE.md` section 99): weapons whose reload is "under thirty ticks" are
    skipped. Reference (`ingame-info-panel` section 6): only reloads "greater than 30" contribute. They
    differ at exactly 30 (and in units). The counters are collected and never drawn, so nothing is
    observable. Kind: register. Audit: `BT-INGAME-INFO-PANEL-PARITY.md` rule 7.

75. **Particle list cap: 400 or 401** (register versus reference). Register
    (`docs/TOTALA-EXE-DATA.md`, "The steam is engine particles", `0x472CD9`): each of the ten layers holds
    at most 400 entries and drops its oldest. Reference (`graphics-particles` section 3): the oldest is
    evicted only when the size is **greater than** 400, so a list reaches 401. Unobservable at that scale.
    Kind: register. Audit: `BT-GRAPHICS-PARTICLES-PARITY.md` rule 3.

76. **Smoke frame reload range** (register versus reference). Register
    (`docs/TOTALA-EXE-RENDER.md` section 4): after the first frame a puff's countdown reloads with
    `half + rand x half / 0x8000`, "3 to 5 ticks" for a period of 7. Reference
    (`graphics-particles` section 9): from half the hold period "through approximately the full period
    minus one", which for 7 would be up to 6. RWE follows the register (`Particle.cpp` about 67). Kind:
    register. Audit: `BT-GRAPHICS-PARTICLES-PARITY.md` rule 10.

77. **Does the fog gate a thermal vent's steam** (design versus reference, and a register
    extrapolation). Reference (`graphics-particles` sections 4, 10): the vent emitter ("TimedSub")
    draws with no visibility test; fog is composited after the particle lists, so remembered ground still
    shows its steam, dimmed. Register: the fog-effects work says "the original refuses every effect for a
    place the local player cannot see", citing `0x480EEA`, which is in `EmitSfx` and does not reach a
    vent emitter made at feature placement. RWE hides the steam in any ground not currently visible
    (`GameScene_commands.cpp` about 3297-3316, comment "a vent under the fog of war keeps its steam to
    itself"). Kind: design, and register (the generalisation). Audit:
    `BT-GRAPHICS-PARTICLES-PARITY.md` rules 5 and 11; compare `BT-WEAPONS-EXPLOSIONS-PARITY.md` rule 9.

78. **Underwater tint is a palette remap, not a multiplier** (design versus reference). Reference
    (`graphics-palette` section 5, `graphics-model-render` section 8): pixels at or below the threshold
    go through the blue table, red halved, green halved, blue halved plus 50, snapped to the palette
    (black becomes `(0, 0, 55)`, white `(107, 143, 163)`). RWE multiplies by `(0.5, 0.5, 1.0)`
    (`shaders/unitTexture.frag` 46, 126; `unitBuild.frag` 40, 70); average channel error 19.6 of 255.
    Related to, and not the same as, the open BT-16 (cut versus tint by owner). Kind: design. Audit:
    `BT-GRAPHICS-PALETTE-PARITY.md` rule 7, `BT-GRAPHICS-MODEL-RENDER-PARITY.md` rule 8.

79. **A digger's final picture is cut at 125** (register versus reference). Register
    (`docs/TOTALA-EXE-ECONOMY.md` near 151 and 219): the 125 cut is on the **copied shadow**
    (`0x4BA1B0`); `docs/TOTALA-EXE-VISION.md` near 437 calls `0x4BA1B0` and `0x4B96E0` "brightness
    passes". Reference (`graphics-model-render` section 8): the final image of a digger definition
    (ARMAMB, CORTOAST) also gets a cutoff at 125, and the two functions are the water cut and the water
    tint. Kind: register. Audit: `BT-GRAPHICS-MODEL-RENDER-PARITY.md` rule 8, Gaps rank 2.

80. **The in-world health bar** (design versus reference; the register is silent). Reference
    (`ingame-info-panel` section 9): 34 by 4 framed, 32 interior pixels scaled by health over maximum,
    colour changing strictly above integer one-third and two-thirds of maximum health. RWE draws 35 by 5
    (33 by 3 inside), fills `1 + floor(fraction x 32)`, and switches colour at floating-point 1/3 and 2/3
    (`UiRenderService.cpp` about 270-304); its horizontal offset of 17 is half of 34. Kind: design. Audit:
    `BT-INGAME-INFO-PANEL-PARITY.md` rule 8.

81. **Footer text for features marked `nodisplayinfo`** (register numbering versus reference).
    Reference (`ingame-info-panel` section 7): feature text is suppressed by "feature flag 4"; the
    register's feature flag word (`docs/TOTALA-EXE-DATA.md` near 100) has `nodisplayinfo` at bit 10 and
    nothing numbered 4 that fits, so the chapter's numbering is its own. RWE parses `nodisplayinfo` and the
    footer never reads it (`GameScene_render.cpp` about 468-492); 46 shipped features set it. Kind:
    register (unspecified). Audit: `BT-INGAME-INFO-PANEL-PARITY.md` rule 7, Gaps rank 4.

82. **The resource HUD's glide, K suffix and share marker** (design; the register is silent).
    Reference (`ingame-info-panel` section 5): displayed amounts move an eighth of the way to the
    target a step, at least one unit; energy rates print with a K suffix above 99999; a positive share
    threshold below the amount draws a two-pixel marker on the bar. RWE draws the live amounts, plain
    integers, no marker (`GameScene_render.cpp` 224-265, `resource_io.cpp`). Kind: design. Audit:
    `BT-INGAME-INFO-PANEL-PARITY.md` rule 6.

83. **The minimap's fog colours and picture size** (design versus reference; the register is silent).
    Reference (`map-radar` sections 1, 3): picture fitted into 126 pixels; unexplored pixels take a "fog
    colour" and mapped-but-unseen pixels go through the display colour map. RWE fits 128 and draws opaque
    black and 47 percent black (`GameScene_commands.cpp` about 1335-1385). The two colours are undecoded.
    Kind: design. Audit: `BT-MAP-RADAR-PARITY.md` rules 2, 3.

### Probable code faults found on the way (not reference conflicts)

- **The nano spray's landing window is twice as wide as the register says.** `docs/TOTALA-EXE-ECONOMY.md`
  ("The spray") and the chapter agree the window is the middle three elevenths (4/11 to 7/11, 3/11 wide)
  of the target's extent; `spawnNanoParticles` samples `+-3/11` of the full footprint, 6/11 wide
  (`GameScene_commands.cpp` about 3412, 3431). Audit: `BT-GRAPHICS-PARTICLES-PARITY.md` rule 7.
- **`generateAlphaTable` is not the original's recipe, and its header says the recipe was never found.**
  The register decodes it (`docs/TOTALA-EXE-VISION.md`, `0x4BA772`, which calls the band matcher
  `0x4BA9D0`); RWE uses an exhaustive nearest scan. Replayed over the shipped palette, the chapter's
  recipe reproduces the shipped `PALETTE.ALP` with 0 of 65536 bytes different and RWE's generator
  differs on 4552 (6.9 percent). Only the fallback is affected (the file is used when present).
  Audit: `BT-GRAPHICS-PALETTE-PARITY.md` rule 5.
- **Stale register text:** `docs/TOTALA-EXE-DATA.md` near 248 says RWE depth-tests particles
  (`docs/TOTALA-EXE-RENDER.md` section 5 corrected that); `docs/TOTALA-EXE-RENDER.md` section 54's table
  gives `PALETTE.SHD` "row 16 ~ identity, row 31 ~ 1.55x" and `PALETTE.LHT` "row 31 ~ 1.5x"
  (superseded by `-SHADING.md` section 09 and `-RENDER.md` section 100: rows 14 and 15 identity, 1.807
  and 1.77); `docs/TOTALA-EXE-VISION.md` near 437 calls the water cut and tint "brightness passes".
- **No projectile symbols on the minimap** (a launched nuke is not shown at all) and **no damage
  blink** on the dots. The reference supplies the missing rule for `noradar` (it removes an ordinary
  projectile's one-pixel minimap symbol; no weapon in `totala1` sets it). Audit:
  `BT-MAP-RADAR-PARITY.md` rules 4 and 6.

### Re-confirmed, not new (Batch 8)

- The sensor visitor: stealth skip, sonar contact at or below the sea with strict distance, radar
  contact with the model top at or above it, the radar lift of twice the detector's altitude capped by
  the sweep radius `max(radar, sonar)`, jamming after detection, direct sight restoring only the radar
  bit, and the 90-tick cloak tail, all agree with `docs/TOTALA-EXE-VISION.md` (BT-6 and BT-7 resolved).
- The minimap dot gate (the radar or sonar bit, or own), the `z - y/2` position, `radlogo` and
  `radlogohigh`, the selected-unit ring decode (four fields, sight not drawn, the on/off gate with the
  anti-missile exception, colours, the 32-gon, the dash parity, the -512) agree with
  `-VISION.md` sections 18 and 25 and are implemented.
- The shading arithmetic (light vector, face normal from the first three indices, upward normal for a
  repeated index, per-vertex mean, five times the dot masked with 31, row 15 for unlit pieces, buildings
  only, ZBuffer=0 unshaded, supersampling for buildings only) agrees with `docs/TOTALA-EXE-SHADING.md`
  with no conflict; the chapter omits only the collinear-NaN case and the per-piece averaging.
- The five construction-recolour regimes were re-read against `computeBuildPhase` row by row
  (`255 x (v-235)/20`, `255 x (v-200)/35`, `3 x (115-v) - 1`, `3 x (30-v) - 1`, `255 x v/30`, low byte
  kept): identical. The two shadow passes, their gates and the 5-pixel offset agree with
  `-RENDER.md` section 100.
- The nano spray's constants (4 units a tick, five attempts, palette 161-167, no particle for a target
  nearer than a step), the smoke class (wind x 8, gravity x 4, stopping frame `2 + rand`), the wake class
  (palette 97-103, expiry `period x 6`, scatter -3 to 3, death when the ground under it reaches the sea)
  and the vent emitter (immortal, 5-tick interval, gravity x 16) agree with the register and are
  implemented.
- The palette generators: the chapter's shade recipe reproduces the shipped `PALETTE.SHD` and its light
  recipe the shipped `PALETTE.LHT`, each with 0 bytes different; the band matcher's empty-band edge case
  fires on none of the 82,000-odd requests the five tables make on the retail palette. The brightness
  multiply (`min(255, c x m)`) agrees with the comment in `shaders/worldPost.frag`.

## Batch 9 (2026-10-17)

Module chapters `graphics-gaf`, `graphics-font`, `graphics-movies`, `graphics-pcx` (PCX and BMP, screenshots,
shared 3DO helpers), `util-sqsh` and `game-settings-campaign`. Same reference commit and the same evidence class
(static reading); the shipped GAF, FNT, PCX, 3DO, text and film files were additionally censused through
`tools/hpi.py`'s reader with throw-away scripts kept outside the tree (958 GAFs, 39 FNTs, 783 PCXs, 610 models,
the five films' headers). The audits are `BT-GRAPHICS-GAF-PARITY.md`, `BT-GRAPHICS-FONT-PARITY.md`,
`BT-GRAPHICS-MOVIES-PARITY.md`, `BT-GRAPHICS-PCX-PARITY.md`, `BT-UTIL-SQSH-PARITY.md` and
`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`. Neither side is chosen; kinds as above. Items are numbered from 84; if
another author has appended items, renumber on merge. All of these are presentation or file handling; none reaches
the simulation.

84. **How a film is placed on the screen** (design versus reference; `docs/TOTALA-EXE-EXTERNAL.md`, not read by
    this project, agrees with the reference). Reference (`graphics-movies` section 6): the frame is written at x = 0,
    y = (480 - height) >> 1, unscaled, into the 640 x 480 surface. `-EXTERNAL.md` adds that films 1 to 4 (640 x 240,
    header flag 2) reach 480 by an alternate-scanline mode that leaves the other rows black and that film 5
    (640 x 304, flag 0) sits at y = 88. RWE (`MovieScene.cpp` about 128-150, comment "the movies are stored
    anamorphic") stretches every film to the largest 4:3 rectangle: films 1 to 4 with doubled rows and no dark
    lines, **film 5 (the credits) 58 percent taller than it should be**. RWE's decoder reads only header flag bit 0.
    Kind: design. Audit: `BT-GRAPHICS-MOVIES-PARITY.md` rule 3.

85. **What skips a film** (design versus reference). Reference (section 8): only a character message or Alt+F4 stops
    a film; a mouse click, Shift, Ctrl, arrows and function keys do not. RWE: any key press or any mouse button
    (`MovieScene::onKeyDown`, `onMouseDown`; `docs/ROADMAP.md` "any key or click skips it, as it skips every film").
    Kind: design. Audit: `BT-GRAPHICS-MOVIES-PARITY.md` rule 5.

86. **Does the registry carry campaign progress** (register versus reference). Register
    (`docs/TOTALA-EXE-DATA.md` section 115, "Progress"): "Progress is kept only in save games. No registry key or
    file holds it." Reference (`game-settings-campaign` section 6): two registry values, `ArmCamp` and `CoreCamp`,
    hold a 26-byte status string whose default is 25 letters `U` and a NUL, read and written by `0x432a80` and
    `0x432b00`; the chapter does not say who calls them, and says they are distinct from the save summary's
    `Thumbs`. The dev machine's key has neither value. RWE keeps progress in the running game and in saves only.
    Kind: register. Audit: `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` rule 7.

87. **The first screenshot number** (register's open point versus reference). Register
    (`docs/TOTALA-EXE-KEYBOARD.md` section 77): "where the original's numbering starts is not decoded; RWE's first is
    `SHOT0000`". Reference (`graphics-pcx` section 4): the highest **positive** parsed number, "starting from zero",
    determines the next name, so a file numbered 0 does not count and an empty folder has highest 0. If the next name
    is highest plus one the first shot is `SHOT0001`; the chapter does not say whether it adds one. Kind: register
    (open). Audit: `BT-GRAPHICS-PCX-PARITY.md` rule 4.

88. **GAF per-frame durations** (design versus reference; the register uses the field once). Reference
    (`graphics-gaf` section 2): each frame carries a duration and sequences are stepped by it inside the tick (BT-1).
    Register (`docs/TOTALA-EXE-RENDER.md` near 510-542): an in-world order marker steps at `tick / (2 x frame0
    duration)`. RWE's reader discards the field (`SpriteSeries` is a bare list of sprites) and every animation uses a
    constant chosen in code: the cursor six frames a second of wall time, smoke 2 or 15 ticks, particles 4. The files
    hold a constant per entry, 1 to 10 (cursors 3 to 10, `smoke 1` 5, `smoke 2` 3, `Explosion` 2, `fire1` 10).
    Kind: design. Audit: `BT-GRAPHICS-GAF-PARITY.md` rule 5.

89. **Bitmap font vertical offset, byte-oriented text and the newline** (design versus reference; the register is
    silent). Reference (`graphics-font` sections 1 to 3): byte 2 of an FNT is a signed vertical offset the glyph blitter
    applies (shipped values 1 to 3 in 39 files); characters are bytes with no UTF-8; the newline ends the line. RWE
    ignores the offset, draws text through the checked UTF-8 iterator, and draws a newline as an empty glyph and carries
    on. Kind: design. Audit: `BT-GRAPHICS-FONT-PARITY.md` rules 2, 3.

90. **How strict the archive chunk reader is** (design versus reference). Reference (`util-sqsh` sections 7 to 9):
    the unpacker rejects a decoded length that differs from the header (error 3) and has no case for method 0
    (stored) or 3. RWE accepts method 0 and a chunk that decodes short (the tail is silently zero) and never
    compares the chunks' total with the file's size; `tools/hpi.py` refuses the last two. Kind: design.
    Audit: `BT-UTIL-SQSH-PARITY.md` rules 2, 4, 7.

91. **The GAF version word** (design versus reference). Reference (`graphics-gaf` section 1): no signature or
    version check. RWE throws unless the word is `0x00010100`. **Five shipped files carry version 0**:
    `anims/vismasks.gaf` (in `totala1.hpi`, `btdata.ccx`, `ccdata.ccx`, `rev31.gp3`) and `anims/terrain.gaf`
    (`totala1.hpi`). `vismasks.gaf` is the table `BT-MAP-LINE-OF-SIGHT-PARITY.md` rule 5 compares circular sight
    against. Kind: design. Audit: `BT-GRAPHICS-GAF-PARITY.md` rule 2.

92. **Layers that stick out of their parent frame, and blend kind** (design versus reference). Reference
    (`graphics-gaf` sections 1, 4): each child frame is drawn from its own hotspot without clipping to the parent, and
    a child with a non-zero blend kind is drawn blended. RWE crops every layer to the parent's rectangle and ignores the
    blend-kind field (`TextureService.cpp` about 28-45). Neither affects the retail set: none of 14,540 distinct
    layered frames has an opaque pixel outside its parent, and the field is 0 in all 74,529 layer headers and 48,617
    frame headers. Kind: design. Audit: `BT-GRAPHICS-GAF-PARITY.md` rules 2, 4.

93. **The packet decoder's window** (register versus reference). Register (`docs/TA-DEMOS.md` near 320-350,
    `tadDecompress`): offsets are **absolute positions in the output**, reads past the end give zero. Reference
    (`util-sqsh` sections 4, 6): the offsets index a 4,096-byte ring that wraps, and the network condenser enables a
    **preset dictionary** (bytes copied from ring offset 0x13). The two agree below 4,096 bytes of output and when no
    packet names the preset; every captured packet decodes. Kind: register. Audit: `BT-UTIL-SQSH-PARITY.md` rule 6.

### Malformed-input faults found on the way (separate from the conflicts above)

Each is a way a file in a mod, an archive or a save folder can cost more than itself. `CLAUDE.md` ("Anything read
from a file or a socket is someone else's") and `docs/SECURITY-AUDIT.md` apply. The tests that exist for these
parsers are in `src/rwe/io/malformed_input.test.cpp` (one GAF case, one Smacker case, three HPI, three PCX, three 3DO,
no FNT, no save body); none of the faults below has a case.

- **M1. `readRaw` checks a short read with `assert` alone** (`io/io_util.h`): a release build returns an
  uninitialised struct. Used unchecked by `GafArchive`, `extractGafEntry`, `FntArchive` and the HPI header
  (the 3DO parser checks the stream itself). A truncated GAF gets garbage entry names and frame counts; a
  truncated FNT garbage offsets; a file under 20 bytes a garbage HPI header. Not an overrun; a function of the stack.
- **M2. GAF entry-table amplification** (`GafArchive.cpp` about 14-28, 45-60). The entry cap (65,536) is per file, each
  entry may claim 65,535 eight-byte frames, and the entry pointers may all be the same one: a file of about 790 KB
  builds about 34 GB of frame tables and makes about 4.3 billion stream reads. Needs a cap on the total.
- **M3. GAF compressed rows**: a short row is filled from the row's first pixel instead of the first unwritten one
  (`gaf_util.cpp` about 71), and bytes left after a full row are not skipped, so the next row's length is read from the
  wrong place. Retail data has neither (0 of 5.68 million rows). Data integrity, not memory.
- **M4. Smacker frame-table size wraps in 32 bits** (`SmkDecoder.cpp` about 122-137): `(frameCount + ring) * 5` is an
  unsigned product taken before the length test. 858,993,460 frames passes on a 108-byte file and then reserves 3.4 GB
  and reads 858 million entries off the end of the buffer; 4,294,967,295 frames with the ring flag makes the table size 0
  and `decodeNextFrame` indexes an empty vector.
- **M5. Smacker Huffman tree builders recurse once per set bit** (about 150-230), bounded only by the tree block's
  length: a block of ones recurses millions deep (stack exhaustion). A real tree has at most 256 or 1,024 leaves.
- **M6. Smacker audio chunk reserves its declared size** before decoding a sample (about 371, 393): up to 4 GB (16-bit)
  or 8 GB (8-bit) from a few bytes.
- **M7. The HPI LZ77 ring is an uninitialised stack array** (`hpi_util.cpp` about 68; the original and `tools/hpi.py`
  zero it), and a chunk that ends early is accepted with a zero tail (about 253, 258). Conforming streams are unaffected.
- **M8. FNT height is a 16-bit field with no cap** (`Fnt.cpp`, `TextureService.cpp` about 345): at 65,535 rows each of 256
  glyph textures is about 66 MB. And **a byte above 0x7f that is not valid UTF-8 in any drawn string throws out of the
  render call** (`UiRenderService.cpp` about 101-154, the bundled `utfcpp` checked iterator): 309 of 435 shipped FBIs
  contain such bytes, but only in the French/German/Spanish/Italian keys an English game never draws; a third-party unit
  with one accent in an English `Description` or `UnitName` would crash the frame when selected. Not run; follows from
  the iterator's documented behaviour.
- **M9. Parse exceptions are caught at no loader.** `GafException`, `PcxException`, the 3DO and Smacker `runtime_error`,
  `HpiException`, the font and the save body's errors propagate to `main`'s "Critical Error" box: **one bad asset in a
  mod ends the launch** instead of costing that asset (only `loadFogTileSet` catches). A zero-length film file is passed
  to the decoder as a present film and throws; the original skips it.
- **M10. The save write is unchecked and the name unfiltered** (`SaveFile.cpp` about 289-296): "Game saved" is printed
  whatever the stream did, and a `name` with separators or `..` leaves the saves folder (own machine only).

### Probable code faults found on the way (not reference conflicts)

- **A 17-row bitmap font loses its bottom pixel row.** The glyph read is `2 x width` bytes (16 rows) where the file
  stores `ceil(width x height / 8)` (confirmed on seven fonts: every glyph-to-glyph distance matches exactly). Four
  shipped files are 17 high: `fonts/armfont.fnt` in `btdata.ccx`, `ccdata.ccx` and `rev31.gp3`, and `fonts/hatt14.fnt`
  in `totala1.hpi`. The Arm campaign screens load `ARMFONT.FNT`. Audit: `BT-GRAPHICS-FONT-PARITY.md` rule 2.
- **`getUnitPic` and the bitmap loader do not catch `PcxException`**, nor does the model loader catch the 3DO parser's
  `runtime_error` (M9).
- **A model with every vertex below its origin has a negative height in RWE and 0 in the original.** Retail: the two
  rules agree on all 610 distinct models. Audit: `BT-GRAPHICS-PCX-PARITY.md` rule 5.
- **`nextScreenshotNumber` ignores names that are not exactly `SHOT` + 4 digits + `.pcx`**, so after `SHOT9999` every
  shot writes `SHOT10000.pcx` over the last. Audit: `BT-GRAPHICS-PCX-PARITY.md` rule 4.
- **`tools/hpi.py` and `hpi_util.cpp` disagree** on the LZ77 ring, on a chunk that stops short, on chunks that add up to
  less than the file and on a stored file cut off by the end of the archive (the Python is the stricter in three of the
  four). `CLAUDE.md` says they must change together.

### Re-confirmed, not new (Batch 9)

- The GAF compressed-row command forms (key skip up to 127 with no value byte, repeat up to 64 with one value byte,
  literal up to 64), the zero-length transparent row and the hotspot placement agree with the chapter; all 5,127,833
  non-empty retail rows decode exactly, with none short, overlong or with bytes left over. Layer composition order and
  offsets agree on all 14,540 distinct layered frames (two to twelve layers, never nested).
- The Smacker frame period (`-3333` is 33.33 ms, the Smacker convention) is the same for all five films in RWE and in
  the chapter, and a missing film is skipped silently by both.
- The HPI chunk header (19 bytes, `SQSH`, version not checked), the checksum over the stored bytes before the inner
  transform, the inner transform (subtract the index, XOR it) and the LZ77 token format (low bit first, absolute 12-bit
  ring index, length 2 to 17, index 0 ends, ring position starting at 1) agree with the chapter.
- The PCX run coding (top-two-bit run header, six-bit count, overlong run clamped to the row) and the end-relative
  palette agree; all 782 real retail PCX files have bytes-per-line equal to the width, 8 bits, one plane and the 0x0c
  marker, so RWE's stricter palette test and its cropping of padded rows change nothing on shipped data. The one `.pcx`
  that is not a PCX (`palettes/guipal.pcx`, a raw 1,024-byte palette) is never read as one.
- The model height rule (zero floor, vertex Y plus the node's offset, child height plus the parent's offset) gives the
  same number as RWE's `findHighestVertex` for all 610 distinct retail `.3do` files, and no retail model has a sibling
  at its root. The selection-plate swap into entry 0 is the register's (`-RENDER.md` near 1493, 1538) and RWE reaches
  the same drawing by skipping the declared index.
- A campaign saved between missions as a header with `BetweenMissions` set, naming the next mission whether the last
  was won or lost, and a `Thumbs` string of 25 letters reset to `U` when the length is wrong, agree with
  `docs/TOTALA-EXE-DATA.md` section 115. The skirmish option names in the summary (`CommanderDeath`, `Location`,
  `Mapping`, `LineOfSight`, `LineOfSightType`) are all saved by RWE under its own names.

## Batch 10 (2026-10-17)

Module chapters `game-main-loop`, `game-cd-check`, `util-hpi`, `game-data-files` and `game-side-data`. Same reference
commit and the same evidence class (static reading); the shipped archives were additionally censused through
`tools/hpi.py`'s reader with throw-away scripts kept outside the tree (30 archives, 7889 entries: overlaps and byte
comparisons between archives, duplicate names, footers, language directories, `version.tdf`, every `textures/*.gaf`
entry's frame count, 275 `.tnt` headers, the `sidedata.TDF` rectangles). The audits are `BT-GAME-MAIN-LOOP-PARITY.md`,
`BT-GAME-CD-CHECK-PARITY.md`, `BT-UTIL-HPI-PARITY.md`, `BT-GAME-DATA-FILES-PARITY.md` and `BT-GAME-SIDE-DATA-PARITY.md`.
Neither side is chosen; kinds as above, plus **audit** where the other side is an earlier audit of this series. Items
are numbered from 94; if another author has appended items, renumber on merge. Only item 98 reaches the simulation.

94. **Archive tie classes (BT-29)** (audit versus reference). `TA-SYSTEMS-PARITY.md` rule 32 and the open row BT-29 read
    the startup chapter's shared registration argument as possibly making `rev31.GP3` and the CCX files one priority
    class and the UFO and HPI files another, so RWE's strict ranking GP3 over CCX over UFO over HPI would be a
    difference. `game-cd-check` (section "Registration sequence") says the integer passed to `AddArchive` is **stream
    retention** (1 keeps the stream open, 0 lets it close and reopen), "not an explicit priority number", and that
    priority comes from insertion order. On that reading the stage order is strict and RWE agrees; what remains of BT-29
    is the extra `.gpf` and non-`rev31` `.gp3`, and the cap (item 96). Census: `rev31.gp3` and `btdata.ccx` share 887
    entries of which 98 differ (96 `anims`, `translate.tdf`, and **`weapons/armyork_weapon.tdf`**, a flak cannon whose
    `rev31` copy has `reloadtime=.55` and whose `btdata` copy has no `reloadtime`), so the order is not immaterial.
    Kind: audit. Audit: `BT-GAME-CD-CHECK-PARITY.md` rule 2.

95. **Loose unit and weapon definitions** (design versus reference). Reference (`game-cd-check`, "Alternate path building
    and consumer checks"): when the archive-mode global `DAT_0050289c` is nonzero, weapon enumeration loads a TDF only
    when the parser identifies accepted archive-backed content, and unit discovery clears availability for a **loose**
    definition. So a loose `units/*.FBI` or weapon TDF in the game folder is rejected and a mod has to ship an archive.
    Whether a retail GOG run has the global set is not shown. RWE puts the data folder first and accepts loose
    definitions (`GameSimulationLoader.cpp` about 412, 483-520). The register (`TOTALA-EXE*.md`, sections 88 and 91) is
    silent. Separately, the lobby's data check hashes archives only (`launcher/src/common/archives.ts`, `isArchive`), so
    a loose override is invisible to it. Kind: design. Audit: `BT-GAME-CD-CHECK-PARITY.md` rule 6.

96. **The ten-HPI allowance on the shipped install** (design versus reference). Reference: the `*.HPI` stage stops
    after ten successful additions. The GOG install folder holds **13** `.hpi` files (`tactics1` to `tactics8`,
    `totala1` to `totala4`, `worlds`); in name order the first ten end at `totala2.hpi`, leaving `totala3.hpi`
    (install resources), `totala4.hpi` (152 `camps`, 100 `maps`, 50 `bitmaps`) and `worlds.hpi` (837 `sections/*.sct`)
    unmounted by this routine. RWE mounts all (`addHpisWithExtension`). Whether the original really runs this install
    that way is open (the chapter describes the function, not the install). Kind: design. Audit:
    `BT-GAME-CD-CHECK-PARITY.md` rule 3.

97. **Duplicate names inside one archive directory** (design versus reference; the Python reader differs from the C++
    one). Reference (`util-hpi`, "Opening files"): each component scans a directory's entries **backwards**, so the
    **last** duplicate wins. RWE's lookup returns the **first** (`HpiArchive.cpp` about 182-233); `tools/hpi.py` keeps
    the **last** (a dictionary overwrite), so the two readers also disagree with each other (`CLAUDE.md` says they must
    change together). Census: no retail archive holds one. Kind: design. Audit: `BT-UTIL-HPI-PARITY.md` rule 2.

98. **Where a skirmish player's starting storage comes from** (register versus reference). Register
    (`docs/TOTALA-EXE-ECONOMY.md` section 23, "Not ported, and why"): the per-player base is `player+0xDC` and
    `player+0xE0` (energy and metal), added when `player+0x149` bit 0 is set, read from a save or scenario at `0x4661B1`
    and `0x4661C7`; "where a skirmish gets its own values from has not been found"; RWE gives the commander the side's
    starting stockpile as storage and only a mission sets the flag. Reference (`game-main-loop`, "Mission-option and
    resource helpers", "Commander creation"): three routines write the same two offsets with a **floor of 200** each and
    mark the player started, and `InitStartingResourcesFromSkirmish` stores the **running maximum** of the qualifying
    players' start values, in one loop over ten slots, so a later slot gets at least the largest start value of any
    earlier one and an earlier slot is not revisited. The offsets agree with the register's; that the "start flag" is
    `player+0x149` bit 0 is an inference. All 86 corpus players open at 1000 and 1000, where the readings agree.
    Kind: register. Audit: `BT-GAME-MAIN-LOOP-PARITY.md` rule 8.

99. **Animated model textures** (design versus register). Register (`docs/TOTALA-EXE-RENDER.md` section 55): a
    multi-frame texture other than the ten-frame team texture is drawn from the current animation frame, with a freeze
    flag that pins frame 0. Reference (`game-data-files`, "Texture archives and model binding"): the binder puts each
    animated texture's sequence on a global play list. RWE keeps **frame 0 only** (`atlas_util.cpp` about 303-308,
    "TODO: support animated textures"), absent from sections 88 and 91. Census: the animated entries are `flashing01`,
    `flashing02`, `glow`, `CorPlat`, `AlienGlo` (three entries) and `onoff01`, found in about forty of 596 models
    (both commanders, the Core labs and plants, the Arm aircraft and vehicle plants, sonar, targeting facilities).
    Kind: design. Audit: `BT-GAME-DATA-FILES-PARITY.md` rule 7.

100. **An empty file reads as absent** (design versus reference). Reference (`util-hpi`, "Closing and whole-file
     adapters"; `game-data-files`, palette adapter): the whole-file loader rejects a missing **or nonpositive-sized**
     file; an empty `.PAL` is absent. RWE returns an empty vector for an empty file from both leaf file systems, so each
     consumer sees a present, empty file (the cause of the zero-length film fault in item M9 of Batch 9; an empty
     `.fbi`, `.tdf`, `.pcx` or `.gaf` meets the same). Census: one retail entry has size zero
     (`camps/useonly/example.tdf`, never listed). Kind: design. Audit: `BT-UTIL-HPI-PARITY.md` rule 3.

101. **The archive copyright footer** (design versus reference). Reference (`util-hpi`, "Archive registration"): opening
     checks magic, version and a trailing `Copyright <year> Cavedog Entertainment` string with the four year characters
     normalised. RWE checks magic and version only (`HpiArchive.cpp` about 121-130). Census: all 30 retail archives carry
     it, with the years 1997, 1998, 1999 and 2000, which is why the original normalises them. RWE therefore accepts
     archives the original refuses (the harmless direction). Kind: design. Audit: `BT-UTIL-HPI-PARITY.md` rule 6.

102. **The radar-picture loader's header offsets** (reference versus the `.tnt` layout RWE reads). Reference
     (`game-data-files`, `LoadRadarPic`): width and height from `+4` and `+8`, a picture offset at `+0x28`, and a load
     only when bit 0 of the byte at `+0x2c` is set. RWE's `.tnt` header (`TntArchive.h`) has the sea level at `+0x28`,
     the minimap pointer at `+0x2c` and `unknown1` at `+0x30`. Census of all 275 shipped `.tnt` headers: `+0x30` is zero
     in every one and `+0x2c` is a nonzero pointer in every one; read four bytes early the flag would be clear on every
     shipped map, which cannot be so for a game that shows previews. Either the routine reads another file or the
     chapter's offsets are off; RWE reads the minimap unconditionally. Kind: reference. Audit:
     `BT-GAME-DATA-FILES-PARITY.md` rule 3.

103. **How strictly `sidedata.TDF` is read** (design versus reference). Reference (`game-side-data`): a missing scalar
     keeps the slot's old contents (colours default to zero), a missing rectangle coordinate is zero, a missing rectangle
     record is fatal, `fontgui` is not read by the loader, the side is an index into five slots and the side table has as
     many sides as there are consecutive `SIDEn` records. RWE throws on any missing scalar or coordinate **including
     `fontgui`, which nothing reads**, keys the table by the exact-case `name` string, offers only the literals `ARM` and
     `CORE` in the lobby, the campaign and `--player`, and ignores the side's `font`. Retail has two complete sides, so
     none of this shows on shipped data. Kind: design. Audit: `BT-GAME-SIDE-DATA-PARITY.md` rules 2 to 4.

104. **Edge scroll and game keys while a menu is open** (design versus reference). Reference (`game-main-loop`, "Input,
     selection refresh, drawing"): `HandleGameKey` and `UpdateEdgeScroll` run only when bit 0 of the interface flag is
     clear, the same bit that gates local simulation (the menus set it). `docs/TOTALA-EXE-INTERFACE.md` section 70
     records the pause half. RWE's edge scroll is gated on keyboard focus only (`GameScene.cpp` about 651-690) and the
     arrow nudge on nothing. Kind: design. Audit: `BT-GAME-MAIN-LOOP-PARITY.md` rule 4.

### Probable code faults found on the way (not reference conflicts)

- **The launcher's archive hash skips symbolic links; the engine loads them.** `listArchives` filters on
  `Dirent.isFile()` (`archives.ts` about 97), which is false for a link, while `addHpisWithExtension` selects by
  extension alone. A data folder assembled from links passes the lobby check with data the check never hashed. Not run.
  Audit: `BT-GAME-CD-CHECK-PARITY.md` rule 4.
- **The launcher's load order and NTFS's differ for names with `_`, `[`, `\`, `]`, `^` or the backquote.** NTFS orders
  by upper-cased name (those characters after the letters), the launcher lower-cases (before them). The engine itself
  does not sort. Retail names are plain letters and digits. Audit: `BT-GAME-CD-CHECK-PARITY.md` rule 4.
- **A HUD colour of 256 or more throws every frame.** `energycolor` and `metalcolor` are stored unclamped and used as
  `palette->at(index)` on a 256-entry vector (`GameScene_render.cpp` about 229-264). Mod data only (the shipped values
  are 208 and 224). Audit: `BT-GAME-SIDE-DATA-PARITY.md` rule 6.
- **A palette shipped only as `.PCX` fails to launch.** RWE reads `palettes/PALETTE.PAL` and `GUIPAL.PAL` and throws
  when one is absent; the original decodes `<name>.PCX`, caches the `.PAL` and regenerates the derived tables. No such
  file ships. Audit: `BT-GAME-DATA-FILES-PARITY.md` rule 3.
- **Ten feature `.tdf` files are listed and parsed twice** because the recursive listing keeps the directory's spelling
  (`features/Corpses/...` in one archive, `features/corpses/...` in another) and the merge is a case-sensitive
  `std::set`; the second insert is ignored, so no value changes. Audit: `BT-UTIL-HPI-PARITY.md` rule 5.
- **The loading screen's five bars never move** (`LoadingScene.cpp` about 79, "for demo/debugging purposes").
  Presentation only. Audit: `BT-GAME-DATA-FILES-PARITY.md` rule 4.

### Re-confirmed, not new (Batch 10)

- Pause halts only the tick accumulator and the single-player menu pause is the register's section 70
  (`GameScene.cpp` about 594-606, `GameScene_menu.cpp` about 281-296); the campaign's two players, colours 0 and 1
  (`MainMenuScene_campaign.cpp` about 330-334), match `docs/TOTALA-EXE-DATA.md` section 115; the `0x08` loading-started
  byte matches `docs/TA-NETWORK.md`.
- Loose files before archives, archives in mount order, case-insensitive component lookup, a directory skipped in
  favour of a later archive's file, and the outer obfuscation arithmetic all agree with the chapter; every one of the 30
  retail archives decodes with RWE's key handling.
- `toDiscreteRect` is applied to six of the thirty rectangles and the four reversed ones (the unit make and use fields,
  `x1` 350 or 400, `x2` 170) are drawn raw, which is what the register's anchor reading needs.
- `32xlogos` has ten frames, indexed by player colour, in both the HUD and the lobby; the fog tile names, the in-game
  title entries and `radlogo` are fetched where used.

## Batch 11 (2026-10-17)

Module chapters `game-game-state`, `ingame-keys`, `ingame-markers`, `ingame-camera`, `ingame-dialogs`,
`game-share-commands` and `game-game-load`. Same reference commit and the same evidence class (static reading); the
shipped `gamedata/HELP.TDF` and `allsound.tdf` were read through `tools/hpi.py` for the key help text and the squad
sounds. The audits are `BT-GAME-GAME-STATE-PARITY.md`, `BT-INGAME-KEYS-PARITY.md`, `BT-INGAME-MARKERS-PARITY.md`,
`BT-INGAME-CAMERA-PARITY.md`, `BT-INGAME-DIALOGS-PARITY.md`, `BT-GAME-SHARE-COMMANDS-PARITY.md` and
`BT-GAME-GAME-LOAD-PARITY.md`. Neither side is chosen; kinds as above. Items are numbered from 105; if another author
has appended items, renumber on merge. Item 105 touches the own-clock TA host only (the lockstep game carries the
speed as a command); the rest are presentation or interface. `TOTALA-EXE-KEYBOARD.md` sections 79 and 80 ("what RWE
binds today" and the gap table) are stale against the tree and are not listed as conflicts.

105. **The `0x19` pause and speed payload** (register inference versus reference). Register: `TA-DEMOS.md` and
     `TA-NETWORK.md` read the two bytes after the type as one 16-bit number in 256ths of normal speed (zero a pause,
     256 normal; every sender puts 256 on the wire at game start), and `GameScene_tahost.cpp` (about 313-331) decodes a
     joining TA's packet that way; `TA-NETWORK.md` marks the scale "the one to check against a real TA". Reference
     (`ingame-keys` "Escape, self-destruct and speed rules", `game-game-state` "Requested and effective game speed"):
     three bytes, **type, subtype, value**: subtype zero is the pause with the new pause bit, subtype one is the speed
     with the clamped level of one to twenty. On the reference's layout the corpus's game-start record (the bytes 256)
     is subtype zero with value one, a pause-on, and a speed change to level 12 is the number 3073, which RWE's decoder
     turns into level 20 while reading a pause-on as normal speed. No capture of a real speed change exists. Kind:
     register. Audits: `BT-INGAME-KEYS-PARITY.md` rule 7; `BT-GAME-GAME-STATE-PARITY.md` rule 4.

106. **What Ctrl+A selects** (register and help text versus reference, with RWE a third reading). Register
     (`TOTALA-EXE-KEYBOARD.md` section 73) and the shipped help line "CTRL+A Select all units": the whole unit array,
     every own living unit. Reference (`ingame-keys`, the key table): "select all **idle** units". RWE selects every
     living own unit whose definition `canMove` (`GameScene_input.cpp` about 311-316), which leaves out buildings and
     factories; the commit that wrote it says "selects everything you own". Kind: register. Audit:
     `BT-INGAME-KEYS-PARITY.md` rule 4.

107. **Ctrl+D on a mixed selection** (design versus reference). Reference (`ingame-keys`, "Escape, self-destruct and
     speed rules"): the key deletes any existing self-destruct orders it finds in the selection and issues a new one to
     the whole selection **only when none existed**, so a mixed selection cancels the counting units and arms none of
     the others. RWE sends one command per unit and the simulation toggles each (`GameSimulation::toggleSelfDestruct`,
     about 1332), so the units without a timer are armed too. The register describes the weapon lookup only. Kind:
     design. Audit: `BT-INGAME-KEYS-PARITY.md` rule 6.

108. **Who may change the game speed** (design versus reference). Reference (`ingame-keys`; `game-game-state`): the
     key works for any non-watcher, with a broadcast, and "there is no additional host-only test". RWE drops a speed
     command not issued by player 0 (`GameScene_commands.cpp` about 3114). The reference does not say what a receiver
     does with a non-host's packet. Kind: design. Audits: `BT-INGAME-KEYS-PARITY.md` rule 7;
     `BT-GAME-GAME-STATE-PARITY.md` rule 4.

109. **Squad sounds and the digit mapping** (roadmap versus register and reference). `docs/ROADMAP.md` (#125) says "a
     sound on assignment" is "ProTA's rather than TA's"; `TOTALA-EXE-KEYBOARD.md` section 73 ("CreateSquad" sound) and
     the reference (`ingame-keys`: squad create plays CreateSquad, bookmark save and recall play SelectSquad) both say
     the original plays it. Both names are `beep4` in the shipped `allsound.tdf`; RWE looks up neither. Separately, the
     reference's Shift on a squad recall *adds the squad to the selection*, where RWE's Shift+digit *adds the
     selection to the group*. Kind: design (roadmap sentence) and audit. Audit: `BT-INGAME-KEYS-PARITY.md` rule 3.

110. **Which sliding panel F4 moves** (register inference versus reference). Register: `TOTALA-EXE.md` section 76
     inferred that the slide position at `0x51F2D8` moves the side panel; `TOTALA-EXE-INTERFACE.md` section 108
     corrected it for Space and left "what F4 does to the side panel in the original" unchecked. Reference
     (`ingame-dialogs`, "Sliding score panel"): the only reader of that slide position in the chapter is the
     score-panel draw, which places the player list at the screen width less the slide. RWE still slides the side panel
     and the minimap on the F4 latch (`GameScene.cpp` about 455-510). Weak: absence of a reader, not a finding. Kind:
     register. Audit: `BT-INGAME-DIALOGS-PARITY.md` rule 6.

111. **Which players the score panel lists** (register versus reference). Register (`TOTALA-EXE-INTERFACE.md` section
     108): a row is skipped for "a record that has and has had no units". Reference (`ingame-dialogs`): a row
     qualifies unless the player is "**eliminated after having created units**", with the ranks of the higher players
     closed up by the draw. The two skip opposite ends (never played versus played and lost). RWE keeps a player who
     has lost, and says so in a comment ("how the losses show"). Kind: register and design. Audit:
     `BT-INGAME-DIALOGS-PARITY.md` rule 6.

112. **The score panel's background** (register versus reference). Register (section 108): the box is `40 x rows +
     46` tall and no fill showed in a screenshot; the decoded height is 30 pixels more than the plates need.
     Reference: a **faded** background sized `numPlayers x 40 + 14`. A faintly translucent fill could be invisible on
     a dark map. RWE draws none. Kind: register. Audit: `BT-INGAME-DIALOGS-PARITY.md` rule 6.

113. **The music evaluator's ring order** (register ambiguity versus reference). Register
     (`TOTALA-EXE-MUSIC.md` section 47): a ring of thirty buckets, "each run advances the ring and zeroes the new
     slot", the recent figure is "the 5 most recent slots"; the order of advance and sum is not stated. Reference
     (`ingame-dialogs`, "Combat activity and CD category selection"): the sums run **before** the advance, the thirty
     total walks all buckets starting just before the write index, and the recent figure is the first five met, so it
     excludes the bucket for the second that just ended. RWE advances and clears first and then sums
     (`GameScene_audio.cpp` about 365-381): its totals cover 29 completed seconds and 4 completed seconds plus an empty
     bucket. The reference's "more than ten" and "above sixty" samples are strict, against the register's "at least".
     Kind: register. Audit: `BT-INGAME-DIALOGS-PARITY.md` rule 7.

114. **Is a typed `+command` also sent as chat?** (design versus reference). RWE never sends a line that parses as a
     command (`GameScene_chat.cpp` about 78-83, a comment citing the original). Reference (`ingame-dialogs`, "Chat
     target controls"): command execution "does not automatically consume the text from the later chat path", and a
     command whose result carries the cheat bit forces the send mode to **all**, which makes sense only if the text is
     then sent. `TOTALA-EXE-COMMANDS.md` section 124 says only that the rest is handed to the interpreter locally.
     Kind: design. Audit: `BT-INGAME-DIALOGS-PARITY.md` rule 3.

115. **The shape of the target ring** (register versus reference). Register (`TOTALA-EXE-RENDER.md` near 553): a flat
     16-segment ring of radius `(int)(footprintRadius x 0.89)`. Reference (`ingame-markers`, "Target ellipse"): an
     ellipse with **horizontal radius equal to the definition field** and the vertical radius truncated from 0.89 times
     it, and no visibility or cache lookup for its target. Kind: register. Audit: `BT-INGAME-MARKERS-PARITY.md`
     rule 7.

116. **When the CD default track types apply** (register versus reference). Register (`TOTALA-EXE-MUSIC.md` section
     44): the shipped defaults apply on a miss "if, and only if" the disc has sixteen tracks and a data first track.
     Reference (`game-game-state`, "Persistent CD category cache"): the sound object gets them only then, but the
     **cached record** gets the first sixteen default bytes on every miss, so a non-matching disc loads them on the
     next reopen. The outcome RWE chose (tracks 2-7 Battle, 8 upward Building) is the result of the reference's
     reading. Kind: register. Audit: `BT-GAME-GAME-STATE-PARITY.md` rule 5.

### Probable code faults found on the way (not reference conflicts)

- **A camera bookmark recall, F3 and `n` do not leave a tracking camera.** `setCameraPosition` sets the position only;
  while a unit is tracked, `GameScene::update` pulls the camera back toward it on the next frame. The reference's
  restore clears the follow and snap state. `relocateCamera` and `nudgeCamera` do leave tracking. Not run. Audit:
  `BT-INGAME-CAMERA-PARITY.md` rule 5.
- **An order marker follows a hidden target.** `renderUnitOrders` draws the marker (and the trail) at
  `tryGetUnit(target)->position`, the true position with no visibility test, where the original freezes the marker at
  the last position seen (`TOTALA-EXE-RENDER.md` near 546). Information leak, client-side only. Audit:
  `BT-INGAME-MARKERS-PARITY.md` rule 5.
- **`+ShareRadar` and `+ShareMapping` print "allies already share their radar/map in RWE".** The radar half has been
  false since `ba7000e8` (BT-139). Neither command has state. Audit: `BT-GAME-SHARE-COMMANDS-PARITY.md` rule 3.
- **A prefixed private chat line reaches everyone.** There are no recipient modes, so `A,` / `E,` / `3:` go to all
  players, prefix included. Audit: `BT-INGAME-DIALOGS-PARITY.md` rule 3.
- **Ctrl+Shift+letter selects nothing.** Every Ctrl selection requires Shift up, and only six category letters are
  handled; the reference's category selection takes the Shift state as "add" and forms the key from any letter.
  Audit: `BT-INGAME-KEYS-PARITY.md` rule 4.

### Re-confirmed, not new (Batch 11)

- Wind: the retained-state reading, the 5-to-14-second interval, the denominator 5000 with an upper clamp only, the
  generator hook's scale and the counter anchor (the two schemes change at the same ticks) agree with BT-26, BT-46 and
  BT-130 and `TA-SYSTEMS-PARITY.md` rule 10; the speed draw range and the direction at zero speed remain the open rows.
- The queued-build footprint animation (ten ticks, eight lines, the selected-owner colour pair, linear travel), the
  path trail (48-unit spacing, 1.6 units a tick, straight 3D line, one frame, trail for the primary unit only) and the
  marker cursor table agree with `TOTALA-EXE-RENDER.md` sections 26 to 28.
- The share-command table, the network-game gate, the reserves capped at capacity and printed uncapped, `+ShareAll`
  inverting rather than setting, and the unregistered `+ShareLOS` agree with `TOTALA-EXE-COMMANDS.md` section 124.
- The player-list slide, its 125-pixel width and Space/F4 gate, the 30 and 5 points of the activity ring and the
  unit-count gate of thirty agree with `TOTALA-EXE-INTERFACE.md` section 108 and `TOTALA-EXE-MUSIC.md` section 47.
- The camera bookmarks are scene state, are not saved and start empty each game; a save carries the position, clamped
  on load; the radar-click centring leaves a tracking camera, as the reference requires.
- The start-slot deal uses the occupied slot numbers as the position set, sparse sets included, as the reference does.

## Batch 12 (2026-10-17)

Module chapters `game-victory`, `frontend-endgame`, `game-statistics`, `frontend-skirmish-menu`,
`frontend-campaign-menu` and `frontend-frontend`. Same reference commit and the same evidence class (static reading); the
shipped `camps/*.tdf` of `ccdata.ccx` and the loose `camps/EXAMPLE.TDF` were read through `tools/hpi.py`'s reader and a
directory listing. The audits are `BT-GAME-VICTORY-PARITY.md`, `BT-FRONTEND-ENDGAME-PARITY.md`,
`BT-GAME-STATISTICS-PARITY.md`, `BT-FRONTEND-SKIRMISH-MENU-PARITY.md`, `BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` and
`BT-FRONTEND-FRONTEND-PARITY.md`. Neither side is chosen; kinds as above. Items are numbered from 117; if another author
has appended items, renumber on merge. None of these reaches the simulation's hashed state: the victory tests, the
statistics and the screens are presentation, with the exception of the launch checks (item 121), which decide what
`GameParameters` the lobby may hand to the loader. BT-82, BT-83 and BT-89 were read and are not re-raised.

117. **Is the screen fade one step a tick or one every two?** (register versus reference). Register
     (`TOTALA-EXE-INTERFACE.md` section 104): ten steps, "one a tick", "a third of a second". Reference
     (`frontend-endgame`, "Screen and palette fades"): the step runs when the stored deadline is **strictly less** than
     the current tick and reschedules to now plus one, so a step falls on every second tick when the loop runs faster
     than the clock: twenty ticks. The palette fade in the same chapter uses "at least", one per tick. RWE follows the
     register (`EndGameFadeTicks = 10`). Kind: register. Audit: `BT-FRONTEND-ENDGAME-PARITY.md` rule 3.

118. **How long the victory or defeat banner shows before the fade** (design versus register and reference). RWE holds
     it 90 ticks (`EndGameBannerTicks`, `GameScene_endgame.cpp` about 84), commenting that the original's banner "is a
     flag on the world renderer and the player dismisses it". The register's state table has no wait before the fade
     (state 1 waits only for a message box) and the reference's state 0 selects state 2 at once. Neither source says how
     many frames the world renderer draws the banner before the machine starts. Kind: design. Audit:
     `BT-FRONTEND-ENDGAME-PARITY.md` rule 3.

119. **A simultaneous wipe: draw or defeat** (audit versus register and reference). `BT-GAME-PLAYERS-PARITY.md` rule 6
     lists Draw as "UNCLEAR: neither the chapter nor the register says". With BT-82's order (a non-campaign game checks
     defeat before victory, unless the watching flag is set) and `game-victory`'s tests (the local count at zero is a
     defeat, and the allied-victory test never looks at the local count), the machine whose last unit dies in the same
     observed second as the last enemy's is **defeated**; the original has no draw state. RWE returns
     `WinStatusDraw` (`GameSimulation.cpp` about 2678). Kind: audit. Audit: `BT-GAME-VICTORY-PARITY.md` rule 7.

120. **What dismisses the glamour picture and speeds the chart up** (register versus reference). Register (section 104):
     "a click during the run-up" (`0x4C1AB0` tested at `0x41FFD9`). Reference (`frontend-endgame`, states 6 and 7): "a
     popped key" (and, for the picture, `cursorKeyFlags`). RWE responds to the left mouse button only, and Return opens
     the chat bar over both screens. Both sources may be true. Kind: register. Audit:
     `BT-FRONTEND-ENDGAME-PARITY.md` rules 4 and 5.

121. **Which launch checks the skirmish Start button makes** (design versus reference). Reference
     (`frontend-skirmish-menu`, "Launch validation"): the map must load; at least one computer **and** one human must
     exist; the player count must not exceed the map's start positions; and a setup whose active slots all share one
     allegiance group other than "none" is refused. RWE checks only a selected map and a start position **for each
     occupied slot number** (`startPositionProblem`, `MainMenuScene.cpp` about 2012), and nothing on the counts or the
     allegiance. Kind: design. Audit: `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 7.

122. **Which new-game layout NewCamp opens** (register versus reference, no proven conflict). Register
     (`TOTALA-EXE-DATA.md` section 115): NewCamp calls the builder with argument one (the any-mission mode with both
     lists) and the zero mode is dead. Reference (`frontend-campaign-menu`, "New-game layouts"): a zero argument counts
     the campaign files and, at two or fewer, shows a simplified screen (side, difficulty, Start; the literal
     `Arm Campaign` or `Core Campaign`); the chapter does not say which argument substate ten passes. The retail
     install has exactly two packaged campaign files, so the simplified screen would be the retail one if the zero mode
     were reachable. RWE follows the register. Kind: register. Audit: `BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` rule 4.

123. **Does the main menu quit on a cancel?** (design versus reference). `goToPreviousMenu` calls `exit()` when only the
     main menu is on the stack (`MainMenuScene.cpp` about 637). The reference's main-menu state acts on five named
     buttons and clears the selection for anything else. Whether a cancel can reach it in RWE was not checked. Kind:
     design. Audit: `BT-FRONTEND-FRONTEND-PARITY.md` rule 2.

### Probable code faults found on the way (not reference conflicts)

- **A team victory is shown as a defeat to the teammate with the higher player id.** `computeWinStatus` names one
  winner (the lowest id of the winning team); `localPlayerWon` asks whether that id is the local player
  (`GameScene_endgame.cpp` about 540). Traced by reading, not run. Audit: `BT-GAME-VICTORY-PARITY.md` rule 7.
- **The score's clock term divides the game time by 30; the corrected register and the reference say 60.**
  `EndGameStats.cpp` about 30 and `EndGameStats.test.cpp`. Section 104 was corrected on 2026-09-24 and the code was not.
  Invisible on shipped maps (`timemul` is 0). Audit: `BT-FRONTEND-ENDGAME-PARITY.md` rule 5 and
  `BT-GAME-STATISTICS-PARITY.md` rule 4.
- **The results chart reads the statistics when it opens, with the simulation still ticking.** Nothing gates the tick
  dispatch on `gameOver`; `gameOverTime` is written and never read. Kills, losses and the production and excess columns
  are therefore read some seconds after the decision, where the original fixes them at the shutdown. Traced by
  reading, not run. Audit: `BT-FRONTEND-ENDGAME-PARITY.md` rule 2.
- **Two seats on one team start a game that is won on the first tick.** `computeWinStatus` returns Won when every living
  player shares a team, and the skirmish screen has no check that refuses such a setup. Traced by reading, not run.
  Audit: `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 7.
- **A key press does not dismiss the glamour picture.** Return opens the chat bar over it. Audit:
  `BT-FRONTEND-ENDGAME-PARITY.md` rule 4.

### Stale register text found on the way

- `TOTALA-EXE.md` section 91, first bullet: the campaign's win and lose rules (section 113) are **ported**
  (`MissionRules`, installed by the loader and ticked by the simulation).
- `TOTALA-EXE.md` section 91, last bullet: "TA's **Permanent** LOS mode has not been looked at" is stale; the simulation
  has the mode (`LineOfSightMode::Permanent`, `gameoptions.test.cpp`).

### Re-confirmed, not new (Batch 12)

- All eleven victory and seven defeat predicates, the polling order, victory-before-defeat, the dying unit being counted
  by the kill rules, the capture as two events, the ANYTYPE wildcard in only three rules, the letters-only name scan and
  the two default rules agree with `TOTALA-EXE-DATA.md` section 113 and `MissionRules`.
- The results chart's seven columns, floors (10, 10, 100 ...), geometry (x 112 and a stride of 74, rows from y 93 at a step
  of 20), the ten-tick column delay, the fifteen-step bars and the ENDMSN next-or-retry rule (a lost final mission still
  offers retry) agree with `TOTALA-EXE-INTERFACE.md` section 104 and `TOTALA-EXE-DATA.md` section 115.
- The thumb bytes of the mission list (`0xff` lost, `0xfe` won, `0xfd` untried), the planet table, the Lunar-on-Core
  substitution, the colour 0 and 1 for the two campaign players, the resource stepping (200, 500, 1000 ... 10000) and the
  stage-to-rule mapping of the skirmish buttons agree with the reference.
- The reference's INTRO button sets its loop flag from the Shift key; this closes the open question about the loop flag
  in `BT-GRAPHICS-MOVIES-PARITY.md` rule 5, and the single-player menu's `AllMissions` bit (which only reveals AnyMsn)
  closes the open row in `BT-GAME-SETTINGS-PARITY.md` rule 6.

## Batch 13 (2026-10-17)

Module chapters `network-net-chat`, `network-packets`, `network-hapinet`, `network-unit-sync`, `network-net-condenser`,
`network-net-stats`, `frontend-options` and `game-savegame`. Same reference commit and the same evidence class (static
reading). RWE's own lockstep multiplayer is RWE-original and was **not** audited against the reference; only RWE's
TA-compatible networking (`net/ta/`, `TaHostGame`, `GameScene_tahost.cpp`, `docs/TA-NETWORK.md`, an executed-against-real-TA
record that outranks a static reading), the TA demo reader where packet layouts overlap (`io/tad/`, `docs/TA-DEMOS.md`)
and, for options and savegame, the UI behaviour. The audits are `BT-NETWORK-NET-CHAT-PARITY.md`,
`BT-NETWORK-PACKETS-PARITY.md`, `BT-NETWORK-HAPINET-PARITY.md`, `BT-NETWORK-UNIT-SYNC-PARITY.md`,
`BT-NETWORK-CONDENSER-STATS-PARITY.md` (two chapters), `BT-FRONTEND-OPTIONS-PARITY.md` and `BT-GAME-SAVEGAME-PARITY.md`.
Neither side is chosen; kinds as above. Items are numbered from 124; if another author has appended items, renumber on
merge. None of these reaches the simulation's hashed state: the HUD ring, the TA wire layer, the options pages and the
save screens are presentation, transport or host-side rules. BT-176, BT-177 and BT-178 were read and are not re-raised.

124. **How long a HUD message stays, and how many lines** (design and register versus reference). Reference
     (`network-net-chat`, "Expiration"): the oldest line goes when its tick plus `(textScroll + 1) * 30` is strictly less
     than the game tick, one per call; the Options text delay defaults to ten (`BT-GAME-SETTINGS-PARITY.md` rule 6), so an
     untouched install keeps a line for **eleven seconds**, and the default line count of ten shows **nine** lines. RWE keeps
     every console line **five seconds** and **eight** lines (`printConsole`). The code comment and
     `BT-INGAME-DIALOGS-PARITY.md` rule 8 call five the original's behaviour; nothing in the register gives five for the
     message list (the only five there is the self-destruct countdown, `TOTALA-EXE-ECONOMY.md` near 311). Kind: design.
     Audit: `BT-NETWORK-NET-CHAT-PARITY.md` rule 4.

125. **Is a received `0x2c` held for a due tick?** (register versus reference). Register (`TA-NETWORK.md`, "How a peer's
     `0x2c` is received", `TOTALA-EXE-MOVEMENT.md` section 118, plus a screen-recording measurement): the record is
     applied the moment it arrives, "nothing is queued and no clock is compared". Reference (`network-packets`, "Splitting
     commands and smoothing delivery ticks"): the receiver splits a bundle into commands with **due ticks** and, with
     smoothing enabled, spreads them across ticks by the packet spacing; the chapter does not give the smoothing switch's
     default or say which kinds use the queue. Both can hold if smoothing is off in the shipped game or the unit-state case
     bypasses the queue. Kind: register. Audit: `BT-NETWORK-PACKETS-PARITY.md` rule 7.

126. **What the `0x0001` and `0x0101` unit-sync values mean, and who reads the second checksum** (register wording versus
     reference). Register (`TA-NETWORK.md`, "Unit sync"): the host's first record per id is "status `0x0001`", the answer
     "`0x0101`: in use"; `TaChecksums` supplies the first checksum only and the register says "a TA host checks a joiner's
     CRCs". Reference (`network-unit-sync`): the value is a permission byte, an availability byte and a signed 16-bit limit
     (`0xffff` is minus one); `0x0001` is availability **zero** (the join-time recheck, before the joiner has reported),
     `0x0101` availability one; the second checksum at offset 10 is compared by the authority only when the remote's
     version is at least 2 (or 1 with a minor of at least 2). The counts agree (`1 + 2n`); the meaning and the consumer
     differ in wording. `BT-GAME-DATA-FILES-PARITY.md` gap 7 asked who reads the script checksum; this is the answer.
     Kind: register. Audit: `BT-NETWORK-UNIT-SYNC-PARITY.md` rules 3 and 4.

127. **Is a host's Start gated on unit-sync completion?** (design versus reference, unclear). Reference: three completion
     tests exist (all-players-synced, per-player, status text) and "the front-end coordinator decides when to finish and
     apply"; no chapter says the Start button is disabled until they pass, and the handshake has no timeout. RWE's
     `TaBattleroom::launch` checks only that someone joined and that the teams differ. Kind: design. Audit:
     `BT-NETWORK-UNIT-SYNC-PARITY.md` rule 5.

128. **Does a live TA send ally chat as `0xF9`?** (register versus reference, no proven conflict). Register
     (`TA-DEMOS.md`, `tad_events.h`): `0xF9` is a 73-byte ally chat record (two DirectPlay ids and a 64-byte body), read off
     three corpus demos, and RWE prints it with an "(ally)" prefix on a live peer too. Reference (`network-packets`): the
     live receiver scans only command kinds two to forty-four and stops at any other, so a live TA would not walk a `0xF9`;
     BT-176 gives the recipient modes without a distinct wire code. `0xF9` may be a recorder construct, with allied chat on
     the wire being a `0x05` sent to the allies only. Kind: register. Audit: `BT-NETWORK-NET-CHAT-PARITY.md` rule 6.

129. **Which of the two version shorts in a player's 21-byte data block is the major** (register versus reference,
     unclear). Register (`TaHostSession.cpp`, copied from the capture): the host's block is zeros with byte 19 set to
     `0x50`, i.e. the pair (0, `0x0050`). Reference (`network-hapinet`, `network-unit-sync`): the block is a 17-byte name
     field then two 16-bit version values; the second-checksum compare applies only to a remote at version 2, or 1.2, and
     above. Neither says which short is the major, so whether a real host's captured pair falls above or below the threshold
     is unknown, and with it whether RWE joining a TA host must compute the script checksum. Kind: register. Audit:
     `BT-NETWORK-HAPINET-PARITY.md` rule 8 and `BT-NETWORK-UNIT-SYNC-PARITY.md` rule 4.

130. **What Default and Undo restore, and what the sound test plays** (design versus reference). Reference
     (`frontend-options`): each page's Default and Undo restore **that page's** subset (sound, music, UI, visual) and the
     panel's Cancel restores the whole snapshot; `TEST` plays `sounds/explode.wav`. RWE's per-page `RESTORE` and `UNDO` apply
     the whole `GameOptions` to every page, and `TEST` plays the `BUTTON10` click. Kind: design. Audit:
     `BT-FRONTEND-OPTIONS-PARITY.md` rules 3 and 6.

### Probable code faults found on the way (not reference conflicts)

- **A TA player's chat prints with the name twice.** The corpus text is `<Albert-the_8th> wtf isw this`, the label inside the
  wire text; `printChatLine` prefixes the sender's name again (`GameScene_tahost.cpp` about 304, `GameScene_tad.cpp` about
  56). Traced by reading, not run. Audit: `BT-NETWORK-NET-CHAT-PARITY.md` rule 6.
- **Chat typed in a TA-hosted game never reaches the TA player, and the typist is told nothing.** No `0x05` encoder exists;
  `submitChatMessage` posts to the lockstep endpoints, there are none in own-clock mode, it returns success and the line is
  echoed locally. Traced by reading, not run. Audit: `BT-NETWORK-NET-CHAT-PARITY.md` rule 6.
- **A packet with a bad checksum is parsed and applied on the live TA path.** `taParsePacket` computes `checksumValid` and
  neither live caller (`TaBattleroom::handleAppData`, the in-game handler in `TaHostGame.cpp`) reads it; the original
  rejects the packet. Audit: `BT-NETWORK-CONDENSER-STATS-PARITY.md` rule 5.
- **The hosted session's instance GUID is a constant** (`TaHostConfig::instanceGuid`, the captured bytes), never
  regenerated: two RWE hosts, or a restarted one, are one session to a TA. Audit: `BT-NETWORK-HAPINET-PARITY.md` rule 5.
- **A broadcast takes one marker value per peer** (`TaOutboundBatcher::queueForAll`), so with two or more joiners each TA
  sees gaps in the falling sequence; the original numbers a broadcast once. Latent: the game hosts one joiner. Audit:
  `BT-NETWORK-PACKETS-PARITY.md` rule 4.
- **Session, game and host-player names are uncapped**; a real TA copies the session name into a 32-byte field with no
  length check and the player name into 17 bytes. Audit: `BT-NETWORK-HAPINET-PARITY.md` rules 5 and 8.
- **The unit-sync echo answers at most 512 ids a message**, below the 549 types of the Escalation data set. Audit:
  `BT-NETWORK-UNIT-SYNC-PARITY.md` rule 3.
- **Restart, Save and Load are available in a network game.** `openGameExitMenu` enables Restart unconditionally (its
  comment, "no multiplayer game in progress to gate it on", is stale) and `restartGame` and `loadSavedGame` leave the
  scene for one player. Audit: `BT-FRONTEND-OPTIONS-PARITY.md` rule 11.
- **A save name containing `* ? " < > |` fails to write and is reported as saved**: `UiTextBox` admits any printable
  ASCII and `saveFileStem` folds only `/`, `\`, `:` and controls. Audit: `BT-GAME-SAVEGAME-PARITY.md` rule 4.
- **Choosing a damaged save from the front end does nothing and says nothing** (`MainMenuScene::startLoadedGame` returns
  silently). Audit: `BT-GAME-SAVEGAME-PARITY.md` rule 8.

### Stale register text found on the way

- `TA-NETWORK.md`, "Unit sync": `0x0101` is called "in use"; it is permission and availability (item 126).
- `TA-NETWORK.md`, "RWE's own-clock mode" and "Open": the `0x19` payload is still read as two bytes in 256ths of normal;
  BT-178 (resolved) has three bytes, a subtype and a value. Already tracked; the code and the text both wait on it.
- `TA-NETWORK.md`, "Open", the DirectPlay player-info blob: "nothing says whether DirectPlay would take a longer name" is
  answered by `network-hapinet` (a 17-byte field, sixteen characters bounded), see `BT-NETWORK-HAPINET-PARITY.md` rule 8.
- `TA-NETWORK.md`, "Why a TA flushes a packet early": the six-tick message is TA's **default send pacing** (200 ms at
  thirty ticks a second), not a property of the `0x2c` stream (`BT-NETWORK-PACKETS-PARITY.md` rule 3).
- `TOTALA-EXE-KEYBOARD.md` line 394: F3 "missing" is stale; it is wired to the last attack **position**
  (`BT-INGAME-KEYS-PARITY.md` rule 8 already says so; this adds the original's cycling algorithm).
- `BT-INGAME-DIALOGS-PARITY.md` rule 8: "the expiry of old messages is `printConsole`'s five seconds" has no source (item
  124).
- `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md` gap 2: the path-separator half is now answered by `saveFileStem`.

### Re-confirmed, not new (Batch 13)

- The envelope's byte ranges (XOR and sum over payload indices three to total minus four inclusive, the last three payload
  bytes untouched), kind bytes three and four, little-endian 16-bit checksum, and pass-through of other kinds agree with
  `tadEncrypt`, `tadDecrypt` and `TaPacket`, and with the zero checksum failures over the captures in `TA-NETWORK.md`.
- The bundle sequence starts at minus two and falls by one; a reply and an unsequenced send carry minus one (`TaMarkerCounter`,
  `TaReplyMarker`).
- The 14-byte `0x1a` layout, the sequence field written as zero, the `0xffff` limit as minus one, and the `1 + 2n` count
  agree with `taBuildUnitSyncEcho` and with `TA-NETWORK.md` (the counter reaching 557).
- The session description (80 bytes, flags zero, TA's application GUID, no password bit), the options byte on top of the
  first user value with the player count at the bottom, ten player slots, and the battleroom on TCP with the game on UDP
  agree with `TaSessionDescription` and the captures.
- The bit stream (fields low bits first in little-endian words) agrees with the `0x2c` decoder and encoder, which are also
  bounds-checked where the original is not.
- OK saves and Cancel restores a snapshot taken when the options panel opened; the F2 menu pauses only a non-multiplayer
  game; the gamma range (`0.5 + g/24`) agrees.

## Batch 14 (2026-10-17)

Module chapters `gui-gui`, `gui-gui-file`, `gui-listbox`, `gui-dialogs`, `gui-file-list` and `game-console-commands`. Same
reference commit and the same evidence class (static reading). The GUI half was additionally censused: every `.gui` of the
seven retail archives (186 distinct paths, later archives overriding earlier ones) through `tools/hpi.py`'s reader and a
throw-away TDF reader kept outside the tree, so the counts below are of the shipped data. The audits are
`BT-GUI-GUI-PARITY.md`, `BT-GUI-GUI-FILE-PARITY.md`, `BT-GUI-LISTBOX-PARITY.md`, `BT-GUI-DIALOGS-PARITY.md`,
`BT-GUI-FILE-LIST-PARITY.md` and `BT-GAME-CONSOLE-COMMANDS-PARITY.md`. Neither side is chosen; kinds as above. Items are
numbered from 131; if another author has appended items, renumber on merge. None of these reaches the simulation's hashed
state except item 131, which is about whether a port would have to. BT-176 and BT-177 were read and are not re-raised.

131. **Do the mask-one console commands touch the simulation?** (register versus reference). Register
     (`TOTALA-EXE-COMMANDS.md` section 124, "Other registered commands"): their "effects are options and display and none
     touches the simulation", naming `Selectable`, `Logo`, `NoMetal`, `NoEnergy` and `BigBrother`. Reference
     (`game-console-commands`, "Ordinary commands: players, resources"): `NoMetal` and `NoEnergy` **assign a player's stockpile
     outright** (the player from argument one, the amount from argument **two**, so `+NoMetal 3` assigns zero to player 3),
     `Selectable` writes a flag on units, `Logo` writes a player field, `Give` moves resources. A lockstep port that took
     section 124 at its word would desync the first time one was typed. Kind: register. Audit:
     `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 5.

132. **`Selectable`: which units, which flag** (register versus reference, detail). Register: "sets selectable on every live
     unit". Reference: adds flag `0x20` to units already carrying `0x10000000`; "it does not simply select every unit".
     Audit: `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 4.

133. **Is the cheat mask gated by the lobby's "Cheat Codes" option?** (register wording versus reference, unclear). Register
     and `BT-INGAME-DIALOGS-PARITY.md` rule 3: the talk dialog adds the cheat bit "in non-campaign play". Reference: the same
     words. The lobby has a `CHEATING` button (`LOUNGE2.GUI`, caption "Cheat Codes", a linked label) and the host record
     carries a cheating state (`BT-GAME-GAME-LOAD-PARITY.md`); nothing says the dialog reads it. If it does not, every
     player of every skirmish or network game can type `+ATM` and `+View`. RWE has no human cheat at all, so this changes
     nothing now. Audit: `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 2.

134. **`+Give`: which account the word moves, and what the player argument counts** (register versus reference, unclear).
     Reference: "metal" calls `TransferEnergy` and "energy" calls `TransferMetal`, with a warning not to reverse the
     documentation; the register (section 124, "The packet") says the **source's routine names are crossed** ("the routine
     called metal moves energy accounts"), which would make the word mean what it says. The two readings agree only if
     the crossed names are the same crossing. Separately the reference takes the target as a **slot index from zero**; RWE
     counts `Player n` from one (or a name), so `+Give 1 100 metal` reaches a different player. Audit:
     `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 5.

135. **`YESORNO` gadget names** (register versus reference). Reference (`gui-dialogs`, "Named dialog constructors"): the
     constructor fills `CHC1`, `CHC2` and `TITL`. The shipped `YESORNO.GUI` (the only copy, `totala1.hpi`) names its four
     records the panel, `CHOICE1`, `CHOICE2` and `TITLE` (attribute 2, x 25, width 350), which `TOTALA-EXE-INTERFACE.md`
     section 63 also uses. Either the chapter abbreviates, or its lookup is a substring match that would not find these,
     or its data differs. Audit: `BT-GUI-DIALOGS-PARITY.md` rule 5.

136. **Button attribute `0x08`: a caption shadow, a toggle, or both** (register versus reference, incompleteness). Register
     (section 99, `0x4A59A4`): the caption's drop shadow is drawn only when the gadget's attribs carry bit 3, and RWE names
     the bit `CaptionShadow`. Reference (`gui-gui`, button input): the same value is **toggle a zero/one status on release
     inside**. The shipped data gives it to exactly the toggle-like buttons (28: the `CHATSEL` recipient buttons, the
     lobby's `READYx` and `ALLYx`, `TALK`/`TALK2`'s `SENDTO` and `PLAYERx`), which supports the second; the readings are
     compatible and RWE implements only the first. Audit: `BT-GUI-GUI-PARITY.md` rule 5.

137. **Which glyph cut labels use** (register versus reference, unclear). Reference: a label draws in glyph slot one and
     restores slot zero; the application loads `hattfont12` and `hattfont11` into the two slots. Register
     (`TOTALA-EXE-INTERFACE.md` section 108): the Space strip uses `hattfont11`, "the list uses `hattfont12`", and a play-test
     named "the beige outlined font the gui labels use" without a size. RWE draws every label, button, list and input in
     `hattfont12`. If label-slot is the smaller cut, every RWE label is one size too large. Audit: `BT-GUI-GUI-PARITY.md`
     rule 7.

138. **The briefing's blink parameters against the blink word's fields** (audit versus reference, inference). The
     briefing audit (`BT-FRONTEND-CAMPAIGN-MENU-PARITY.md` rule 5) reads a span as registered with "period 0x5e, duration 1.0
     and phase 0.25". The blink word has **two colours and two durations** and a phase that toggles between them. A plausible
     mapping is colour B = `0x5E`, duration A = 1.0 s, duration B = 0.25 s; neither chapter lists the argument order.
     Audit: `BT-GUI-FILE-LIST-PARITY.md` rule 5.

139. **Placement of an overhanging dialog: slide or recentre** (design versus reference). Reference: an extent off the right or
     bottom triggers **recentring**; RWE slides the panel left or up by the overhang. Two shipped boxes overhang:
     `MULTIMSG.GUI` and `SKIRMISHMSG.GUI` (x 147, y 115, 375 by 375, bottom edge 490). Five more start above the top by
     design (the sliding bars). Audit: `BT-GUI-GUI-PARITY.md` rule 8.

140. **What quick key `-68` means** (reference, unclear). 26 shipped buttons (`CHATSEL`, `TALK2`, `SELPROV` `SERVICEX`) write
     `quickkey=-68`, byte `0xBC`, which is the comma's code in the Windows key space; RWE leaves it negative and nothing
     matches. Audit: `BT-GUI-GUI-PARITY.md` rule 5.

141. **Does the dialog fade `-0x18` mean a table shade or a true-colour dim?** (reference, unclear). `LoadGuiLayer` with `0x800`
     fades the covered area by minus `0x18` for `YESORNO`, the message box and the options menu; the chapter does not name
     the table. RWE dims nothing. Audit: `BT-GUI-DIALOGS-PARITY.md` rule 2.

### Malformed-input faults found on the way (separate from the conflicts above)

Each is a way a `.gui` in a mod, an archive or a data folder can cost more than itself. `CLAUDE.md` ("Anything read from
a file or a socket is someone else's") and `docs/SECURITY-AUDIT.md` apply. `io/malformed_input.test.cpp` has no GUI case
and `io/gui/gui.test.cpp` has one well-formed file; none of the faults below has a case. All traced by reading, not run.

- **M11. A `.gui` that parses to zero records is indexed.** `UiFactory::panelFromGuiFile` takes `entries[0]` after an
  `assert` (`UiFactory.cpp` about 71 and 157); `readGuiOrThrow` returns an empty list as success. An empty file, a file of
  comments, or a binary file (the shipped `ENDGAME.GUI` is a 4352-byte dump beginning with a zero byte and the word HEADER,
  filled with `0xCD`; RWE never loads it) reaches it. `GameScene_menu.cpp` about 157 (`LOADGAME.GUI`) and
  `MainMenuScene.cpp` about 266 and 278 pass a parsed list on unchecked; `GameScene_endgame.cpp` about 349 checks. A mod that
  overrides `LOADGAME.GUI` with an empty file crashes the save dialog.
- **M12. `parseGui` returns an optional and throws.** A missing `COMMON`, `id` or `name`, an unknown gadget type (the
  original also has 8, 10, 11 and 13; no retail file uses them) or a `VERSION` block missing a field throws
  `GuiParseException`/`TdfValueException` (`io/gui/gui.cpp` about 19, 64 to 72, 106 to 115). The callers' `!parsedGui`
  branches are dead. `loadBuilderGui` (`GameSimulationLoader.cpp` about 297) parses `<unit>1.GUI`, `<unit>2.GUI` ... for
  every unit loaded with no `catch`, so a malformed builder page in one mod stops **every peer's** game loading. The
  original defaults every missing key.
- **M13. Sizes, positions and `stages` are unchecked `int`s taken as unsigned.** `width`, `height`, `xpos`, `ypos` and `stages`
  are not narrowed (the original narrows to short and byte and refuses a layout larger than the screen). A negative size
  becomes about four billion in the component constructors; a large one is accepted. Consumers that loop on it: the plate
  fill (`UiPanel::render` about 110, `sizeX / tile` columns by rows per frame), `layoutNinePatch`
  (`NinePatch.cpp` about 15, **builds one tile per step each frame**; `width + step - 1` overflows near the limit), the
  scroll bar's art loops (`UiScrollBar.cpp` about 56 and 97) and the staged-button factory (`stages` of two billion
  allocates a label and a sprite per stage, `UiFactory.cpp` about 768 and 810). No shipped gadget has a negative size.
- **M14. Art assumptions with no count check.** `UiScrollBar` indexes frames 0 to 9 of the `SLIDERS` entry unchecked
  (`UiScrollBar.cpp` about 52 to 110) and loops forever on a zero-height middle frame (as the original does of its
  equivalent); `createPanel` and the picture-box path take `.sprites.at(0)` of a series (`UiFactory.cpp` about 205, 231 and
  668), which throws on a frame-less one.
- **M15. The record count is bounded only by the file cap.** The original allocates 200 records; the largest shipped file has
  44. A 16 MB file of minimal records builds millions of components.

### Probable code faults found on the way (not reference conflicts)

- **A list scrolled down when it overflows by fewer than three rows reads past the end of its items.** `UiListBox::scrollDown`
  (`UiListBox.cpp` about 348) compares the position against `maxScroll - 3` in unsigned arithmetic; for a maximum under three
  (including zero, a list that fits) the subtraction wraps, the test is true and the position becomes three. `render` (about
  20) reads `items[position + row]` for up to `min(visible lines, count)` rows with no bound on the sum. Reachable from the
  mouse wheel on a focused list and from a scroll bar's down arrow or wheel (the bar posts `ScrollDownMessage`, the list
  answers with `scrollDown`). Audit: `BT-GUI-GUI-PARITY.md` rule 9.
- **A list under 12 pixels tall has about 4.29 billion visible lines** (`numberOfLines` is `height / 12 - 1` in unsigned
  arithmetic, `UiListBox.cpp` about 207); the smallest shipped list is 31 pixels.
- **Grabbing a full-height scroll-bar box without moving divides 0 by 0** (`UiScrollBar.cpp` about 130, `boxInfo.range` is zero
  when the viewport fills the track); the list's `static_cast<unsigned int>` of the NaN is undefined (zero on x86-64).
- **Label alignment is never read.** 106 of 218 shipped labels are centre-aligned and 6 right-aligned; `labelFromGuiEntry`
  never sets an alignment, `UiLabel`'s centre is "on the x" not "within the width", and only two call sites set it. The real
  `YESORNO.GUI` has a `TITLE` label RWE's code comment says does not exist, so the question is drawn left-aligned at x 25.
  The comment at `GameScene_menu.cpp` about 389 to 392 ("The shipped file has no such gadget") is wrong for this install.
  Audit: `BT-GUI-GUI-PARITY.md` rule 6.
- **`active=0` is read and never used.** 86 shipped gadgets in 19 files (`RESTART.GUI`'s heading, `HELP.GUI`'s caption,
  `BLANK.GUI`, the lobby templates, the campaign `Side0` to `Side4`, `Campaign`/`Missions` lists and knobs) are drawn and
  clickable; a few screens patch it by name (`MainMenuScene.cpp` about 735 reuses `DebugString` on purpose).
- **Letter quick keys are dropped.** `parseGuiEntry` (about 119) reads `quickkey` as an integer, which fails on a letter;
  six records write one (`OPTIONS` `O` and `ALLIES` `A` in `ARMMAIN.GUI`/`CORMAIN.GUI`, `OK` `N` and `CANCEL` `P` in
  `NEWMULTI.GUI`). On the New Game screen Next and Previous have no keys.
- **A linked label is not clickable.** `link` is not read; 15 non-empty (ten in `LOUNGE2.GUI`, five in `SKIRMISH.GUI`:
  "Location", "Commander", "Mapping", "Line of Sight", "Difficulty"). Clicking the caption of a skirmish option does nothing.
- **A message box does not wrap, and Return does not close it.** `MainMenuScene::openMessageBox` (about 520) puts one
  centred, unwrapped line in the 372-by-272 plate and never enables the panel's default keys; callers pass
  `"Could not read <map>: <e.what()>"`, a peer's map name and a version message. Audit: `BT-GUI-DIALOGS-PARITY.md` rule 6.
- **Four labels swallow the click meant for the button below** (`RESTART.GUI` `Adjust` over `Difficulty`, `SKIRMISH.GUI`
  `HELPTEXT` over `Difficulty`, `VISUALRT.GUI` `TEXT` over `SHADING` and `BSHADOWS`; two to four rows each):
  `UiPanel::mouseDown` gives the click to the first visible child whose rectangle contains the point, a label included.
- **The text cap of 63** (`UiTextBox::largestMaxLength`) is below `SAVEGAME.GUI`'s 99 and the original's 127; no shipped
  screen is affected.
- **`status` is not applied**: four radio buttons start pressed in the data (`NEWCAMP.GUI` `Side0` and `Medium`,
  `SELCAMPX.GUI` `ARM` and `MEDIUM`).

### Stale register and comment text found on the way

- `TOTALA-EXE-COMMANDS.md` section 124: "none touches the simulation" (item 131) and `Selectable` "on every live unit" (item
  132). Four registered commands (`Mem`, `Assert`, `DPrint`, `ZBuffer`) have empty bodies and `MemDump` writes an empty file;
  the register does not say so.
- `io/gui/gui.cpp` about 80: the comment says the missing semicolon after `ARMGEN.GUI`'s `attribs` makes "both invalid". The
  integer reader takes the leading integer, so `attribs=256` reads correctly and only `colorf` is lost; the same fault is also
  in `CORGEN.GUI` `GADGET10` and in `RESTART.GUI` `GADGET3` and `GADGET4` (`active=1` swallowing `commonattribs=0`).
- `GameScene_menu.cpp` about 389 to 392: see the label item above.
- `TOTALA-EXE.md` section 88 does not list the absence of the cheat and debug command tables (a lockstep consequence);
  `BT-GAME-CONSOLE-COMMANDS-PARITY.md` gap 3 proposes the line.

### Re-confirmed, not new (Batch 14)

- The line interpreter's rules (white-space tokens, `#` stops the line, 20 tokens, no quoting, case-insensitive word, a
  missing integer is zero) agree with `chat_command.cpp`.
- A `.gui` record is a `COMMON` block plus type-specific keys at the record's own level; RWE reads it that way and builds
  the panel from the actual records, as the original recomputes the count (the file's `totalgadgets` is wrong in ten
  shipped files and is ignored).
- Every shipped gadget type is in RWE's enum (types 0 to 7 and 12 only); no shipped record misses a required key; no name is
  over sixteen bytes; no position or size is outside the original's narrow ranges.
- `maxchars` is honoured per text box (10 for a nickname, 63 for a game name, 20 for a save name).
- The three-by-three plate layout (frames 0 to 8, last row and column pinned to the far edges) is the list skin's tiling
  rule exactly (`layoutNinePatch`); the list skin itself is not drawn.
- The first-match, exact-name lookup agrees with `UiPanel::find` for the 39 shipped files that repeat a name.

## Batch 15 (2026-10-17): debug_lib, frontend/multi, network/net_game

Module chapters `debug-debug-lib`, `frontend-multi` and `network-net-game`. Same reference commit and the same evidence class
(static reading). RWE's own lockstep multiplayer was audited only for what a player sees in the lobby and for the places where it does
the same job as the chapter (resource sharing, drop and timeout, colours); the TA-compatible path (`net/ta/`, `TaHostLobbyScene`,
`GameScene_tahost.cpp`, `docs/TA-NETWORK.md`, an executed-against-real-TA record that outranks a static reading) was audited rule by
rule. The audits are `BT-DEBUG-DEBUG-LIB-PARITY.md`, `BT-FRONTEND-MULTI-PARITY.md` and `BT-NETWORK-NET-GAME-PARITY.md`. Neither side is
chosen; kinds as above. Items are numbered from 142; if another author has appended items, renumber on merge. None of these reaches the
simulation's hashed state: the lobby, the TA wire layer and the debug tools are presentation, transport or host-side rules. The debug
chapter produced **no** conflict (every rule is developer tooling). BT-176 to BT-178 were read and are not re-raised.

142. **How long is type `03`?** (register versus reference). Register (`tad_util.cpp` `tadExpectedSubPacketSize`, `TA-DEMOS.md`
     "`0x03` | 7 | ?", marked "the reference's and unverified"): **7** bytes. Reference (`network-net-game`, "Protocol framing
     table"): **3** bytes, "reserved/default callback; also raw condenser envelope outside application parsing". Never seen in the
     corpus. The same table sizes `13` (18), `1c` (5), `1d` (9), `25` (5) and `27` (17), which the register has no length for (a bundle
     holding any of them loses its remainder on the live path). Kind: register. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 5.

143. **What `0x1e` is** (register wording versus reference). Register (`TA-NETWORK.md` "Launch", `taBuildStart`, `TA-DEMOS.md`): "the game
     starts. Every capture says 1", listed as "start". Reference (`network-net-game`, "Loading, start positions"): type 30 stores the
     **recipient's start-position index** and is resent by the host until the recipient's `1f` acknowledges; fixed starts give
     consecutive positions from zero to the non-observer participants, random starts shuffle them. The captured `1` is then the joiner's
     index with the host at zero. RWE sends `1` once, never reads the `1f` and has no random start. Kind: register. Audit:
     `BT-NETWORK-NET-GAME-PARITY.md` rule 16.

144. **What `0x21`, `0x22` and `0x26` are** (register wording versus reference). Register (`TA-DEMOS.md` "ident", `TaBattleroom.h` "0x26:
     the two player ids, and 32 bytes every capture leaves empty", `taBuildPlayerNumber` "a player's id and its 1-based number in the
     game"): identity records. Reference: `26` is the **host's ten-slot DPID table**, broadcast at every heartbeat (41 bytes = type + ten
     DWORDs); `21` is a joiner's **group-assignment request** and `22` the host's **result**, a `lobbyDataSynced` value one to ten that
     groups machine-owned players (a remote human and its computer share one) and is separate from the numbered team. RWE sends `22
     <id> <n>` unprompted, once, and never answers a `21`. Kind: register. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 9.

145. **The 21-byte create-player payload carries a protocol marker, not the version** (audit versus reference). Item 129 (and
     `BT-NETWORK-HAPINET-PARITY.md` rule 8, `BT-NETWORK-UNIT-SYNC-PARITY.md` rule 4) read the two shorts at the end of the block as the
     joiner's version, which decides whether the second unit checksum is compared. `network-net-game` ("Names, slot creation") says the
     shorts are **fixed values zero and `0x50`** that a host checks (and rejects on) in the create-player message; the compatibility
     version is the player record's own major and minor. RWE's captured pair (0, `0x0050`) is exactly the marker. This weakens item 129's
     premise; it does not say where the version is. Kind: audit. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rules 3 and 7.

146. **The 17-byte prefix of the `0x28` record** (register incompleteness versus reference). Register (`io/tad/tad_events.h`
     `TadResourceStats::prefix`): "mostly zero ... even the field boundaries are not settled", two growing values "shaped like 16.16
     fractions", byte 0 "1 only in a game's closing seconds". Reference (`network-net-game`, "Economy snapshots"): the record is four
     widened **kill and loss counters**, four stock and storage floats, six cumulative floats and a **share flag** (1 + 17 + 16 + 24 =
     58). The growing values are the counters; byte 0 would be the share flag, which a flagged reply sets and which starts the `29` view
     handshake. RWE writes the prefix as zeros, so a TA scoreboard shows zero kills and losses for RWE's side. Kind: register. Audit:
     `BT-NETWORK-NET-GAME-PARITY.md` rule 18.

147. **What happens to a dropped player's units** (design versus reference). Design (`docs/ROADMAP.md` "Drop handling", `GameScene_peers.cpp`
     `onPlayerDropped` comment): their units "stay exactly as they were, with the orders and the fire mode they had, which is what the
     original does". Reference (`network-net-game`, "Removal, rejection and timeout policy"): `RemovePlayer` **kills its units**. No
     `TOTALA-EXE*.md` finding covers either; RWE's own TA-host removal (`removeRemotePlayer`) does kill them. Kind: design. Audit:
     `BT-NETWORK-NET-GAME-PARITY.md` rule 15.

148. **When a silent peer is given up on** (design versus reference, unrecorded). RWE's lockstep drop is automatic after
     `dropTimeoutSeconds` (default 10, 0 never) with a countdown banner and no decision; a silent TA joiner is never dropped at all.
     Reference: after `timeoutSecs * 30` ticks (pause-adjusted) a **prompt** opens, only if every timed-out remote shares one machine
     group, and at `timeoutSecs + 120` seconds the host rejects with reason six on its own. The default `timeoutSecs` is not stated.
     Neither `TOTALA-EXE.md` section 88 nor `compatibility.md` mentions it. Kind: design. Audit:
     `BT-NETWORK-NET-GAME-PARITY.md` rule 15.

149. **Does the original have a commander-death rule in the battle room?** (code comment versus reference and register).
     `TaHostLobbyScene::startLoading` (about 265): "TA has no commander-death rule: a player whose commander dies keeps playing", and it
     plays `GameContinues`. `TA-NETWORK.md` "The options byte": bits `0x08` and `0x10` are "Game Ends and Deathmatch; neither is
     Continues", and RWE advertises the default `0x4F` (bit `0x08` set). Reference (`frontend-multi`, `COMMANDER`; `game-victory`): a
     commander policy with mode two meaning Deathmatch, in the battle room and in the victory test. A joiner shown Game Ends while the host
     plays Continues would see the machines disagree about when the game ends. Which bit is which is not settled by either source. Kind:
     design and register. Audit: `BT-FRONTEND-MULTI-PARITY.md` rule 22.

150. **How many states the `COMMANDER` button has** (reference, unclear). `frontend-multi` says it "cycles zero/two"; the register's
     options byte has three named states (Game Ends, Deathmatch, Continues). Which of the chapter's two values is Continues is not
     stated. RWE's host-lobby button has two (game ends, continues) and drops the Deathmatch label. Audit:
     `BT-FRONTEND-MULTI-PARITY.md` rule 16.

151. **Is the symmetric-team alliance a documented divergence?** (audit statement versus the register it cites).
     `BT-GAME-PLAYERS-PARITY.md` rule 1, `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 2 and `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 8 call
     RWE's fixed, symmetric lobby team "the register's documented alliance divergence". A search of `TOTALA-EXE.md` section 88 and
     `docs/compatibility.md` (2026-10-17) finds no entry about alliances or teams. The original's relations are directional, editable
     in play (the ALLIES dialog) and separate from the numbered team and the shared-victory flag (`frontend-multi`, `network-net-game`).
     Kind: register (a missing entry). Audit: `BT-FRONTEND-MULTI-PARITY.md` rule 13, `BT-NETWORK-NET-GAME-PARITY.md` rule 12.

152. **Fixed start positions: the seat number or the order of the participants** (audit versus reference, two chapters). Batch 11 records
     that "the start-slot deal uses the occupied slot numbers as the position set, sparse sets included, as the reference does"
     (`game-game-load`), and `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 7 makes RWE require a position for each occupied seat's slot
     number. `network-net-game` ("AssignStartPositions") gives **consecutive positions from zero to the non-observer valid participants** for
     fixed starts. For three humans in seats 0, 1 and 5 on a four-position map the two readings give different results and one of them is a
     refusal. Kind: audit. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 16.

153. **Name length limits** (reference code versus shipped data). `frontend-multi` ("OpenNewMultiDialog"): the new-game dialog sets name
     limits **sixteen** and password ten, into seventeen-byte fields. The shipped `NEWMULTI.GUI` (`totala1.hpi`, read with `tools/hpi.py`)
     says `maxchars=63` for `GAMENAME`, **`10` for `NICKNAME`** and `10` for `PASSWORD`; RWE honours the file. `SELGAME`'s box is clamped to
     RWE's wire bound of 20. The three disagree. Kind: data. Audit: `BT-FRONTEND-MULTI-PARITY.md` rule 7.

154. **The logo-candidate rule uses `>=`** (reference, unclear). A candidate `j` is excluded if any player's colour is greater than or equal to
     `j`, which "can suppress every index up to a used high logo". RWE (and `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 3) exclude a colour
     only when another seat holds exactly it. Probably a decompilation artefact; not settled. Kind: reference. Audit:
     `BT-FRONTEND-MULTI-PARITY.md` rule 10.

155. **Which transport the lobby ping and heartbeat use** (reference versus register, unclear). Register (`BT-NETWORK-HAPINET-PARITY.md`
     rule 3, `TA-NETWORK.md`): the battleroom runs over TCP and the game over UDP. Reference (`network-net-game`, `SendNetHeartbeat`,
     `HandlePing`): the `02` is sent "under temporarily disabled guaranteed delivery", which on DirectPlay's TCP/IP provider is the
     datagram path. `TaPinger` frames an uncompressed 20-byte packet and sends it on whatever transport the request arrived on. Kind:
     register. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 14.

156. **What the `0x07` is** (register wording versus reference). Register (`TA-NETWORK.md` "A silent host is offered for rejection"): a real
     host "sends a `0x07` and a `06 26 20 24` status about every two seconds". Reference: `07` is the **reply** to a probe `06` and sets the
     peer's keepalive flag; the heartbeat itself sends `06`, the `26` table and the republished `20` and `24`, and a silent peer is judged
     by its last-heard time over **any** message. RWE sends `07` unsolicited with every keepalive and never answers a joiner's `06`. The
     captures agree with RWE's output; the role of the `07` differs. Kind: register wording. Audit:
     `BT-NETWORK-NET-GAME-PARITY.md` rule 9.

### Batch-15 RWE faults (crashes, unbounded or unauthenticated input; see `docs/SECURITY-AUDIT.md`)

None is a crash found by running; each is a trace by reading, in the TA-host path that `docs/SECURITY-AUDIT.md` does not cover.

- **A forged `DELETEPLAYER` ends the game for the TA player.** `TaHostSession::handleDeletePlayer` (about 651) erases the named peer without
  checking who sent it, over UDP as well as TCP, and the ids are small and predictable (host `0x08D90E76`/`0x08D90E77`; joiners counted down in
  pairs). `onPeerLeft` then posts a `PeerLeft` and `GameScene::pollTaHostInbound` (`GameScene_tahost.cpp` about 263) calls
  `removeRemotePlayer(simulation, link.remote)`, killing the joiner's commander and every unit. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 20.
- **`PeerLeft` removes the joiner whoever left.** `pollTaHostInbound` does not compare `item.peer` with `link.peer`. Same rule.
- **A second machine can join a launched game and play as the joiner.** `handleCreatePlayer` (about 603) has no closed state and no password,
  and `TaHostGame.cpp` (about 296-315) stamps `item.sender = JoinerSender` on every peer's in-game data; the session description also never
  says started. Same rule.
- **A silent joiner is never dropped** (no last-heard timeout on the TA-host path). Same rule.
- **Five size-less codes lose the remainder of a bundle on the live path**: `13`, `1c`, `1d`, `25`, `27` fall to zero in
  `tadExpectedSubPacketSize` and `tadSplitSubPackets` makes the rest one unknown subpacket, discarding any `0x2c` behind them. `1c` (a player
  drop) and `27` (an integrity warning) are the ones a live game can produce. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 5.
- **A full session drops the pending handshake silently**, leaving the joiner waiting; the original sends reject reason five. Rule 7.
- **A host can change the map or any rule under a guest who has readied and start at once** (lockstep lobby): readiness is never cleared by a
  change (`resetRoom` runs only on entering a room). A different map or rule from the one agreed to is a different game for that guest.
  Audit: `BT-FRONTEND-MULTI-PARITY.md` rule 9.
- **A direct LAN join checks that the map file exists, not that it is the same map** (`joinSelectedGame`, `readFile("maps/<map>.ota")`). A
  same-named map with different data joins and desyncs. Audit: `BT-FRONTEND-MULTI-PARITY.md` rule 6.
- **A second TA joiner is not numbered or told the slot table** (latent while one joiner is hosted): the `numbersSent_` latch and the
  `[host, peer]` form of the `26`. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 9.
- **Session name and host-player name are uncapped** (the chapter now gives 16 + 15 and 16): a long map or game name overflows a real TA's
  32-byte list entry. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 3 (extends `BT-NETWORK-HAPINET-PARITY.md` gap 2).
- **Debug chapter: no fault.** The original's defects it lists (unbounded `sprintf`, unchecked allocations, a file handle compared with zero) are
  not reproduced in RWE.

### Unrecorded divergences found on the way (not reference conflicts)

- The multiplayer lobby's absent rule fields and controls -- max units, watch permission, cheat permission, closed admission, the
  restrictions editor, the Deathmatch stage, screen resolution, memory and CD columns, the Synching overlay, blocked seats, observers, the
  provider list (one connection), the serial and modem dialogs, the game-list VER and Lock statuses, `ENDMULTI` -- are written only as code
  comments (`MainMenuScene_multiplayer.cpp` about 1455-1480, `MainMenuScene_multiplayer.h`). `TOTALA-EXE.md` section 88 and
  `docs/compatibility.md` mention neither the lobby nor lockstep. Audit: `BT-FRONTEND-MULTI-PARITY.md` rules 2, 3, 6, 14, 16, 18.
- `runAutomaticSharingFor` clamps the transfer to the recipient's free capacity and returns unless it is positive, where the original
  does not lower-clamp the gap (`network-net-game`, "Resource messages"). Harmless; `TOTALA-EXE-COMMANDS.md` section 124's list of
  hardenings omits it. Audit: `BT-NETWORK-NET-GAME-PARITY.md` rule 17.
- A fatal join or network error ends the process in the original (`FatalError`, exit code 3); RWE reports a drop or a message box. Audit:
  `BT-DEBUG-DEBUG-LIB-PARITY.md` rule 5.
- `TOTALA-EXE.md` section 88 lacks the cheat and debug table line already proposed in `BT-GAME-CONSOLE-COMMANDS-PARITY.md` gap 3; this
  batch adds the lobby line, the alliance line (item 151) and the drop line (items 147, 148).

### Stale register text found on the way

- `TA-NETWORK.md` "Launch" and `TaBattleroom.h`: `0x1e` "the game starts" (item 143); `0x22` "number" and `0x26` "two player ids" (item 144).
- `TA-DEMOS.md` `0x03` length 7 (item 142) and the `0x28` prefix (item 146).
- `TA-NETWORK.md` "RWE's own-clock mode" and `tadEncodeSpeed(u16)`: the `0x19` payload is three bytes with a subtype that picks speed or
  pause (BT-178, confirmed again by `network-net-game`'s `25` entry). Already tracked.
- `docs/ROADMAP.md` "Drop handling": "which is what the original does" (item 147).
- `TaHostLobbyScene.cpp` about 265: "TA has no commander-death rule" (item 149).

### Re-confirmed, not new (Batch 15)

- The 186-byte `0x20` is the type byte plus the packed 185-byte player record; the 192 is the demo header's form (`TA-DEMOS.md` correction).
- The sizes of types `02`, `05`, `06`, `07`, `08`, `09`, `0a`, `0b`, `0c`, `0d`, `0e`, `0f`, `10`, `11`, `12`, `14`, `15`, `16`, `17`, `18`,
  `19`, `1a`, `1b`, `1e`, `1f`, `20`, `21`, `22`, `23`, `24`, `26`, `28`, `29`, `2a` and the embedded length of `2c` agree with
  `tadExpectedSubPacketSize` and the corpus counts.
- Automatic resource sharing every 60 ticks, the last qualifying recipient, one third of the metal surplus and one half of the energy
  surplus over the reserve, the crossed metal/energy routine names and the 17-byte `16` layout agree with `TOTALA-EXE-COMMANDS.md` section 124
  and `sim/ResourceSharing.cpp`; the mapping share (every 450 ticks) and the `16` over the wire are the recorded gaps.
- The colour grant (the asked colour if free, otherwise the next free one, nothing sent on exhaustion), the two-second heartbeat, the
  echoed-tick ping and the `15`-then-UDP launch handshake agree with `TaBattleroom` and `TA-NETWORK.md`.
- A leaving TA player's commander dies with the quit cause, its other units vanish and its ids are deleted (`TaHostGame::leave`,
  `removeRemotePlayer`) as the chapter's `RemovePlayer` and `TA-NETWORK.md` describe.
- `Assert`, `Mem`, `DPrint` and `ZBuffer` console commands are empty-bodied while an assert dialog template exists
  (`TOTALA-EXE-COMMANDS.md` section 124, `debug-debug-lib`).
- RWE refuses a start with every seat on one team (BT-194) and has ten seats; the team range 0 to 4 with five meaning none agrees with
  `frontend-multi` and `TA-NETWORK.md`.
