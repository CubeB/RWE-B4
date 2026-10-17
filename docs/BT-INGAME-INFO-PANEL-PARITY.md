# Sensor contacts, battle composition and information panels parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-CONTROL-PANEL-PARITY.md`
and `TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers
one chapter of the reference, `ingame-info-panel` (module group `ingame/info_panel`, the
interval `0x467440` to `0x46bc70`): the per-tick sensor pass that decides which units are
radar, sonar or jammed contacts for the displayed player, the layered battle frame, the
resource HUD, the unit and feature footer, the bars and outlines, and the diagnostics. This is
the finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

The minimap (`0x4665d0` to `0x466dc0`) is the next chapter and has its own audit,
`BT-MAP-RADAR-PARITY.md`. Sight, line of sight and the fog grid are `BT-MAP-LINE-OF-SIGHT-PARITY.md`.
The order panel and build placement are `BT-INGAME-CONTROL-PANEL-PARITY.md` and
`BT-INGAME-BUILD-PLACEMENT-PARITY.md`. Nothing from those is repeated here.

**Almost all of what this chapter says about sensors and the footer is already in the
register**, and the register is the more detailed side (`TOTALA-EXE-VISION.md` sections 2, 17,
18 and 25; `TOTALA-EXE-INTERFACE.md` section 99). So most of this audit is a cross-check that
finds the two sources agree, plus the places where RWE departs from both. Differences in how
the screen looks are **presentation only**; the one difference that reaches the simulation is
marked.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ingame-info-panel.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of its source
  links were followed. Addresses are the ones the chapter names; rules are restated in prose.
  The spec-room discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`)
  was read, read-only. BT-6 (sonar and radar range boundary), BT-7 (the model-height field),
  BT-68 (the recent-damage timer) and BT-81 (four-corner sampling) touch this chapter and are
  **not re-raised**; they are cited where they apply.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No original-game play. Several
  findings below are about what a player sees; the Gaps table says which need a play check.
- **The RWE half** is a read-only audit of `src/rwe/sim/GameSimulation_visibility.cpp`,
  `src/rwe/game/GameScene_render.cpp` (the HUD and footer, about 164-510, and the world pass),
  `GameScene_commands.cpp` (the hover and visibility predicates), `GameScene_util.cpp`,
  `UiRenderService.cpp`, `resource_io.cpp` and the shaders at the working tree of 2026-10-17.
  Nothing was built, run or changed. Another agent had uncommitted edits in the tree, so every
  line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Who emits contacts: the displayed player's units only
3. The detection visitor: stealth, height tests, ranges
4. Jammers, the cloak proximity fuse and direct sight
5. Battle composition order
6. The resource HUD
7. The footer: identified units, unknown contacts, orders, features, build buttons
8. Bars, outlines and model primitives
9. Light bars, status strip, messages and diagnostics (not applicable)
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 1-3 contact flags, radar and sonar visitor, jammers, restoration | `TOTALA-EXE-VISION.md` "The visibility pass, `0x467440`" and section 17 | agrees; RWE shares allies' emitters by design (rule 2), and differs on what a nearby unit does to a cloak (rule 4) |
| 4 battle composition order | `TOTALA-EXE-RENDER.md` section 5 (ten particle layers, airborne pass) | agrees in outline; rule 5 |
| 5 resource HUD smoothing | none | rule 6 |
| 6, 7 footer, identified and unknown units | `TOTALA-EXE-INTERFACE.md` section 99; `-VISION.md` section 18 | agrees; RWE leaks identity on the minimap (rule 7) |
| 8 feature GAF drawing | `TOTALA-EXE-WRECKS.md`, `-RENDER.md` section 52 | not re-audited |
| 9 bars, outlines, primitives | `-RENDER.md` section 52 (primitives); bars are new | rule 8 |
| 10 light bars, status, messages | `-INTERFACE.md` section 108 (the Space strip) | rule 9 |
| 11 diagnostics | none; developer displays | rule 9 |

## 2. Who emits contacts: the displayed player's units only

**Reference (section 2).** The pass returns when fewer than two players exist and otherwise
computes contacts for **the displayed player alone**. Its second loop, the emission loop,
visits **the displayed player's own units** (live, not flagged for death, activation bit
set, a nonzero radar or sonar range) and queries around each with the larger of the unboosted
radar and sonar ranges. Other players' units get contact bits only through the first loop,
which grants **both** contact bits to the viewer's own units, to units of players whose alliance
table points at the viewer and whose "share" flag is set, and to everything when the viewer
override is on.

