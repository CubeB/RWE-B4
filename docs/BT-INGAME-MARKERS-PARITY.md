# Build footprints, order icons, path trails and range overlays parity audit

Spec-room finding, 2026-10-17. Like `BT-INGAME-INFO-PANEL-PARITY.md`, `BT-MAP-RADAR-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `ingame-markers` (six services from `0x438C00` to `0x439B30`): the
queued-build footprint animation, the terrain-following range-circle primitive, the unit range
rings in ordinary and diagnostic modes, the order-target marker with its remembered position, the
animated path trail between orders, and the target ellipse. This is the finding that
implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

`TOTALA-EXE-RENDER.md` (the order overlay, the artwork, the spacing and the march; sections 26 to
28) already decodes five of the six from the executable, down to the instruction, and RWE
implements the footprint animation, the trail and the markers closely, with comments citing the
register. So this audit is a cross-check that finds the two sources agree almost everywhere. What it
adds is the **range-ring drawing** (ordinary and diagnostic), the **remembered target position**
rule as it bears on what RWE draws, and two places where the chapter and the register describe one
routine differently.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ingame-markers.md`). Nothing else
  under that tree was opened: no source, no decompilation, and none of its source links were
  followed. Addresses are the ones the chapter names; rules are restated in prose. The spec-room
  discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read,
  read-only; no row concerns these overlays and none is re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no image was produced and no game run. Weaker than `TOTALA-EXE*.md`, which was read
  from the retail executable and, for the footprint box, the path trail and the markers, checked
  against play. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `game/GameScene_render.cpp` (`renderBuildBoxes`,
  `renderBuildBoxSweep`, `renderPlacementSweeps`, `renderCloakRadius`, `drawWaypointTrail`,
  `renderUnitOrders` and the call sites near 2119-2160) and `game/GameScene_commands.cpp`
  (`tryGetUnit`) at the working tree of 2026-10-17. Nothing was built, run or changed; another
  agent had uncommitted edits, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. The queued-build footprint animation
3. The terrain-following range-circle primitive
4. Unit rings in ordinary and diagnostic modes
5. Target position, remembered locations and the order icons
6. Animated connections between orders
7. The target ellipse
8. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| footprint animation (`0x438C00`) | `TOTALA-EXE-RENDER.md` section 28 (the eight lines, the ten-tick clock, the palette pairs), section 27 | **MATCHES**; one description conflict; rule 2 |
| range-circle primitive (`0x438EA0`) | RENDER near 479 ("explicitly call the height lookup at every vertex") | rule 3 |
| unit rings (`0x4390A0`) | RENDER 332 ("the unit's range circles, once per unit"); `TOTALA-EXE-WEAPONS.md` (kamikaze) | the drawing is new; rule 4 |
| order icon and coverage (`0x439740`) | RENDER near 495-552 (the cursor animations, the cached position, the half rate) | **MATCHES** in decode; RWE misses the cache; rule 5 |
| path trail (`0x4394E0`) | RENDER near 395-480 and the implementation spec | **MATCHES**; documented phase difference; rule 6 |
| target ellipse (`0x4399F0`) | RENDER near 553 (a flat 16-segment ring, radius 0.89 times the footprint radius) | a **CONFLICT** in the radius rule; rule 7 |

## 2. The queued-build footprint animation

**Reference.** The service returns at once for unit type zero (without writing the output position).
It adds the unit definition's low and high bounds to the order position, projects both corners
**using the low corner's height for both projected Y values** (a flat overlay, not a wireframe),
and takes an age from the game tick less the order's timestamp, clamped to zero through ten (the
fields are unsigned, so a future timestamp wraps and selects the ten-tick end). The horizontal and
vertical sweep offsets are the projected spans times age over ten. Four outer lines are displaced
one pixel beyond four inner lines, giving two colours; the vertical lines sweep each toward the
opposite edge, meet at the midpoint and have exchanged sides at ten ticks. **Bit 4 of the ordering
unit's flag word** picks the pair: GUI palette three with the second-colour entry when set,
otherwise palette one with palette nine. The service then writes the order position to the shared
output accumulator. It is not a construction-progress display and decides nothing about when the
hull becomes solid.

