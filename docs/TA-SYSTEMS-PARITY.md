# Time, simulation order and economy parity audit

Spec-room finding, 2026-10-16. Like `NAVAL-SYSTEMS-PARITY.md`, it allocates no
section number in the main register. This is the finding that implementation
work cites for tick order, pacing, pause, the economy, and the few start-up
rules that change gameplay values, per `AGENTS.md` and ADR-0004.

## Source and evidence class

- **Source.** The prose chapters of the TA: Byte Tactics systems manual, at
  commit `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\`): `index.md`,
  `01-startup-assets-lifecycle.md`, `02-time-simulation.md`, `03-economy.md`,
  and a skim of `module-inventory.md`. Nothing else under that tree was opened
  for this audit: no source, no decompilation, and none of its source links
  were followed. Addresses below are the ones those chapters name; the rules
  are restated in prose, with constants and arithmetic, and nothing is copied.
- **Evidence class.** A **static reading** by that project: nothing in it was
  executed, and its own evidence conventions say so (confirmed, derived,
  asset-dependent, open). It is weaker than `TOTALA-EXE*.md`, which were read
  from the retail executable and in several places checked against the demo
  corpus. Where the two disagree each is stated and marked **CONFLICT**,
  with no winner picked. Where the reference is silent, an existing RWE
  finding stands and the entry says "reference silent".
- **No executed observation was used.** No original-game play and no demo
  oracle was run for this audit. Where an oracle could settle a question the
  Gaps table names it; none is claimed to have been run.
- **The RWE half** is a read-only audit of `src/rwe/sim`, `src/rwe/game`,
  `src/rwe/io/tdf`, `src/rwe/vfs` and `src/rwe/ai/classic` at the working tree
  of 2026-10-16 (HEAD `91330bbb`, branch `ai-envoy-unload-commander-flee`).
  Nothing was built, run or changed, and tests were not run; a MATCHES below
  means the code reads as the rule says.

Status words: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR** (cannot be
settled from the reference and the code alone), as in the naval audit.
**DIVERGENCE (documented)** marks a difference the register already records as
on purpose (`TOTALA-EXE.md` section 88) or as decoded and deliberately not
ported (section 91); those are not gaps. **CONFLICT** marks a place where the
reference disagrees with an existing RWE finding.

## Index

Time and simulation (reference chapter 02)

1. Four kinds of progress, ticks per second and the wall-clock quantum
2. Game speed steps
3. Step budget, fractional carry, five-step cap, adaptive speed
4. Remote-player lag policy
5. Pause
6. The major tick order
7. Order-dependent consequences
8. Per-unit order and the player and slot walk
9. Periodic cadences
10. Wind selection
11. Per-player execution, economy gate, report cadence

Economy (reference chapter 03)

12. Account model
13. Request helpers
14. Immediate spending
15. Eligible units, completion gate, ongoing energy use
16. The special production chain
17. Extractor output
18. Wind generators
19. Tidal generators
20. Difficulty income scaling
21. Settlement arithmetic
22. Account feedback and backlog
23. Storage and waste
24. Construction charging
25. Negative work: decay, cancellation, refund
26. Repair
27. Weapon costs and stockpiles
28. Cloak cost
29. Starting balances
30. Sharing
31. Persistence, reports, HUD

Start-up and assets, only where values change (reference chapter 01)

32. Archive precedence
33. Loose files and language directories
34. TDF parsing rules that affect values
35. Unit limit

36. Gaps to fix (ranked), then the conflicts in one place

---

## 1. Four kinds of progress, ticks per second and the wall-clock quantum

**Reference (02 sections 1, 3).** An application frame, a battle-loop
iteration, a simulation tick and a subsystem deadline are four different
clocks. Many gameplay quantities use 30 simulation ticks per gameplay second
(weapon time conversions among them). The pacer's elapsed quantity comes from a
timer whose unit the chapter says it did **not** establish, so "30 ticks per
wall-clock second" is stated as a gameplay convention and explicitly not as a
measured wall rate.

**RWE.** `src/rwe/sim/SimTicksPerSecond.h` defines 30 ticks a second and a tick
quantum of `1000 / 30` in integer arithmetic, which is **33 ms**. The scene
accumulator (`src/rwe/game/GameScene.cpp:625-628`, `:1022`) spends 33 ms a tick,
so at normal speed RWE dispatches 30.30 ticks a real second, and the HUD game
clock (`GameScene_render.cpp:604`, game time over 30) runs about one percent
fast against a wall clock. The COB piece step and clock also use the 33 ms
quantum (`GameSimulation.cpp:5179`); `TA-DEMOS.md` (the "lead, not a finding"
under the build-stance rows) records that a 33 ms script clock reproduces two
corpus builds exactly. **UNCLEAR**: the gameplay convention matches (30 ticks a
second in every conversion), but the original's wall-clock tick length is not
established by the reference and RWE's has never been measured against the
original. `TOTALA-EXE-EXTERNAL.md` (not read by this project) gives a fixed
33.33 ms for the movie player only.

**Remaining uncertainty.** Whether the original's normal-speed game clock
advances 30.0 or 30.3 ticks a real second. A stopwatch against the in-game
"Game Time" readout in retail TA settles it in five minutes.

## 2. Game speed steps

**Reference (02 section 3).** The speed setter clamps to 1 through 20, 10 is
normal, and the pacer's initial rate factor is the effective step times 0.1.

**RWE. MATCHES.** `src/rwe/game/GameSpeed.h` holds 20 steps, 100 to 2000 per
mille, default index 9 (step 10), displayed as step minus 10; the scene
multiplies elapsed time by it (`GameScene.cpp:625-628`). `TOTALA-EXE-INTERFACE.md`
section 70 agrees.

## 3. Step budget, fractional carry, five-step cap, adaptive speed

**Reference (02 sections 4.1, 4.3).** Whole steps are the floor of elapsed
timer ticks times the rate plus a carried fraction; the residual is stored.
Negative counts clamp to zero; pause forces zero and returns; otherwise a
budget over **five** is capped to five. Capped budgets increment a hysteresis
counter; above **ten** the counter resets and the effective speed drops by
one (if above one). A non-capped budget decrements the counter; below
**minus one hundred** it resets and the effective speed rises by one toward
the configured speed. Steps discarded by the cap are **not** added back to the
fractional carry: it is an adaptive budget, not an unlimited catch-up.

**RWE. DIFFERS (architectural).** RWE accumulates integer milliseconds and
dispatches through `FrameScheduler` (`src/rwe/network_util.h:190-240`) with a
cap of ten ticks a frame (`GameScene.cpp:1019-1022`; more for replay seek and
own-clock catch-up), and drops the backlog the cap refused
(`ticksLostToCap`, `:1050-1052`). Its speed governor is a sustainable-speed
estimate that runs from tick cost (`estimateSustainableSpeedPermille`,
`:1063`), which in a network game is also what the peers agree on. The
original's five-step cap and 10-up/100-down hysteresis are not ported. This is
a consequence of RWE being lockstep where TA is owner-authoritative
(`TA-NETWORK.md`), and is not listed in section 88. Treat as **DIVERGENCE
(architectural, undocumented)**; no player-visible rule depends on the exact
constants except how fast a slow machine falls out of normal speed.

**Remaining uncertainty.** Whether "drops what the cap refuses" is the same
policy at the margin. Not worth an original check.

## 4. Remote-player lag policy

**Reference (02 section 4.2).** The pacer scans active remote-controlled
(type 3) players that still have units and finds the slowest synchronisation
tick below the local tick. At a lag of **900 ticks or more** it reduces the
effective rate: the lag used is capped at **3600**, the scale is proportional
to `3600 - lag` with a minimum of **0.01**, and the lagging indicator clears
below the threshold. These are tick-state comparisons, not ping.

**RWE. DIVERGENCE (architectural).** RWE's peers hold each other in lockstep
(`GameScene.cpp:1001` averages scene time; the gate gain constants are in
`network_util.h:196-198`). The original's rule slows the local game when a
remote player's own game clock is 30 seconds or more behind; RWE never lets a
peer get that far behind. In own-clock mode (`--ta-host`), RWE follows the
peer rather than the reverse (`GameScene.cpp:1013-1021`), so the rule applies
to the real `TotalA.exe` joiner's side and not to RWE. No action.

## 5. Pause

**Reference (02 section 5).** In the network-enabled branch, when no steps are
pending and the game is paused, queued packets can still be flushed and
received and periodic probes sent. Pause forces the step count to zero;
interface, drawing, edge scroll and the application loop continue.

**RWE. MATCHES.** `GameScene.cpp:594-606` zeroes the accumulator (also dropping
a fractional tick) and takes pause from the shared pause state in a network
game, while rendering, chat and camera input continue. `TOTALA-EXE-INTERFACE.md`
section 70 is the same finding and adds that menus never pause a multiplayer
game. A paused RWE peer still services its network thread; the reference's
"flush, receive, send probes" has no separate counterpart to check.

## 6. The major tick order

**Reference (02 section 6; anchor `0x495490`).** For each pending step, in
order: (1) increment the game tick; (2) network form only, receive packets;
(3) update all units; (4) update projectiles; (5) update explosions (debris,
exploded pieces); (6) update players (player status, path-search service,
economy, local sensor work); (7) update features; (8) step global GAF
sequences; (9) update wind; (10) update meteors; (11) camera follow;
(12) particles; (13) blink state; (14) network form with qualifying state:
share resources and flush queued packets. After all steps, post-step hooks and
message and marker expiry run once.

**RWE.** `GameSimulation::tick` (`GameSimulation.cpp:5101-5257`) runs, in this
order: increment; AI controllers; wind; mission rules; automatic sharing;
**economy settle** (`:5128`); path finding; mission scripts; the behaviour
pass over every unit; piece animation for every unit; COB scripts for every
unit; carried units; self-repair; water damage; nanoframe decay; self-destructs;
**projectiles**; burning and falling features; feature regrowth; victory;
dead-unit deletion; spawning of new units; cloak suppression; visibility;
recorders. Particles, GAF sequences, blink and camera follow are scene-side and
not part of the sim.

- Units before projectiles: **MATCHES** (a projectile created in the unit pass
  is stepped the same tick).
- Economy: reference phase 6, after units and projectiles; RWE settles
  **before** the unit pass of the tick. **DIVERGENCE (documented)**:
  `TOTALA-EXE-ECONOMY.md` section 111 row 2 records it, notes both orders count
  a tick's lathe into the same second, and that the stall episodes replay RWE's
  order against the corpus. What the section does not say is that anything else
  reading the stockpile on a settle tick (a shot, a cloak charge, an automatic
  gift) sees the stockpile one phase earlier or later than the original.
- Wind: reference phase 9, after players; RWE updates it first (`:5115`). On a
  tick that is both a settle and a wind change, the original settles on the old
  wind fraction and RWE on the new one. Wind changes every several seconds and
  settles every second, so about one change in thirty is affected, by one
  second of one wind generator's output. **DIFFERS**, negligible.
- Path finding: reference puts the path-search service at the top of the player
  update, after units; RWE runs it before the unit pass (`:5132`). A path
  requested in tick T is served at the end of T in the original and at the start
  of T+1 in RWE, and in both cases the requesting unit first sees the answer in
  T+1. **MATCHES in effect.**
- **CONFLICT, phase order.** The reference puts explosions between projectiles
  and players, and players **before** features. `TOTALA-EXE-MOVEMENT.md` section
  132 ("Where the strategic work sits") and `TOTALA-EXE-ECONOMY.md` section 111
  put the per-player pass **after** the projectile and feature passes. Both
  readings agree units come first and the player pass after the projectile
  pass; they disagree on features before or after players, and the reference
  has a debris phase the register does not name. What reads feature state in
  the player pass is the economy's reclaim credit only, so the visible
  consequence is at most a one-tick offset in a feature's removal against a
  settle. Left open.
- AI controllers: the reference places the computer player's strategy inside
  the player phase, so what it decides is read the next tick
  (`TOTALA-EXE-MOVEMENT.md` section 132). RWE runs them first and queues
  commands for the next tick's drain (comment at `:5105-5109`).
  **MATCHES in effect.**
- Sharing: reference phase 14, after every world phase; RWE runs it just before
  the settle (`:5124-5126`, `ResourceSharing.h`). **DIFFERS**, see rule 30.

**Remaining uncertainty.** Whether the reference's phase list is a single
flat call sequence (it says it is a direct call sequence and that called
subsystems can recurse); the register's section 111 read the same routine in
the executable, so the features-versus-players order is worth a direct look
before anyone moves either.

## 7. Order-dependent consequences

**Reference (02 section 7).** Economy runs after unit actions, so a
construction request consults the account's **prior** shortage state and adds
new demand for the later settlement; the request helper is not a fresh
allocation. Sensors are updated in the player phase, after units and
projectiles, so targeting uses maintained information rather than a rebuilt
sensor state before each shot.

**RWE. MATCHES.** The request gate is the unit's carried debt, written only at
the settle (`UnitState.cpp:699-716`). Sensors: RWE computes visibility at the
end of the tick (`GameSimulation.cpp:5242`) and answers `canSeeUnit` live;
section 88 records the original's 30-tick enemy-list snapshot as a deliberate
difference. **DIVERGENCE (documented)** for the staleness.

## 8. Per-unit order and the player and slot walk

**Reference (02 sections 8.1-8.2).** Players are walked in slot order, each
player's allocated unit interval in slot order; a unit with a nonzero
definition index counts. Controller types 1 and 2 take the weapon, self-repair,
water-hazard and order branches; type 3 (remote) takes a different, smaller
role. Within a unit: wind-generator script notices; weapons; scripts; damage and
transfer timers; selection and eligibility flags; health-percentage refresh
every 30 ticks; water hazard on the 30-tick cadence; self-repair under the
8-tick mask; primary orders; secondary orders; motion; height; death. Weapon
update precedes script execution and primary orders.

**RWE. DIVERGENCE (documented).** This is `TOTALA-EXE-MOVEMENT.md` section 132,
which reads the same routine, finds the same order, and was **deferred on
purpose** (section 91). RWE visits units in id order, runs orders then weapons
then motion, and runs scripts and piece animation in all-units passes after
(`GameSimulation.cpp:5165-5190`). The reference agrees with section 132 on
every stage listed; it adds nothing new that the register lacks, and it does
not name the piece-animation-after-threads order that section 132 does. Not a
gap; the acceptance check section 132 names (the three demo scorers) stands.
Cadences within it are rule 9.

## 9. Periodic cadences

**Reference (02 section 10).** Health-percentage refresh every 30 ticks;
environmental water damage every 30 ticks; self-repair when the tick satisfies
the 8-tick mask; particle expiry by deadline; wind change scheduled several
gameplay seconds apart.

**RWE.** Water damage: **MATCHES** (`GameSimulation.cpp:3424-3468`, per the
naval audit rule 14). Self-repair every 8th tick, one hit point and one energy
through the energy-only request: **MATCHES** (`:5010-5067`, with the full
decode in the comment, `TOTALA-EXE.md` section 94 and
`TOTALA-EXE-ECONOMY.md`). Health-percentage refresh is cosmetic and not
audited. Wind: rule 10.

## 10. Wind selection

**Reference (02 section 10; 03 section 6.2).** A wind change is scheduled by
game tick, picks a speed from configured bounds, picks a direction **only when
the speed is non-zero**, computes the vector and a normalised fraction (speed
divided by a denominator initialised to **5000** at battle load, capped at
one). The "new wind" notification is set on the selecting call and cleared on
intervening calls; the stored vector and fraction are **not** zeroed, so
clearing the notification is not calm wind.

**RWE.** `GameSimulation.cpp:4298-4321`. The fraction is speed over 5000 with a
cap at one, and scripts are told on a change only: **MATCHES**. Two
differences: (a) RWE draws the direction even when the speed is zero (same
random stream consumed differently; not observable except in the seeded
sequence and in `SetDirection` being sent with a stale angle), and (b) the
interval is hard-coded at 5 to 14 seconds (`:4303`), where the reference says
only "several gameplay seconds". **UNCLEAR**: the reference gives no constant
and no register finding records where 5 to 14 comes from.

**Remaining uncertainty.** The interval and its distribution decide how much a
wind generator's income varies second to second. A static read of the wind
selector (reference anchor `0x490C40`) is a spec-room item.

## 11. Per-player execution, economy gate, report cadence

**Reference (02 section 9).** The player update services path searches first,
then enumerates player records. The economy runs only for an eligible active
record, an accepted team state, a qualifying unit-count and creation state,
controller type 1 or 2, and the shown endgame conditions: an eliminated or
watching slot does not settle. Multiplayer economy **reporting** is sent every
fourth qualifying local pass, distinct from settlement and HUD refresh.

**RWE.** The settle runs for every player the machine simulates locally
(`GameSimulation.cpp:4335-4340` and `:4470-4475`, `simulatesLocally`), which is
the type 1 or 2 rule (a Remote player belongs to its owner's machine). The
reference's unit-count and endgame qualifiers have no counterpart: a player with
no units keeps settling at zero production and its stockpile persists.
**UNCLEAR** (no visible effect on an alive-or-dead player, as the stockpile of
the dead is not shown or spent). The cadence: reference silent on the settlement
period itself (it states only "every fourth local pass" for the report);
`TOTALA-EXE-ECONOMY.md` section 23 and 111 say once every 30 ticks for every
player, and `TA-DEMOS.md` has the corpus's 120-tick `0x28` cadence, which is
four of them. RWE settles when `gameTime % 30 == 0` (`:4333`): **MATCHES** the
register; the reference corroborates the report cadence (4 x 30 = 120) and
nothing contradicts the 30.

**Remaining uncertainty.** None that affects a player.

---

## 12. Account model

**Reference (03 sections 1-2).** The player's balances are fed by per-unit
accounts and by one extra player-level account. Each resource has six
floating-point fields: produced, used, demand (header name "paid"), backlog
("stall"), previous produced, previous used. "Paid" must not be read as already
debited at request time; the settle apportions demand and carries the unpaid
part as backlog.

**RWE. MATCHES.** The register's table (`TOTALA-EXE-ECONOMY.md` section 23,
"The per-unit block") is the same layout: produced, asked, granted, still owed,
and the two display copies. RWE holds them as `UnitState` request and debt
buffers plus the player's own block (`GamePlayerInfo.h:160-161`,
`UnitState.h:961-962`). Nothing to change.

## 13. Request helpers

**Reference (03 section 3).** The energy-only request adds the amount to usage
first and fails if the account already has positive energy backlog; otherwise it
adds the amount to demand. It does **not** look at the player's balance. The
paired energy-and-metal request adds both to usage and succeeds only when both
backlogs are non-positive, then adds both to demand; if either backlog is
positive neither demand field is raised. A metal shortage therefore stops work
that also needs energy, and conversely.

**RWE. MATCHES.** Energy-only: `GameSimulation::addEnergyRequest`
(`GameSimulation.cpp:2665-2683`) records the desire, tests energy debt, then
queues the demand. Paired: `GameSimulation::addResourceDelta` and
`UnitState::addResourceDelta` (`:2610-2640`, `UnitState.cpp:677-697`) record
usage, refuse on either debt (`ResourceDebtGate::EnergyOrMetal`), and queue both
demands only on success. The settle sweep's own `EnergyUse` request passes
`EnergyOnly`, as `TOTALA-EXE-ECONOMY.md` section 111 row 3 found and fixed.

Small difference: RWE passes the builder's **un-clamped** step cost as the
"apparent" usage and the clamped step as the demand (`UnitBehaviorService.cpp:5422-5427`,
`:6092-6097`). On the final tick of a job the usage figure (display only) can
exceed the demand; the original passes one amount for both. **DIFFERS**,
display only, no effect on the settle.

## 14. Immediate spending

**Reference (03 section 4).** Energy and metal spend helpers test the
corresponding current balance and, on success, subtract and record usage;
otherwise they fail with no debit. The paired helper requires **both** balances
to cover the cost before subtracting, subtracts energy and records it, then
handles metal in a following branch. A positive balance can therefore allow an
immediate shot while a construction account has backlog, and a construction
request can be accepted from a debt-free account that the later settle finds
under-funded. Negative or non-finite costs are not sanitised.

**RWE. MATCHES.** `GameSimulation::chargeStockpile` (`:2642-2663`): both
balances must cover the cost, then both are debited, with the desire booked for
the display and nothing placed in the request buffers. This is the fix in
`TOTALA-EXE-ECONOMY.md` section 111 rows 6 and 7. The "following branch" for
metal in the paired helper is not described in enough detail by the reference to
audit; **UNCLEAR**, with no known effect. Costs come from FBI and weapon TDF
reads and are not clamped at the spend; the security audit's hardening of
mod data is separate.

## 15. Eligible units, completion gate, ongoing energy use

**Reference (03 section 5.1).** The player pass resets the total capacities and
the aggregate production, use, demand and backlog values, then enumerates live
units. Runtime classes take different ongoing energy-use branches. For the
stationary or building branch, activation controls the special production
chain. Other qualifying units request their ongoing energy use when **active or
moving**. A unit whose remaining build fraction is **exactly zero** adds its
ordinary `energymake`, `metalmake` and its storage; an unfinished storage
structure contributes none of it. The special chain and the completion-gated
income and storage block are separate parts of the function.

**RWE.** Completion gate and "unfinished storage contributes nothing":
**MATCHES** (`GameSimulation.cpp:4359-4369` for storage, `:4459-4462` for
`energymake` and `metalmake`, both behind `isBeingBuilt`). Ongoing energy use:
`:4389-4396` charges `energyuse` for an **activated** unit only. The "or moving"
half for no-switch units is not modelled. **DIVERGENCE (documented)**:
`TOTALA-EXE-ECONOMY.md` section 111 row 5, decided 2026-09-17 not to chase
(four Escalation metal makers are the only known units affected). The reference
independently states the same rule, which is mild corroboration of the register's
reading. A negative `EnergyUse` as production (the solar collector) is
**reference silent**; the register's reading (section 23, "What counts as
production") stands.

## 16. The special production chain

**Reference (03 section 5.2).** For the qualifying active building branch the
updater first requests its configured energy use, then takes the **first**
applicable special producer, in this priority: positive extraction rating (only
if the energy request succeeded); non-zero metal-conversion output (only if it
succeeded); positive wind rating; positive tidal rating. A definition combining
several fields gets only the first. The wind and tidal branches do not test the
earlier energy-request result.

**RWE. MATCHES on shipped data, DIFFERS structurally.** Each of the four is
computed independently (`GameSimulation.cpp:4398-4423`), and the wind and tidal
branches are not gated on being powered, as the reference says. The exclusive
chain is not enforced; `TOTALA-EXE-ECONOMY.md` section 23 ("Not ported, and
why") records that no shipped unit sets more than one. **DIVERGENCE
(documented)** for mods. The register and the reference agree on the chain.

## 17. Extractor output

**Reference (03 section 6.1; anchor `0x437840`).** The extraction updater scans
the unit's occupied **footprint cells** and adds each available cell's metal
value **plus one**, stores the sum in a fixed-point accumulator, multiplies it by
the unit's extraction rating and caches the result on the unit. The script
`SetSpeed` call receives the accumulated whole value. The plus-one means a
zero metal byte is not necessarily a zero contribution. The economy pass later
credits the cached amount when the active and energy-request conditions pass.
Recomputation and crediting are separate operations.

**RWE.** The footprint scan and the cache-and-credit split: footprint cells
summed over `computeFootprintRegion` and credited at the settle
(`GameSimulation.cpp:4400-4405`; the `SetSpeed` notification at creation,
`UnitBehaviorService.cpp:175-180`). **MATCHES** in shape. The **plus one per
cell is MISSING**: RWE multiplies the plain sum by the rating. `TOTALA-EXE-ECONOMY.md`
section 23 says "times the metal under the footprint" and does not say whether
a zero byte contributes, so this is **not a conflict with a register finding but
a point the register never settled**. Two consequences if the reference is right:
an extractor on ground with no metal (a map's metal byte 0 over its footprint)
would yield rating times the number of footprint cells (a stock rating of 0.001
over four cells is 0.004 a second, immaterial to economy), and the value handed
to `SetSpeed`, which the stock extractor scripts use to set the spin rate, would
read higher by the number of cells.

**Remaining uncertainty.** The reference says "plus one" and "fixed-point
accumulator"; whether the plus one is applied to cells beyond the map edge, or
before or after the surface-metal default the map supplies (RWE initialises the
metal grid to the OTA `SurfaceMetal`, default 3, `GameSimulation.cpp:237`),
cannot be told from the chapter. A static read of the extractor routine, or the
`SetSpeed` argument of one extractor on a zero-metal cell in the original, settles
it.

## 18. Wind generators

**Reference (03 section 6.2).** The generator contributes the normalised wind
fraction times its rating, before any applicable income scaling; its script
gets direction and speed on new-wind notifications.

**RWE. MATCHES.** `GameSimulation.cpp:4415-4418` (fraction times rating, the
difficulty scale applied later at the settle), with the 5000 divisor and the
cap at one at `:4312`. `TOTALA-EXE-ECONOMY.md` section 23 has the same.

## 19. Tidal generators

**Reference (03 section 6.3).** Map loading sets tidal strength from a
**non-negative mission setting, or falls back to 0.5**. The economy's tidal
branch multiplies that strength by the unit's tidal rating. The base product is
resolved; a stock generator's per-second output needs its FBI.

**RWE. DIFFERS (fallback).** `tidalStrength` is an integer read from the OTA
key with a default of zero (`src/rwe/io/ota/ota.cpp:44`, loader
`GameSimulationLoader.cpp:694` clamps negatives to zero, `GameSimulation.h:609`),
and the generator uses it as is (`GameSimulation.cpp:4420-4423`). A map or
mission with no tidal setting therefore gives a tidal generator **zero** where
the reference gives half its rating. `TOTALA-EXE-ECONOMY.md` section 23 records
the multiplication by the map's `tidalstrength` and the port that added it, and
says nothing of a fallback. **UNCLEAR whether it is reachable**: shipped maps
overwhelmingly set the key, and the reference does not say whether its "mission
setting" is the OTA key or a separate mission-level one. Also **UNCLEAR**: the
integer type (RWE) against a float (reference); a fractional OTA value would
truncate in RWE.

**Remaining uncertainty.** A one-off read of the shipped OTAs for `tidalstrength`
(the `hpi.py` tool), and a check of what the original does on a map with none.

## 20. Difficulty income scaling

**Reference (03 section 7).** For an active owner of controller type 2, difficulty
0 credits **50%** of the supplied income, difficulty 1 **70%**, the default branch
the full amount. Other owner types use the ordinary amount. The helpers apply to
the producer contributions and selected other credits; the reference explicitly
does not say every operation is multiplied.

**RWE. MATCHES for the original's computer player.** The settle multiplies the
whole production buffer by the player's `resourceBonusFor`
(`GameSimulation.cpp:4479`, `:1268-1276`; `ResourceSettler.cpp:52`), and the
classic engine sets 0.5, 0.7 and 1.0 by difficulty
(`src/rwe/ai/classic/ClassicAiController.cpp:1074-1080`). Because the
multiplier covers the whole buffer, reclaim credits, gifts and cancellation
refunds are scaled as the register says they are
(`TOTALA-EXE-COMPUTER-PLAYER.md` section 123). Note
`TOTALA-EXE-ECONOMY.md` section 23, "Not ported, and why", still reads as though
RWE had only its own bonus table; that predates ADR-0005 and is stale for the
default engine. RWE's own engine uses 1.25 by default (`AiTuningProfile.cpp:359`)
and is a documented option.

**CONFLICT, starting stockpile.** See rule 29.

## 21. Settlement arithmetic

**Reference (03 sections 8.1-8.2, 15).** Aggregate each live unit's two resource
blocks, then the player's extra account; add the optional storage-bonus fields
when the player flag enables them; record income and usage and add them to
lifetime counters. Available funds are newly produced plus the current balance.
Backlog is funded **first**: if funds cover it, the funded ratio is one,
otherwise funds over backlog and all funds are consumed. What remains goes to
current demand the same way; the unspent remainder is the new balance. No
priority among consumers within a class.

**RWE. MATCHES.** `src/rwe/sim/ResourceSettler.cpp:5-43` is that arithmetic;
`GameSimulation.cpp:4470-4553` pools the player's block with every unit's, in the
order the register gives. The register's section 23 states the same two-stage
fractions, and the worked example in the reference (backlog 40, demand 60,
funds 70) gives the same numbers in either. `ResourceSettler.test.cpp` exists.

## 22. Account feedback and backlog

**Reference (03 section 8.3).** Each unit and the extra player account receive
both funded ratios. New backlog equals old backlog times the unfunded backlog
fraction plus current demand times the unfunded demand fraction. Produced and
used copy to the previous-period fields and clear; demand clears.

**RWE. MATCHES.** `UnitState::settleResources` (`UnitState.cpp:709-716`) and the
player block's roll (`ResourceSettler.cpp:80`). A fully paid debt is exactly
zero in both (register section 111 row 8). The settle's fractions are held as
single-precision floats, as the register notes the original stores them.

## 23. Storage and waste

**Reference (03 section 9).** Capacity is recomputed from eligible complete live
units plus enabled bonus fields. After backlog and demand allocation, a balance
above capacity is clamped and the excess increments a wasted counter. Finishing
or destroying storage changes the next recomputed capacity; excess is not
wasted if backlog or demand consumes it first. No universal lower-bound clamp.

**RWE. MATCHES.** `GameSimulation.cpp:4339-4370` rebuilds the capacity each
settle, the clamp and the excess counter are at `ResourceSettler.cpp:68-76`, and
the clamp follows the allocation. The skirmish commander-holds-the-base rule is
RWE's older route to the same number and is documented
(`TOTALA-EXE-ECONOMY.md` "Not ported, and why"; the demo corpus confirms 1000
and 1000). RWE clamps a negative supply to zero (`ResourceSettler.cpp:11-14`),
the register's row 9.

## 24. Construction charging

**Reference (03 section 10).** The common helper treats the remaining build
fraction as one for unbuilt and zero for complete. Positive work reduces the
remaining fraction by work over build time, bounded; the **actual fraction
change** determines proportional energy and metal demand; progress advances only
if the paired request is accepted. Accepted work raises health by the
corresponding integer contribution. Several builders each call it through their
own account.

**RWE. MATCHES in rule, with the register's documented difference in the tick
count.** The per-tick spend is `cost * (workerTime / 30, integer) / buildTime`
(`UnitBehaviorService.cpp:5422-5427`, `:6092-6097`), health is differenced as an
integer (`UnitState.cpp:175-190`), and progress is refused when the request is
(`:5429-5433`). The progress accumulator is integer work units and the original
is a 4-byte float: **DIVERGENCE (documented)** in section 88 (one tick fast on
the 10 of 15 exact-multiple pairs in the corpus), with `tools/tad-buildtime.py`
as the oracle. The nanoframe appearing a tick late is the other documented
offset. Nothing new in the reference.

## 25. Negative work: decay, cancellation, refund

**Reference (03 section 10).** Negative construction work dismantles progress,
adjusts health, credits **metal** through its refund path, and destroys a fully
unbuilt target. Factory cancellation has its own invested-metal refund. Energy is
not refunded by the inspected cancellation branch.

**RWE. MATCHES.** The frame decay (`GameSimulation.cpp:1381-1473`) credits the
untruncated metal fraction and destroys at zero; the cancel path credits the
truncated invested metal (`UnitBehaviorService.cpp:5460-5474`); an externally
killed product flushes the factory's list (`:5331-5342`); energy is never
refunded. This is `TOTALA-EXE-ECONOMY.md` section 134, ported 2026-10-14 and
consistent with the reference. The refund is credited through the production
buffer so the computer-player handicap is applied once at the settle.

## 26. Repair

**Reference (03 section 10).** Ordinary repair uses a separate helper and an
**energy** request rather than the full construction energy-and-metal fraction
formula.

**RWE. MATCHES.** `UnitBehaviorService.cpp:6395-6425` and `updateSelfRepair`
(`GameSimulation.cpp:5010-5067`) both use the single-resource energy request, so
a player who owes metal can still repair. This is `TOTALA-EXE.md` section 94.

## 27. Weapon costs and stockpiles

**Reference (03 section 11).** The common weapon update checks player energy and
metal against the per-shot costs of non-stockpile weapons, invokes the firing
handler, and spends only after a successful fire result; a target reference or an
aim animation alone does not prove a charge. A stockpile weapon instead needs a
positive built-round count and decrements it on a successful fire; building
rounds is a separate weapon-building order. Damage and experience change reload,
not per-shot cost.

**RWE. MATCHES in effect.** A non-stockpile weapon is charged from the stockpile
at the fire step and does not fire if it cannot pay
(`UnitBehaviorService.cpp:1478-1500`); a stockpile weapon needs a stocked round
(`:1478-1484`); the silo build is its own ramp (`:980-1017`). The register
(`TOTALA-EXE-WEAPONS.md` "Firing") has the compare at `0x49E3F3` and the take at
`0x49E51F`, with the shot's event bits raised between them. RWE collapses the
compare and the take into one step. The only observable difference would be a
fire handler that refuses after the compare; none is known in RWE's firing code
after the charge. **UNCLEAR**, low.

## 28. Cloak cost

**Reference (03 section 12).** If the cloak-request flag is on, the unit is not
blocked by the proximity or state flag, and its work-time deadline has elapsed,
it picks the moving or stationary cloak cost from the movement-state bits,
**converts it to an integer**, and tries an immediate energy spend, setting or
clearing the activation state from the result. In every other path the activation
state is cleared. Requested, permitted and affordable are three facts; the
proximity detector is a sensor matter.

**RWE. MATCHES, with one difference.** `GameSimulation.cpp:4431-4458`: the same
three gates, the integer truncation of the selected cost, the all-or-nothing
spend through `chargeStockpile`, and clearing otherwise. The proximity hold is a
90-tick timestamp (`GameSimulation_visibility.cpp:719-761`). Difference:
moving versus stationary is decided from a 0.1-unit position change
(`:4437`), where the original reads the movement-rate band bits, the comment
there treats the two as equivalent. **UNCLEAR**, no known case. RWE also
requires the unit not to be under construction (`:4431`); the reference does not
list that test, and a cloak request on a nanoframe is not reachable from the
interface.

## 29. Starting balances

**Reference (03 section 13).** Starting balances are set only when the
pending-save condition allows. Campaign uses the mission's start arrays and a
starting-storage-bonus helper; skirmish the configured slot values; multiplayer
the host's value **multiplied by 100**, with a host-slot fallback. They are
initialisation, not production, and **should not be scaled by the income
handicap** merely because the player is computer-controlled.

**RWE.** Skirmish and multiplayer take `params->metal` and `params->energy`
as given; a mission takes the schema's human or computer start values
(`GameSimulationLoader.cpp:804-825`), and no handicap is applied to any of them.
**MATCHES the reference.** The "host value times 100" is a lobby encoding: RWE's
lobby carries the final number (`MainMenuScene_multiplayer.cpp:173`), and the
corpus's 1000 and 1000 openings agree. **UNCLEAR** as to the lobby's allowed
range.

**CONFLICT.** `TOTALA-EXE-ECONOMY.md` section 23 says the same handicap constants
"appear at each production site **including the starting stockpile at
`0x465451`**". The reference says a computer-controlled player's starting
balance is not income-scaled. If the register is right, a computer player on
Easy or Medium starts with half or seven tenths of the stockpile and RWE gives it
all of it; if the reference is right, RWE is right. Both stated; neither chosen.
The classic engine (the default) is the seat that would feel it.

## 30. Sharing

**Reference (03 section 13).** Sharing commands configure local thresholds and
flags under multiplayer conditions; metal and energy thresholds are capped at
current capacity in the handlers; resource and map or radar sharing are separate
flags. The transfer allocator is not documented. The main tick invokes sharing
for the local player after the world phases, in the qualifying network path.

**RWE.** `src/rwe/sim/ResourceSharing.{h,cpp}` implements the register's
section 124 (every 60 ticks, one third of the surplus for metal and one half for
energy, last-eligible-ally recipient). The reference documents none of that, so
**the allocator is not cross-checkable here**. **DIFFERS in position**: RWE runs
the automatic pass **before** the settle on the same tick (`GameSimulation.cpp:5124-5126`,
by design, "so what it moves is paid out in the same second"), the reference
runs it after every world phase, including the player phase. In the original a
gift made on a multiple-of-60 tick is therefore paid out by the **next**
settle, a second later. Whether the original also caps thresholds at current
capacity is **UNCLEAR** for RWE: not looked for. Sharing is multiplayer-only and
the gift does not change a build's total, only when it is funded.

## 31. Persistence, reports, HUD

**Reference (03 section 14).** Per-unit saves store both resource blocks, accumulated
and carried state, under a unit-ID-based box; restoring only balances would change
future request eligibility. Multiplayer reports on a separate cadence; the HUD
smooths displayed amounts and samples rates on its own deadline.

**RWE. MATCHES.** Per-unit debt is saved and hashed through
`UnitStateFieldTable.cpp:419-420`; the player block's debt, request buffers and
stalled flags are in `save_util.cpp:302-305` and `GameHash_util.cpp:67-82`.
Report cadence: rule 11. HUD smoothing is presentation and was not audited.

---

## 32. Archive precedence

**Reference (01 section 4.1).** When the gated mounting sequence runs it removes
missing archives and, in the executable's directory, enumerates in this order:
`rev31.GP3` with the high-priority registration argument; current-directory
**CCX** files with the same argument; current-directory **UFO** files with the
other argument; current-directory **HPI** files with the other argument, stopping
after **ten** successful registrations; then HPI files at the root of CD-ROM
drives; then it resolves shadowed files. The chapter says this establishes
enumeration order and call arguments but **not** a complete conflict-resolution
rule; it needs the registration and shadow implementation.

**RWE.** `src/rwe/vfs/CompositeVirtualFileSystem.cpp` (`addToVfs`): the data
folder itself first (loose files), then archives in the strict extension order
`.gp3`, `.gpf`, `.ccx`, `.ufo`, `.hpi`, first match wins, files within one
extension in the operating system's directory-iteration order. The launcher
mirrors this (`launcher/src/common/archives.ts`).

- Loose files before archives: **MATCHES** (reference 4.2).
- `rev31.GP3` before the HPIs: **MATCHES** (`TA-PATCHES.md`, `TOTALA-EXE.md`
  section 88 transport entry).
- **DIFFERS / UNCLEAR, tie classes.** The reference gives `rev31.GP3` and CCX the
  same registration argument, and UFO and HPI the other. RWE strictly ranks
  GP3 over CCX, and UFO over HPI. Whether the shared argument means equal
  priority (then enumeration order decides) is not in the chapter.
- **DIFFERS, extras.** RWE mounts `.gpf` and **any** `.gp3`, where the reference
  names only `rev31.GP3`; the original mounts no `.gpf`. A mod-folder `.gp3` with
  another name is read by RWE and ignored by TA.
- **DIFFERS, the ten-archive cap and directory order.** RWE mounts every HPI;
  the original stops registering current-directory HPIs after ten. Within an
  extension, RWE's order is the operating system's, which is stable on Windows
  and unspecified on Linux.

**Remaining uncertainty.** Real overlap. A one-time comparison of which
entries appear in more than one of `rev31.gp3`, `ccdata.ccx`, `btdata.ccx`,
`totala*.hpi`, with the `hpi.py` tool, says whether the CCX/GP3 tie ever picks
a different file. Do this before changing any order. A data folder with more than
ten HPIs is outside shipped content.

## 33. Loose files and language directories

**Reference (01 sections 4.2, 5).** A loose file wins; an archive entry may be
skipped if shadowed. Data paths try a directory with a **language suffix** (the
requested directory, a hyphen, the preferred language) first and fall back to the
ordinary path, so a copied common file may not be the one actually selected.

**RWE.** Loose first: **MATCHES**. Language-suffixed directories: **MISSING**,
no counterpart found in `src/rwe/vfs` (the only language handling is the
campaign reader and `GameLaunch.cpp`). On an English install the suffix
directory is normally absent, so the shipped data resolves the same; it matters
for a non-English retail install and for fixtures. Low.

## 34. TDF parsing rules that affect values

**Reference (01 section 6).** Fields need an equals sign and a closing
semicolon; whitespace is trimmed; storage and lookup are case-insensitive; a
repeated field **updates** the earlier entry. Missing keys give the loader's
default. A present but malformed number goes through C numeric conversion, so it
is **not** equivalent to a missing key: it converts to what the C routines give
(a leading-digits prefix, or zero). Loaders can then multiply, truncate, narrow,
clamp or replace the retrieved value.

**RWE.** Case-insensitive keys and blocks: **MATCHES**
(`src/rwe/io/tdf/TdfBlock.h:45-66`). A repeated key keeps the **later** value:
**MATCHES** (`insertOrAssignProperty`, `TdfBlock.cpp`). A repeated **block** of
the same name replaces the earlier one wholesale (`createBlock`,
`TdfBlock.cpp`); the reference does not say what the original does and the
chapter leaves duplicate-case behaviour as an open point (**UNCLEAR**).
Numbers: a stream extraction (`TdfBlock.h:14-27`) converts a leading numeric
prefix as the C routines do (`"5abc"` gives 5), but a value with **no** numeric
prefix fails and the loader's default is used where the original would give 0
(**DIFFERS**). Only malformed values are affected, so this is mod-data fidelity;
a required key that fails throws and rejects that content, which is the
security audit's rule. Low.

## 35. Unit limit

**Reference (01 section 3; 03 not).** The `UnitLimit` preference defaults to 250
and is clamped to 20 through 500, but the effective match limit can come from
elsewhere, including host information; the clamp is not a complete description.
`TOTALA-EXE-DATA.md` (OTA `maxunits`, default 200) records the mission cap, and
the naval audit's rule 17 records the 300-tick wait after a failed creation and
its message "Unable to create any more units".

**RWE. MISSING.** There is no per-player or per-game cap in the simulation.
`GameScene_render.cpp:609` and `TOTALA-EXE-INTERFACE.md` (about line 2320) say so
and leave the "(Max N)" off the unit counter; the map's `maxunits` is read
(`ota.cpp:60`) and passed only to the classic AI's ratings
(`GameSimulationLoader.cpp:1010`). In a big game, the original stops production
at the cap and RWE does not, so a long arena or a late game with a human
building freely goes past the number TA would have allowed. This is the
player-visible gap in this audit that the register does not already list.

**Remaining uncertainty.** Per-player or whole-game, which sources feed the limit
in a skirmish, and what an order does when the cap is hit (only the 300-tick
creation wait is described, for a factory). Needs an original-game check; a hashed
behaviour change.

---

## Not audited here

Items that overlap findings already settled or that belong to later reference
chapters: the order and action detail inside a unit update beyond rule 8, motion
subphases (`TOTALA-EXE-MOVEMENT.md`), the HUD rate presentation and smoothing,
profiling buckets, the application loop and focus, state-handler mapping and the
loading sequence (01 sections 8-12), TNT versions and header selection (01 9.4),
the script-checksum contents (01 10), COB `SetSpeed` consumer semantics, and
allied transfer allocation (rule 30). Ch.01's archive-registration source
(`CD-ROM` roots, `TOTALA.ID`) has no RWE analogue.

## 36. Gaps to fix

Ranked by player-visible impact times confidence. "Original check" means a play,
capture or oracle run on retail Total Annihilation before the code is changed.
Demo oracles: `tools/tad-buildtime.py`, `tools/tad-stalltime.py`,
`tools/tad-weapontime.py` and `tools/tad-storagecapacity.py`, with the checked-in
fixtures `src/rwe/sim/tad_build_episodes.h`, `tad_stall_episodes.h`,
`tad_weapon_episodes.h` and `tad_economy_episodes.h`; the economy fixture
scores **storage** only, and the stall scorer scores what a stalled settle
costs a factory. Neither can see extractor, wind or tidal output, because the
stream does not carry the map or the wind (`TA-DEMOS.md`, "the stall half of the
economy oracle").

| Rank | Gap | RWE files a fix would touch | Original check first? |
| ---: | --- | --- | --- |
| 1 | Unit cap: a per-player or per-game limit from the map's `maxunits` (and the lobby), refused creation, the 300-tick factory wait, and "(Max N)" on the HUD. Rule 35 | `src/rwe/sim/GameSimulation.cpp` (`trySpawnUnit`, `spawnNewUnits`), `GameSimulation.h`, `GameSimulationLoader.cpp:1010`, `save_util.cpp`, `GameHash_util.cpp` and `dump_util.cpp` if new state, `GameScene_render.cpp:609`, the AI controllers that read `unitLimit` | **Yes.** Semantics (which source, per player or global, what an order does) are not in the reference. Hashed behaviour. |
| 2 | Wind selection interval and draw rule: confirm 5 to 14 seconds, and the zero-speed direction. Rule 10 | `src/rwe/sim/GameSimulation.cpp:4298-4321` | **Spec-room static read** of the wind selector first (reference gives no constant). No demo oracle (wind is not in the stream). |
| 3 | Tidal fallback of 0.5 when the map or mission sets none, and float rather than integer strength. Rule 19 | `src/rwe/io/ota/ota.cpp:44`, `GameSimulationLoader.cpp:694`, `GameSimulation.h:609`, `GameSimulation.cpp:4420-4423`, `save_util.cpp:684,823` | Light: read the shipped OTAs for the key (`hpi.py`); then one retail look at a map without it. |
| 4 | Wall-clock tick length (33 ms integer against 33.33 ms) and the game clock running about one percent fast. Rule 1 | `src/rwe/game/GameScene.cpp` accumulator and `GameSpeed` use (scene-side only; **not** `SimMillisecondsPerTick`, which the COB clock and the piece step share) | **Yes**, a stopwatch against the in-game "Game Time" in retail. Not a sim change if kept scene-side. |
| 5 | Extractor "plus one per footprint cell" and the `SetSpeed` argument. Rule 17 | `src/rwe/sim/GameSimulation.cpp:4400-4405`, `UnitBehaviorService.cpp:175-180` | **Yes**, an extractor on a zero-metal cell in retail, or the spec-room read of the extractor routine. Cannot be scored from demos. |
| 6 | Starting stockpile handicap for Easy and Medium computer players (CONFLICT, rule 29). | `src/rwe/game/GameSimulationLoader.cpp:804-825`, `GameSimulation::addPlayer` | **Yes**: the register against the reference; open the starting-balance site in the executable (`0x465451`) in the spec room, then confirm with a retail Easy game's first resource sample. The demo corpus is human-only for the openings. |
| 7 | Archive tie classes (GP3 with CCX, UFO with HPI), the extra `.gpf` and non-`rev31` `.gp3`, and the ten-HPI cap. Rule 32 | `src/rwe/vfs/CompositeVirtualFileSystem.cpp`, `launcher/src/common/archives.ts` (must change together) | Light: list the overlap across `rev31.gp3`, the `.ccx` files and the `.hpi` files; change nothing if there is none. |
| 8 | Automatic sharing runs after the settle in the original and before it in RWE. Rule 30 | `src/rwe/sim/GameSimulation.cpp:5124-5128`, `ResourceSharing.cpp`/`.h` | Light: the reference has no allocator detail to compare; confirm the position in the register's section 124 reading. Multiplayer only. |
| 9 | Phase order: players before features, and a debris phase (CONFLICT, rule 6); wind after players. | `src/rwe/sim/GameSimulation.cpp:5101-5257` | **Yes**, the spec-room look at the step routine; likely one-tick effects only. A tick-order change moves every replay (version gate). |
| 10 | Economy gate for eliminated players, `UnitLimit`-style qualifiers. Rule 11 | `src/rwe/sim/GameSimulation.cpp:4335-4340,4470-4475` | No effect shown; look only if a defeated player's income is ever visible. |
| 11 | Duplicate TDF blocks (RWE replaces the earlier block), and a non-numeric value falling back to the default instead of zero. Rule 34 | `src/rwe/io/tdf/TdfBlock.cpp`, `TdfBlock.h` | Spec-room read of the TDF parser; low, mod data only. |
| 12 | Language-suffixed data directories. Rule 33 | `src/rwe/vfs/` | No; English installs resolve the same. |
| 13 | Missing "or moving" energy charge and the exclusive producer chain. Rules 15, 16 | `src/rwe/sim/GameSimulation.cpp:4389-4423` | **DIVERGENCE (documented)**, decided not chased (section 111 row 5). Listed for completeness. |
| 14 | Final-tick usage figure in the construction request. Rule 13 | `src/rwe/sim/UnitBehaviorService.cpp:5422-5427,6092-6097` | No; display only. |

### Documented divergences this audit leaves alone

Per-unit order (section 132) and the idle poll cadences (section 138), deferred
on purpose. Economy settle ordering inside a tick (section 111 row 2). The
integer build accumulator (section 88). The live detection snapshot (section 88).
The five-step cap and speed hysteresis, and the remote-lag rate policy, as
consequences of lockstep (rules 3, 4: not listed in section 88, so a short entry
there would close it).

### Conflicts with existing RWE findings, in one place

1. **Per-player pass against the feature pass, and the debris phase (rule 6).**
   Reference: units, projectiles, explosions, **players, features**, global
   animations, wind, meteors, camera, particles, blink, sharing.
   `TOTALA-EXE-MOVEMENT.md` section 132 and `TOTALA-EXE-ECONOMY.md` section 111:
   units, projectiles, **features, players**. Open.
2. **Starting stockpile of a computer player (rule 29).** Reference: balances are
   initialisation and are not income-scaled. `TOTALA-EXE-ECONOMY.md` section 23:
   the handicap constants appear at the starting stockpile (`0x465451`) too. Open.
3. **Extractor sum (rule 17).** Reference: each footprint cell contributes its
   metal byte plus one. `TOTALA-EXE-ECONOMY.md` section 23: "times the metal under
   the footprint", with no statement about the zero byte. Not a contradiction, but
   the register never settled the point and RWE has no plus one.
4. **Tidal fallback (rule 19).** Reference: a non-negative setting or 0.5. RWE
   and the register's section 23 record only the multiplication by the map's
   `tidalstrength`. Open.
5. **Where weapons spend (rule 27).** Not a conflict with the register's address
   table (compare at `0x49E3F3`, take at `0x49E51F`), but RWE's code comment
   (`UnitBehaviorService.cpp:1457-1463`) says the price is settled "before
   anything else"; the reference says it is spent after a successful fire
   result. They agree on the observable behaviour unless the fire step can
   refuse after the compare.
6. **A stale statement in the register.** `TOTALA-EXE-ECONOMY.md` section 23 "Not
   ported" still describes the computer-player handicap as not ported; the
   classic engine now applies 0.5 and 0.7 through the settle (rule 20). The
   reference corroborates the 50% and 70% figures and the owner-type test.
7. **Reference limits.** The reference itself lists as unresolved the allied
   transfer allocator, all economy-producing call sites, the initial-storage
   bonus semantics, every repair, reclaim, capture and resurrection formula, the
   full HUD rate presentation, the complete timer implementation and every
   packet or authority branch; nothing in it is used here to settle those.