**Register.** `TOTALA-EXE-VISION.md` ("The visibility pass") says the same thing in the
register's words: loop 0 gives an own or allied-sharing unit both bits; loop 1 takes "each of
the viewer's live units that is switched on". The single-viewer shape is recorded there as
deliberately not ported (RWE computes per player).

**RWE: DIVERGENCE (documented as a design choice, and the register's `[open]` is narrowed
by this chapter), and the difference reaches the simulation through sonar.**
`updateVisibility` (`GameSimulation_visibility.cpp` about 500-640) adds each unit's radar and
sonar detector to **every player allied with the owner**, not only to the owner
(the `arePlayersAllied` loop at about 580-610). A teammate's radar dish therefore paints enemy
blips on your minimap, and a teammate's sonar station puts a submerged enemy on **your**
sonar contact set. This is the choice `ROADMAP.md` records on 2026-09-03 ("an ally's dishes fill
your radar picture, which is what an alliance is for") and `TOTALA-EXE-COMMANDS.md` ("The
sharing commands", "Mapping") leaves **[open]**: "what a default alliance shares without any
toggle is not settled by this account, and RWE's teams share sight and radar unconditionally".

What the chapter adds is the rule the open item was missing. The flag it names in the first
loop, the owner's player-record byte `+0x97` **bit 6**, is the register's `+ShareRadar` toggle
(`0x40`). It gates only whether an **ally's own units** go onto your contact picture (their
icons), by the owner's alliance table toward you. It has nothing to do with whose dishes emit:
the emission loop visits the displayed player's own units in the chapter and "each of the
viewer's live units" in the register. So both static sources say an ally's radar does **not**
paint your enemies, whatever the toggle. (Neither says what an alliance does when nobody has
typed the command; the toggle is an option of the lobby's state, default unrecorded.) The
radar half is display only (the sim never asks for a radar contact; `canDetectUnit`'s own
comment says so). The **sonar half is hash-visible**: `canSeeUnit` (about 213) lifts the
below-the-surface veto from the viewer's sonar contacts, so a submarine heard only by an ally
can be targeted by a unit that, in the original, could not see it. The documented choice was made
for the picture; its consequence for targeting was not recorded. `+ShareRadar` itself is not
built (RWE answers "Unknown command", `-COMMANDS.md`).

**Jammers: MATCHES.** The jammer loop (about 612-640) skips only the jammer's owner, so an ally's
jammer blanks your radar as in the original (`-VISION.md` records this quirk as followed on
purpose). RWE also skips an unfinished jammer where the chapter's jammer loop repeats no
"flagged for death" test; a dying jammer for the last tick or two is not a case that can matter.

## 3. The detection visitor: stealth, height tests, ranges

**Reference (section 2).** The visitor ignores empty slots, the viewer's own units and
definitions with the stealth bit. Distance is the sum of two squared differences taken in
whole world units (each truncated before the sum). **Sonar contact**: target Y at or below the
sea, and squared distance strictly below the sonar range squared. **Radar contact**: target Y
plus the model's maximum Y at or above the sea, and squared distance strictly below
`(radar range + 2 x the detector's whole altitude)` squared. The outer sweep uses
`max(radar, sonar)` unboosted, so the lift cannot enlarge it. A half-submerged ship can be both
contacts. Stealth suppresses both.

**RWE: MATCHES the shape, with the boundary already settled elsewhere.**
`updateVisibility` takes `min(radar + 2 x altitude, max(radar, sonar))` as the radar range
(about 587-600), sonar flat, stealth excluded (about 675-680), `modelTop >= sea` for radar and
`y <= sea` for sonar (about 687-690). The boundary (inclusive in RWE, strict in the original for
sonar and for radar when the lift does not bind the sweep) is **BT-6, resolved, not re-raised**;
the model-height field is **BT-7, resolved**. The one nuance the chapter adds that BT-6 does not
mention: RWE lifts radar by twice `max(position.y, 0)`, unfloored (a fractional altitude counts),
where the register says the detector's **whole** altitude (floor of y, signed, so a detector
below the origin would shorten its own radar). A fraction of a world unit changes the squared
range by a few units and a negative altitude needs terrain below zero: edge cases, recorded for
completeness.