**Register.** RENDER section 28: the box is out of the definition's bounds (the previous section's
footprint-times-eight half-extents), the clock is the 30 Hz tick less the order's creation stamp
clamped at ten, the width and height steps are linear (`width x t / 10`), eight lines in two
colours, the colour flag is bit 4 of the unit's flag word at `+0x110` (the unit is selected),
**selected: `guicolours[3]` and `[10]`, otherwise `[1]` and `[9]`**; at rest the colour-A lines are
the inner pair and the colour-B lines the outer, giving dark cyan inside bright green (selected) or
navy inside bright blue.

**RWE: MATCHES.** `renderBuildBoxSweep` (`GameScene_render.cpp` about 1135-1170) draws the same
four-and-four lines with the same one-pixel overhang, the same linear `t`, the same ten ticks, the
same colours (selected owner `(0,128,128)` over `(83,223,79)`, otherwise `(0,0,128)` over
`(84,84,252)`) and holds the settled shape as `renderBuildBoxes` (about 974-1030). The chapter's
"second-colour entry" for the selected pair is the register's `guicolours[10]`; the two sources
name one palette entry. Differences of detail:

- **The timestamp.** The order's creation tick is on the order in the original and synchronised.
  RWE's orders carry no timestamp, so `updateBuildBoxAppearances` (about 1073-1130) notes the frame
  an order is first seen, or the click for a placement ghost. Visually the same; a build order that
  arrives late over the network starts its sweep late by the delay. Presentation.
- **The box's height: UNCLEAR.** The chapter flattens the overlay at the low corner's height (the
  order position's Y plus the definition's minimum Y); the register adds that the order's Y is the
  levelled build height over the whole footprint (section 27 says RWE's `getHeightAt(centre)` is
  "close but is not the same rule"). RWE uses the height at the footprint's centre. A box on a slope
  sits a few pixels off. Presentation.
- **Extents: MATCHES in rule.** RWE uses the movement-collision footprint region in 16-unit cells;
  the register says the box is footprint-times-eight half-extents either side of the snapped
  centre, which is the same rectangle.
- **When it is drawn.** RWE plays the sweep for every own build order whether or not Shift is held
  and draws the settled box only with Shift (`renderPlacementSweeps` called at about 2119,
  `renderBuildBoxes` inside the Shift block at about 2124-2151). The original draws it from the
  order overlay walk, whose mask depends on which unit it is (the register, section 26).
  `TOTALA-EXE-RENDER.md` section 28 is the authority for RWE's choice; the chapter has the service
  but not the caller's gate.

## 3. The terrain-following range-circle primitive

**Reference.** A radius of zero draws nothing. Otherwise the segment count is the radius times
about two pi over **eight** (one segment per eight world units of circumference), the angular step
is 65536 over that count, and **count plus one** segments are drawn, so the last can overlap the
first. Each endpoint offsets X and Z by the fixed-point sine and cosine and sets its Y to the
**greater of the centre's height and the sampled ground height**: the ring drapes over higher
terrain and never dips below its centre. Optional text goes at the endpoint reached by the segment
index times three, four pixels lower; a label at zero/zero falls back to the last endpoint, so
several labels use different indices to avoid overlap. There is **no minimum-segment guard**: a
radius of one gives a count of zero and a division by zero. A ring is a picture of a radius, not a
test that the cells are passable, visible or attackable.

**RWE: DIFFERS (presentation).** `renderCloakRadius` (about 1034-1066) is the only world-space
ring: 48 fixed segments, a flat circle at the unit's own Y (`centre.y`), plain white, no label, no
terrain sampling. Differences from the chapter: the segment count does not follow the radius (a
large kamikaze ring would be coarse; a small one over-drawn), the ring is flat so on a slope it
cuts through the hill on one side and floats on the other, and the colour is white where the
original's cloak circle is the shadow colour (rule 4). The comment says the ring "lies on the
ground the way the build boxes do", which is true of the box and not of the ring. No visible
consequence on level ground.