## 4. Jammers, the cloak proximity fuse and direct sight

**Reference (section 3).** Jamming runs after detection: the radar jammer clears the radar
bit and sets the jam marker on every unit in its radius, the sonar jammer likewise for sonar,
neither checking owner, stealth or alliance beyond the source filter. A third pass, for live
units with the cloak capability flag whose owner is a human or computer controller, asks
whether a **ready unit** stands within `mincloakdistance`; if so it sets the unit's work
deadline to the current tick **plus 90** and a proximity marker. Finally, units without the radar
bit and not cloaked get the radar bit again when the viewer's **direct sight** sample passes, so
jamming never hides what you can see; the sonar bit is not restored.

**Register.** `-VISION.md` agrees on every number: jammers hide and never fake, run after
detection, ignore ownership except the viewer's; the fuse sets the tick plus `0x5A` (90) and is
"a proximity fuse on the unit's own cloak"; loop 4 restores bit 8 from line of sight.

**RWE.** Jam-then-detect order: **MATCHES** (about 692-700; direct sight is `canSeeUnit`, which
jamming does not touch). The 90-tick tail: **MATCHES** (`CloakSuppressionTicks`, about 21).

- **Who counts as an intruder: UNCLEAR, and hash-visible.** The register says the fuse asks
  "whether any live **enemy** of that owner is within that distance" (`0x40B0D0`), the chapter
  calls the query `HasReadyUnitInRange`. Neither says what *enemy* or *ready* test. RWE's
  `updateCloakSuppression` (about 719-760) counts **any live unit not owned by the cloaker**,
  so an **allied** unit walking up to your cloaked Commander decloaks it, and so does an
  **unfinished** enemy (a nanoframe). The cloaked flag feeds `canSeeUnit`, which feeds
  targeting, so this is not presentation. A spec-room read of `0x40B0D0` settles it (does it
  skip allies; what does "ready" exclude).
- **The range comparison** is inclusive (`<=`) in RWE; the chapter says "in range" without a
  comparison. UNCLEAR.

## 5. Battle composition order

**Reference (section 4).** One frame is built in this order: terrain tiles (and the debug
grid); resource panel, footer and the pending minimap; then the world: per-row unit lists from
the previously computed visible ids, rows by whole Z over 16 plus a margin; features scanned
over a rectangle with margins, low features drawn at once and tall ones in the row pass; **particle
lists 0, 1, 2 before low features, 3 and 4 after**; the row pass draws mode-1 (ground) units
and selection outlines, then tall features of that row; list 5; then, with object drawing on,
list 6, projectiles, explosions, list 7, the remaining (airborne) units, list 8, order overlays
and own-unit health and group labels, list 9 (inside that block); **fog afterwards**; then the
selection-drag and build-preview rectangles, score/status/network/messages, debug data, pause,
menus, profiling bars; the cursor is restored and the frame flipped.

**Register.** `-RENDER.md` section 5 gives the same skeleton (16-unit Z buckets, ten hand-placed
particle layers, the airborne pass after layer 7, damage smoke on 9 over everything including
health bars) and records that RWE draws sprite particles over the finished frame with the
depth test off, "confirmed against the original 2026-09-21" for scenery against smoke.

- **Layer assignment of the nano spray: UNCLEAR.** The chapter says existing call sites use
  layer **6** for construction and reclaim nanos; the register's `-RENDER.md` section 5 puts
  **weapon impacts** on 6 and never names the nano spray's layer. Both can be true. Listed as a
  question, not a conflict.
- **Vent steam and burning-feature smoke (layers 4 and 5): DIFFERS, presentation only.** In the
  original these sit before the ground row pass, so a unit or a tall building drawn in that
  row covers them. RWE draws all sprite particles over everything, which the register
  endorses for layer 9 and never examined for 4 and 5. The stale sentence in
  `TOTALA-EXE-DATA.md` (near line 248, "RWE depth-tests particles against world Y") also
  contradicts `-RENDER.md` section 5; the code does not depth-test them (`inWorld` is set by
  nothing). A text fix, not a code fix.
- **Fog is the last world layer in the original: DIFFERS in structure, UNCLEAR in effect.** The
  original lays the fog frames over units, features and effects; RWE folds it into the terrain
  pass (`drawMapTerrain(..., fogOverlay)`, `GameScene_render.cpp` about 1601) and tints
  remembered features and wrecks per object (`fogged` in `unitTexture.frag`), with sprite effects
  gated rather than dimmed (rule in `BT-GRAPHICS-PARTICLES-PARITY.md`). Whether a remembered
  building is dimmed the same way needs a side-by-side look.
- **Row bucketing by whole Z over 16** is the original's painter's sort. RWE has a depth buffer;
  `-RENDER.md` section 5 records that.
- **The interface overlays after fog** (drag box, build preview, status): RWE draws these in the
  chrome pass after the world; the order among them is not audited.

## 6. The resource HUD

**Reference (section 5).** The renderer takes a snapshot of the displayed player's resources.
Displayed **amounts move toward the integer target by one eighth of the integer difference, at
least one unit a step in either direction, capped by storage**; fractions are discarded. Income
and use refresh when a per-player deadline is strictly below the game tick, the deadline then
advancing 30. Energy shows integers, with a **K suffix above 99999 income** and a particular
negative-use threshold; metal rates show one decimal and absolute use. A positive sharing
threshold below the current amount adds a **two-pixel marker** on the bar. Fill drawing is guarded
by storage greater than zero. (The chapter notes its private "metal" and "energy" fields are
swapped in this view; the access pattern fixes which is which.)

**Register.** `-INTERFACE.md` section 99 decodes the *footer* rates (one decimal for metal, none
for energy, clamped at zero) but not these HUD bars; nothing in the register covers the
smoothing, the K suffix or the share marker.

**RWE: MISSING (all three), presentation only.** The bars are drawn straight from the live
amount: `rectWidth = width x amount / max` (`GameScene_render.cpp` 224-265), so they jump with
each tick where the original's glide by an eighth of the gap. The text uses `formatResource`
(`resource_io.cpp`), a plain `%.0f`, and `formatResourceDelta` one decimal for metal and none
for energy; there is **no K suffix**, so a six-digit income prints in full. The share limit
set by `+SetShareEnergy` and friends has no marker on the bar. Income and use do refresh once a
second from the economy settle (`GameSimulation.cpp` about 4640), matching the 30-tick cadence
in effect. The exact K rule (which figure, which negative threshold) is not fully stated by the
chapter: **UNCLEAR**, wants a spec-room read before it is built.

## 7. The footer: identified units, unknown contacts, orders, features, build buttons

**Reference (sections 6, 7).** For a hovered live unit, the visibility service decides whether
the footer may identify it. If so: the title (or the owner's name for a commander or a
`showplayername` definition in a network game), the health bar for an own unit or a definition
without `hidedamage`, the owner logo; **for an own unit only**, the four production and use
figures, kills (singular and plural, "Veteran" after four), the translated current order name,
and a stockpile build percentage that takes priority over the order-target slot and is shown
only to the owner. Otherwise, an own unit's order target shows its title and health when it is
live **and visually identified**, again respecting `hidedamage`. An **unidentifiable** unit shows
"Unidentified object" with prefix **S when the sonar bit is set, otherwise R**; the existence of
a hover id does not authorise revealing its definition, health or production. Feature text is
suppressed by a feature flag unless debugging, uses the internal name in debug and the
description otherwise, and omits zero resource labels. A hovered build gadget shows title, integer
metal and energy cost and description (not for the CORBUILD gadget), the name truncated to
sixteen characters. Only weapons with a reload over 30 contribute reload-timer fields to the
redraw snapshot.

**Register.** `-INTERFACE.md` section 99 agrees on the footer: owner-gated rates, metal one
decimal, kills line with Veteran from the fifth kill, stockpile "Weapon" bar, second slot not
build-only, hovered feature, redraw key. `-VISION.md` section 18 records the unknown-contact text
(`R: ` / `S: ` and "Unidentified object", `0x46B6F3`) and lists it under **Not ported**.

**RWE.**

- **Layout, rates, kills, Veteran, mission line, stockpile bar: MATCHES** the register
  (`GameScene_render.cpp` 314-466; `killsCaption`, `GameScene_util.cpp` 1990, uses `kills > 4`).
  The chapter's "Veteran after four" and the register's "from the fifth kill" are the same rule.
  The commander-or-`showplayername` name rule is moot on retail data: `rev31`'s `armcom.fbi` and
  `corcom.fbi` carry both `Commander=1` and `ShowPlayerName=1`.