## 4. Unit rings in ordinary and diagnostic modes

**Reference.** The unit-ring service uses the order's unit and its definition. **With `showRanges`
zero** it draws a circle of the **minimum cloak distance** only when that distance is nonzero and
the unit's activation flag value four is set, in the shadow colour. A kamikaze definition (flag
`0x10000000`) with a non-null explosion weapon additionally draws a **pulsing circle** from half
that weapon's area of effect: it grows at twice that half-area radius per sixty ticks of the game
tick modulo sixty, has a nominal lower bound of eight and caps at the half-area; it then draws the
kamikaze trigger distance for a unit with a nonzero motion state or the **sight distance** for a
unit at rest, and returns. **With `showRanges` nonzero** every nonzero definition radius is drawn
and labelled in GUI palette fourteen: minimum cloak, sight, radar, sonar, radar jam, sonar jam,
build distance, manoeuvre and kamikaze distance, and the weapon ranges alternate between two
colours by tick parity with labels "weapon1 range" to "weapon3 range". Slots one and two require
their own slot flag value two and a nonzero range; **the third slot tests the first slot's flag
while dereferencing the third slot's weapon pointer**, so it can suppress a valid third ring or
read an absent weapon when the first slot is active. A display fault, not combat's eligibility.

**Register.** RENDER names the handler ("the unit's range circles, once per unit", mask `0x10`)
and nothing of what it draws; `TOTALA-EXE-WEAPONS.md` has the kamikaze trigger. The v3.1 cloak
radius is RWE's `renderCloakRadius` for the unit under the cursor.

**RWE: DIFFERS in who and what, MISSING for the rest.**

- **The cloak ring: DIFFERS.** Drawn for the **hovered** unit with Shift down
  (`GameScene_render.cpp` about 2141-2146), only for a cloakable unit that is currently cloaked,
  in white. The chapter draws it as part of every order overlay unit's pass (the register: "once
  per unit") when the activation flag four is set, in the shadow colour; whether that flag is "is
  cloaked" or "cloak switched on" is not said. UNCLEAR for the flag, DIFFERS for the set of units:
  selected units show no ring in RWE.
- **The kamikaze pulse: MISSING.** Walkers that blow up (the crawling bombs, Roach and Vader) show no trigger or blast ring at all. The rule is fully stated above; only
  the pulse's growth and the nominal minimum of eight need a play check.
- **The sight ring for a unit at rest: MISSING** (drawn in the kamikaze branch only).
- **The diagnostic ring set and the labels: MISSING**, together with the command that turns it on
  (`+ShowRanges`, `BT-GAME-SHARE-COMMANDS-PARITY.md` rule 6). The minimap detection rings
  (`renderMinimapDetectionRings`) are a separate, documented feature.
- **The third-slot fault: not applicable** until the set exists; should it be built, the question
  of whether to reproduce a display fault belongs to the maintainer.

## 5. Target position, remembered locations and the order icons