- **A radar-only contact shows its full identity in RWE: DIFFERS, and a leak.** The minimap
  picks under the cursor with `getUnitUnderCursor` (`GameScene_commands.cpp` about 45-85), which
  offers every unit `unitIsDetectableByLocalPlayer` says is on the picture, **radar and sonar
  contacts included**. The footer then draws that unit's real name and, unless `hidedamage`,
  its health bar (`GameScene_render.cpp` 314-336). The original refuses at the visibility test
  and prints "R: Unidentified object" (or "S:" for a sonar contact). So pointing at a blip
  reveals what it is and how hurt. The register lists the *text* as not ported but not the
  disclosure; the chapter's sentence "a hover id does not authorise revealing its definition"
  states the rule. Presentation only; fix is to test visibility (not detectability) before
  drawing the footer, and to add the two-line unknown state.
- **The order-target slot also uses the radar-inclusive test: DIFFERS** (about 439-441,
  `unitIsDetectableByLocalPlayer`). The chapter and register both say "visually identified". A
  builder or guard whose target is only a blip shows that target's real name and health.
- **Feature text and `nodisplayinfo`: UNCLEAR, probably DIFFERS.** The chapter says feature text
  is suppressed by "feature flag 4". The register's feature flag word has `nodisplayinfo` at bit
  10 and no feature flag numbered 4 that fits (`-DATA.md`, table near 100); the chapter's
  numbering is evidently its own. RWE parses `nodisplayinfo` into the sim definition
  (`GameSimulationLoader.cpp` about 250) but the footer never reads it (about 468-492), so every
  feature gets text. **46 shipped features set it** (ice chunks, ice and lava vents, lava spires,
  moon craters; census of `totala1`'s `features` folder). A spec-room read of the footer's feature
  branch (`0x46B78C`) should confirm that the suppressing bit is that one.
- **Build gadget CORBUILD exclusion: UNCLEAR, not reached.** RWE has no gadget of that name.
- **Reload timers in the redraw key**: the register skips weapons "under thirty ticks"
  (`-INTERFACE.md` section 99), the chapter says only weapons "greater than 30" contribute. The
  two disagree on a weapon of exactly 30 (and on units). The counters are collected and never
  drawn, so nothing is observable; **CONFLICT in the text only** (listed in
  `BT-NEW-CONFLICTS.md`).

## 8. Bars, outlines and model primitives

**Reference (section 9).** The in-world hit-point bar (`0x46a430`) skips nonpositive health and
draws a **34-by-4** framed bar with **32 interior pixels** scaled by health over maximum; the colour
changes strictly above integer one-third and two-thirds of maximum health; zero maximum is
unguarded. The percent bar clamps to 100. The selection box is a bounding-footprint outline of
the model's lower-Y corners, gated by a UI option bit. The model primitive drawer starts at
primitive 1 whenever a first-face index is not -1 (not at that index), draws flat polygons for solid
colour primitives and only **quads** for textured ones.

**Register.** The primitive rules are `-RENDER.md` section 52 (and the loader swap that makes
"skip primitive 0" equal to "skip the declared plate"). The footer health bar colours (interface
slots `0x0A` over `0x04`) are `-INTERFACE.md` section 99. The in-world bar's geometry is **not in
the register**.

**RWE.**

- **Footer bar, quad and primitive rules: MATCHES** (`drawHealthBar2`, `UiRenderService.cpp` about
  306; primitives per the register).
- **The in-world health bar: DIFFERS in size and edge rule, presentation only.**
  `UiRenderService::drawHealthBar` (about 283-304) draws **35 by 5** with a one-pixel border (33 by 3
  inside) and fills `1 + floor(fraction x 32)` pixels, so a nearly empty unit still shows one
  pixel; the colour flips at `fraction < 1/3` and `< 2/3` in floating point. The chapter's bar is
  34 by 4 with 32 inside, and its thresholds are on **integer** one-third and two-thirds of the
  maximum health, strictly above. RWE's own offset (`x -= 17`) is half of 34, which hints the width
  was once 34. Neither the fill rule at zero fraction nor the position offset (`y + 8`) is stated
  by the chapter. The bar is drawn only after the backquote toggle (`healthBarsVisible`,
  default off); the chapter says the original draws own-unit bars under an option, so the default
  is itself **UNCLEAR**.
- **The selection box (`0x46a530`)**: the register's colour table (`-RENDER.md` section 50,
  slots 1, 3, 9, 10) covers the 3D selection brackets; the chapter's footprint rule (all four lower-Y
  corners of the model bounds, gated by a UI option bit) was not compared with RWE's selection
  drawing. Not audited.

## 9. Light bars, status strip, messages and diagnostics (not applicable)

**Reference (sections 10, 11).** The light bars are the side-panel frames; the status panel
slides by about a third of the remaining distance a step with a one-pixel minimum, shows
simulation time at 30 ticks a second, the local unit count over the limit, and the effective
speed against a normal of 10, plus the requested speed when different. `MarkRecentlyDamaged`
sets the byte at `+0xfa` to **240**. `ShowGameMessage` routes mode 0 to the message list with
parameters (16, 0, 10) and mode 1 to a message box. The two state probes, the network bar
(scaled so a full bar is **5600 bytes** per rate unit, against a "56 K/s" comment) and the
profile bars are developer displays.

**RWE.** The Space strip is `-INTERFACE.md` section 108 and is built (`statsBarSlide`,
`GameScene_render.cpp` about 563-600). The damage timer is **BT-68, resolved** (240 ticks, read
only by the blink in the minimap loop): RWE has no such timer, recorded in
`BT-MAP-RADAR-PARITY.md`. The developer displays have no counterpart and need none; RWE's
network overlay (F9, `docs/NETWORK-OVERLAY.md`) and the F10 panel replace them. **Not applicable.**

---

## 10. Gaps to fix

Ranked by what a player would notice, or what could move the simulation. Only rows 1 and 2
touch the simulation; the rest is presentation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Allied radar and sonar emitters are shared** (a documented choice, ROADMAP 2026-09-03; `-COMMANDS.md` leaves the default `[open]`). Both static sources say only the viewer's own units emit contacts and that `+ShareRadar` governs only allies' own icons. In RWE a teammate's sonar puts a submarine on your sonar set, which lifts the below-surface veto in `canSeeUnit` and so changes targeting. | DIVERGENCE (documented for radar; the sonar targeting effect is not recorded), hash-visible | maintainer decision; a play check of two allied players, one with sonar, would show the original |
| 2 | **Cloak proximity fuse counts allies and unfinished units.** The register says "live enemy"; RWE decloaks on any other-owner unit within range. `0x40B0D0` is not decoded for the two tests. | UNCLEAR, hash-visible | spec-room read of `0x40B0D0` (enemy test, ready test) |
| 3 | **Hovering a radar or sonar blip on the minimap reveals its name and health bar**; the order-target slot does the same. The original prints "R: Unidentified object" and keeps identity back. | DIFFERS (a leak), presentation | code: visibility test before the footer and the target slot, plus the unknown state (`-VISION.md` section 18 lists the text as unported) |
| 4 | **Features marked `nodisplayinfo` (46 shipped) still get footer text.** | UNCLEAR, probably DIFFERS | spec-room read of the feature branch at `0x46B78C` to confirm the bit |
| 5 | **Resource HUD has no one-eighth glide, no K suffix and no share-limit marker.** | MISSING | spec-room read for the K rule; then code |
| 6 | **In-world health bar is 35 by 5 and rounds its edges differently** from the chapter's 34 by 4 with integer thresholds. | DIFFERS | a spec-room read of `0x46a430` for the exact fill and offset |
| 7 | **Vent steam and feature smoke are drawn over units and tall buildings**; the original draws layers 4 and 5 before the row pass. | DIFFERS, presentation | decision (the register endorses over-everything for layer 9 only) |
| 8 | **Stale text in `TOTALA-EXE-DATA.md`** (near 248) says RWE depth-tests particles; `TOTALA-EXE-RENDER.md` section 5 corrected that. | documentation | fix the sentence |
| 9 | **Reload-timer threshold** ("under 30" versus "over 30") disagrees between register and chapter; never drawn. | CONFLICT in text only | none needed |
| 10 | Detector altitude is clamped at zero in RWE; the register says the signed whole altitude. | DIFFERS, mod data only | none |

No source file was changed.