**Reference.** The coverage service first resolves the marker position. With **no target unit** it
uses the order position. With a target unit it asks whether the target is **visible to the ordering
unit's player**; an invisible target whose cache-valid flag (`0x200000`) is set uses its **cached**
horizontal position and the target's *current* height; otherwise it uses the live full position,
**refreshes the cached horizontal position** and sets the flag. So the first time a hidden target
is drawn with no cache, the cache is filled from its true position; later it keeps the old
horizontal coordinates. A marker-animation id of zero writes the position and draws no icon. In
the diagnostic mode, ids one and two also draw each enabled weapon slot's area-of-effect ring
(the full area value as radius here, against the kamikaze pulse's half), coverage and the attack
run length. For a nonzero id the cursor sequence's frame is the **game tick over twice the
sequence's duration, modulo the frame count**, drawn blended at the projected position; there is
no null-sequence or zero-duration guard. The frame is on the global tick, not the order's start.

**Register.** RENDER near 495-552: the same table of cursor sequences by id (id zero never
assigned: no icon), the same half-rate frame rule, and the same cache: "the marker freezes where
you last saw the thing rather than tracking it through fog". Section 28 agrees on the ten-tick
clock.

**RWE: MATCHES the icons; the remembered position is MISSING and leaks the target.**

- **Icon table: MATCHES.** `renderUnitOrders` maps each order to a `CursorType` (move, attack,
  guard, repair, reclaim and so on; build and bugger-off have none) at about 1426-1447. Two
  stand-ins are commented (the D-gun uses the attack cursor, landing on a pad the move cursor).
- **Frame selection: DIFFERS.** The icon's frame is `wall-clock milliseconds / 500` modulo the
  frame count (about 1449-1460), a fixed two frames a second regardless of the sequence's own
  duration, and on the wall clock rather than the game tick, so markers keep animating while the
  game is paused and do not slow with the game speed. `BT-GRAPHICS-GAF-PARITY.md` rule 5 holds the
  rate rule; this adds the clock.
- **The target position: DIFFERS, and it is an information leak.** For an attack, a guard, a
  repair, a reclaim, a capture, a load or a D-gun order against a unit, RWE draws the marker at
  `tryGetUnit(target)->position` (about 1330-1420): the unit's **true, current position, with no
  visibility test** (`tryGetUnit` returns the simulation's state, `GameScene_commands.cpp` about
  1488). If the target moves into fog or under a cloak, the marker (and the trail to it, for the
  orders that draw one) follows it, and a player who ordered an attack on a unit that then
  retreated can watch where it goes. In the original the marker freezes at the last position the
  player saw. Not hash-visible; client-side only; but it shows an enemy's movements under fog.
  Kind: gap against the register (RENDER near 546) and the chapter. Settled by the code: keep a
  per-order last-seen position on the client, refreshed while the target is visible.
- **The diagnostic coverage rings: MISSING**, with rule 4's set.

## 6. Animated connections between orders

**Reference.** The trail service draws the current order's icon first (through the coverage service,
so the shared accumulator is updated) and **returns at once if its flag is zero**. Otherwise it
takes the three-dimensional difference between the previous and the resolved positions and its
integer length; **below one world unit it draws nothing**. The icon is `pathicon`. The first
offset along the segment advances **48 world units over thirty ticks** from the order's age modulo
thirty; samples then fall every **48 world units** while the offset is below the length, each
interpolated in X, Y and Z with 64-bit multiply and shift and projected with its interpolated
height; the route is not snapped to terrain. The frame starts at the order's age over the duration
(at least one), modulo the frame count, and advances one per sample. It is a straight line between
displayed destinations, **not** the A\* route, the steering waypoints or a guaranteed channel.

**Register.** RENDER implementation spec: identical arithmetic (`1.6 x (age mod 30)` world units
of phase, spacing 48, straight 3D lerp, no terrain sampling), the artwork (one 11 by 11 frame, so
the frame advance is a no-op), and the rule that only the current unit gets the trail while other
selected units get icons only.

**RWE: MATCHES, with one documented departure.** `drawWaypointTrail` (about 1258-1308) uses the
same spacing, the same 1.6-per-tick march wrapping every thirty ticks, the same skip under 0.001 of
a unit (the original's threshold is one world unit; a difference of hundredths, presentation) and
the same straight 3D line, and says in its comment that the trail "cuts through the hill rather
than draping over it. That is the original's behaviour, not an omission." The trail is drawn for
the hovered unit and for the single selected unit and not for the others (about 2141-2156), which
is the register's rule.

- **The phase: DIVERGENCE (documented in the code, not in section 88).** The march phase comes from
  the global clock, not from each order's age, because RWE's orders carry no timestamp; the comment
  explains the cost of adding one. The original's two orders queued a few ticks apart march slightly
  out of step; RWE's march together.
- **One frame: MATCHES.** The register found `pathicon` has one frame, so the frame advance is a
  no-op in both; RWE draws `frames.front()` only.
- **Orders that draw no line.** RWE draws no trail for attack and D-gun orders (the table at about
  1480-1500). The chapter's flag is per call and the table that decides it is the mission-order
  table (the register: "I did not enumerate the table"). UNCLEAR whether an attack order has a trail
  in the original; the register's artwork note says bit one decides.

## 7. The target ellipse

**Reference.** The service uses the target unit's live position and a **definition field at
`+0x178`** when a target exists, otherwise the order position and the radius **32**. It makes a
screen-space ellipse whose **horizontal radius equals that field and whose vertical radius is
truncated from 0.89 times it**, draws sixteen segments in palette twelve, and copies the world
position to the output. It does not resample the ground (unlike the range circle) and does **no
visibility or cache lookup** for its target (unlike the coverage service). Its radius is a model
field, not a loaded weapon range.

**Register.** RENDER near 553: a flat 16-segment ring "of radius (int)(footprintRadius x 0.89) (the
double 0.89 at `0x4FD2C0`; radius 32 when the order has no target unit), colour `cfg+0xDD7`".

- **CONFLICT: where the 0.89 applies.** The chapter makes the ring an **ellipse** (the factor on the
  vertical radius only); the register scales a **circle's** single radius by 0.89. One of them has
  the horizontal radius at the full field value, the other at 0.89 of it. The two may be one
  routine read at different depths (a ring drawn flat in the oblique projection comes out as an
  ellipse), in which case the register's "radius" is the vertical one; nothing in either source
  settles it. Kind: register. Settled by a spec-room read of the segment loop.
- **RWE: MISSING.** There is no ring around an order's target (`TOTALA-EXE-RENDER.md` section 26
  step 9, "for the full TA look, also bracket and ring the order's target while Shift is down", is
  an open item of the roadmap's rendering list).
- **No visibility test in the original:** like the marker's live case, a ring around a hidden
  target would reveal it, so a port should either draw it at the remembered position (rule 5) or
  only for a visible target. The chapter says the original does neither: the ring uses the target's
  live position. If so, the original leaks the hidden target's position to the player who ordered
  the attack, through this ring alone, while the icon is frozen. UNCLEAR whether the ring is drawn
  for the orders that can name a hidden target.

---

## 8. Gaps to fix

Ranked by what a player would notice. None touches the simulation (the overlays are presentation).

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **The order marker and its trail follow a target into fog or under a cloak**: RWE draws the target's true position (no visibility test); the original freezes the marker at the last seen position. | DIFFERS (information leak) | code: a per-order last-seen position kept by the scene |
| 2 | **Kamikaze units show no ring**: no pulsing blast ring, no trigger distance, no sight ring at rest. | MISSING | code, after a play-check of the pulse |
| 3 | **The cloak radius** is drawn only for the hovered unit; the original draws it for every overlay unit, in the shadow colour, and as part of a terrain-following ring. | DIFFERS (UNCLEAR for the flag) | spec-room read of the activation flag |
| 4 | **No diagnostic range rings** (`+ShowRanges`): nine radii and three weapon ranges with labels. | MISSING | code, with the command |
| 5 | **No ring around an order's target** while Shift is held; and the register and the chapter disagree on whether it is a circle scaled by 0.89 or an ellipse. | MISSING, CONFLICT | spec-room read |
| 6 | **Marker icons animate on the wall clock at two frames a second**, so they run while paused and ignore the sequence duration and the game speed. | DIFFERS (presentation) | code; rate is `BT-GRAPHICS-GAF-PARITY.md` rule 5 |
| 7 | **The range circle is flat with 48 segments**; the original drapes over terrain with one segment per eight units. | DIFFERS (presentation) | code |
| 8 | **The footprint box's height** is the centre height, not the order's levelled height. | UNCLEAR (register section 27) | code |

No source file was changed.
