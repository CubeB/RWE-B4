# What the original executable does: movement, pathfinding and what blocks a unit

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §1, §13, §16, §86, §87, §95, §102.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

1. [Aircraft flight model](#1-aircraft-flight-model)
13. [What an aircraft does with nothing to do](#13-what-an-aircraft-does-with-nothing-to-do)
16. [Wakes, thrust, and the small unit flags](#16-wakes-thrust-and-the-small-unit-flags)
86. [Patrol: what makes a unit leave its route](#86-patrol-what-makes-a-unit-leave-its-route)
87. [Pathfinding: one scheduler, a bug-walk, and a unit that never waits](#87-pathfinding-one-scheduler-a-bug-walk-and-a-unit-that-never-waits)
95. [What blocks a unit: the map square, the passability class, and why a hovercraft cannot cross a wreck](#95-what-blocks-a-unit-the-map-square-the-passability-class-and-why-a-hovercraft-cannot-cross-a-wreck)
102. [The ground path follower: an aim point on the segment, and two brakes into the corner](#102-the-ground-path-follower-an-aim-point-on-the-segment-and-two-brakes-into-the-corner)

## 1. Aircraft flight model

The most thoroughly verified area, because RWE's aircraft were visibly wrong in
three separate ways and each turned out to have a different cause.

### The per-tick air movement, `0x43D290`

Seven steps, in this order. The order matters: bank is fed the *total* change in
velocity for the tick, after everything else has had its say.

| Step | Address range | What it does |
|---|---|---|
| Drag | `0x43D2FB`–`0x43D38B` | `vel *= 1 − Acceleration/MaxVelocity` |
| Brake | `0x43D38E`–`0x43D47D` | if horizontal speed > `BrakeRate`, scale it back to `BrakeRate` and re-inject the excess along the nose |
| Height | `0x43D4D0`–`0x43D502` | `vel.y = clamp(goalY − y, −r, r)`, set outright, where `r` is a quarter of the speed stored at the end of the last tick, or 1 below a speed of 4 |
| Turn | `0x43D520`–`0x43D585` | heading steps toward the desired heading, limited by `TurnRate` |
| Profile | `0x43D58B`–`0x43D5EA` | `desiredVel = toTarget × sqrt(2 × Acceleration / max(distance, 8))`, in x and z only |
| Clamp | `0x43D60A` | the horizontal change in velocity is limited to one `Acceleration` |
| Bank | `0x43D68B`–`0x43D6B3` | roll from the tick's total velocity change |

**Height is not steered.** Found 2026-09-11, and missed by the six-step
reading above until then. The profile and the clamp are flat: the distance
is the hypot of x and z (`0x43D51B`), the clamp tests the hypot of the two
horizontal deltas (`0x43D605`), and the result is added to `vel.x` and
`vel.z` alone (`0x43D653`, `0x43D65A`). Height has its own step, which
does not accelerate at all. It takes the goal altitude less the unit's
height and writes it straight into `vel.y`, clamped to ±`r`. Here `r` is
`[mover+0x20] >> 2`, a quarter of the `|vel|` that `0x43D688` stores at
the end of each tick, and a flat 1.0 when that speed is under 4
(`0x43D4D8`). The step is skipped only for a unit in the off-map bucket
(`unit+0x82 == [gs+0x142B7]`, `0x43D4C8`). An aircraft therefore closes
on its altitude as fast as that limit allows and stops on it, and it
cannot overshoot. RWE had height inside the profile, sharing the one
`Acceleration` with the turn. An Atlas (0.04) that took its cargo while
still coming down at a unit a tick, then turned for home, sank fifty
units under the ground and the sea before it climbed back.

**There is no throttle/brake mode switch.** This is the single most important
finding in this section. RWE had a two-branch law — full acceleration toward
`MaxVelocity` while outside the braking distance, full deceleration inside it —
which meant the step was pinned at exactly one `Acceleration` on every tick of
every trip, and the two branches chattered against each other whenever an
aircraft sat near its destination. The original just steers continuously toward
a speed it can still shed, and the step goes slack by itself on the profile.

**The `8` is a floor on the range, not a cap.** `fcom` against `8.0f` (stored at
`0x4FD2E0`) at `0x43D58B`, then `test ah,1; je`, keeps the real distance when it
is ≥ 8 and substitutes 8 below it. So speed rises as `sqrt(2·a·d)` with distance,
and the last eight units become a straight linear run-down to a stop rather than
a curve demanding ever harder braking. Reading it as a cap instead gives every
aircraft a top speed of about one world unit per tick, which is obviously wrong
the moment you try it.

**`MaxVelocity` is not enforced anywhere.** The drag term *is* the speed limit:
equilibrium sits where the acceleration added each tick balances the speed drag
removes, which works out to exactly `MaxVelocity`. No clamp is needed and none
exists.

### Bank, `0x43D0D0`

Honest coordinated-turn physics, not an animation:

```
43d0d9   accum.{x,y,z} = accum * 0xF333 >> 16     ; one-pole lag, 0.9499817
43d13e   accum += deltaVelocity                    ; the tick's total change
43d16a   mov cx, WORD PTR [esi+0x66]               ; the unit's HEADING
43d173   call 0x4B7173                             ; Rotate2D(heading, {accum.x, accum.z})
43d1a0   mov ecx, [esp+0x8]                        ; tmp[0], the lateral component
43d1ad   neg ecx
43d1af   mov eax, [edx+0x1A2]                      ; BankScale
43d1c2   call 0x4B715A                             ; atan2(lateral × BankScale, G)
43d1ca   mov WORD PTR [esi+0x64], ax               ; store roll
```

`0x4B7173` computes `v0' = v0·cos − v1·sin`, an ordinary 2D rotation.

Two things fall out of the constants. `G` at `0x43D186` is **gravity × 20**, and
the one-pole lag's steady-state gain is `1/(1 − 0.95) = 20` — so the two twenties
cancel and the whole thing reduces to `roll = atan(lateral_acceleration ×
BankScale / gravity)`. The lag is therefore purely a smoother: it gives the ramp
on entering a turn and the wash-out on leaving it, with no rate limit or clamp
anywhere. Gravity is 112 world units per second squared on nearly every shipped
map, i.e. `112/900` per tick squared.

**The lateral component is taken against the nose, not the direction of travel.**
`[esi+0x66]` is unambiguous. This matters because it looks wrong for a
construction aircraft, which points at its job while dashing sideways to the next
station — all of its forward acceleration lands in the lateral term. That is
genuinely what the original does; the reason it does not shake is that its
accelerations are small, not that it uses a different frame. Resolving against
the velocity direction instead was tried and measured *worse* (25.4° peak, 18
sign reversals per hop), because the velocity change flips relative to the flight
path too and the heading lags behind it.

### The construction aircraft build pattern

The aircraft holds station one `BuildDistance` from the job, on a ring, stepping
a seventh of a turn around it and holding each station, with the nose locked on
the job rather than on the flight path.

### Deliberately not ported

- ~~**The `BrakeRate` nose re-aim** (`0x43D38E`–`0x43D47D`)~~: ported 2026-09-11
  (`applyBrakeRateNoseReaim`, in the flying state's velocity update, after drag
  and before the profile as in the original). It is why the original's aircraft
  barely crab: speed above `BrakeRate` is stripped and re-injected along the
  nose, so they fly roughly where they point. It had been left out because it
  moved the construction-aircraft bank peak by 0.00° (a construction aircraft
  never reaches its `BrakeRate` of 1.5) and nothing faster had been measured.
  Measured now with the shipped fighters' numbers: the worst crab angle through
  a ninety-degree turn drops to a few degrees (`brakerate.test.cpp`). Applied
  in every air state since B4 #41: `Mover::Update` (`0x43DD20`, §87) hands any
  `canfly` unit to `0x43D290` whatever its mission, so the gunship ring and the
  dogfight, which already flew through the flying state's function, had it
  from the first port, and the attack run, which steers its own heading, now
  runs the brake step before that swing.
- **Pitch.** The original also pitches aircraft via `PitchScale`
  (`def+0x1A6`) into `unit+0x68`. RWE has no pitch for units at all and the
  renderer applies only yaw and roll.

  **This bullet said "from the longitudinal component of the same
  accumulator" until 2026-09-24, and that was wrong: pitch is taken from the
  SAME component as bank.** The two are computed back to back and the
  disassembly settles it by counting the stack. `0x43D1A0` reads the
  component from `[esp+0x8]`, negates it, multiplies by `BankScale`
  (`def+0x1A2`), shifts down 16 and hands it to `atan2` against the gravity
  term held in `edi`, storing the result in `unit+0x64`. `0x43D1CE` then
  pushes `edi` again -- one push, so `esp` drops by four -- and `0x43D1D5`
  reads `[esp+0xc]`. The displacement grew by exactly the amount `esp` fell,
  so **both reads name the same slot**; only the scale field and the
  destination differ. Whatever longitudinal component the rotation produces is
  never consumed. Found by comparing this section against an independent
  reading of the same binary, which had it right; see
  [`TOTALA-EXE-EXTERNAL.md`](TOTALA-EXE-EXTERNAL.md). It is inert in shipped
  content, where every `PitchScale` is 0, and it matters the moment pitch is
  ported -- doing it "the longitudinal way" would look wrong in play.

---

## 13. What an aircraft does with nothing to do

Two things the player reported, which turn out to be the same question asked
twice: a bomber "immediately stops in place and lands" once its target is
destroyed, where the original "flies around after and sometimes goes circling
back over the now destroyed target"; and construction aircraft guarding an idle
factory, which in the original "fly around in the nearby vicinity".

The original has three different answers depending on *why* the aircraft has
nothing to do, and the difference is the whole finding. An attack that runs out
of target is replaced by a search mission that circles the spot for ever. A
mission list that merely empties gets `DefaultMissionType`, which for an
aircraft means going and landing. A guard order flies a circuit of its own for
as long as the order lasts. RWE had one answer — go and land — for all three.

Companion to the missions document, whose conventions this section uses
throughout: `Dir(θ)` is the direction an object with heading θ faces, and
`0x48A980(a, b)` is the bearing such that `Dir` of it points from `a` to `b`.
Arrival tolerances handed to `0x44E730` are whole world units; distances
written as `0x1400000` and the like are 16.16.

### 1. An attack that ends because the target died: `VTOL_SeekAttack`

`AirStrike` does not simply finish. Its prologue, at `0x411FEA`, reads the
mission's target unit; when that is null and `mission+0x42` has bit 9 set — the
flag that says the mission was created against a unit rather than a patch of
ground — it allocates a fresh mission, resolves the name `"VTOL_SEEKATTACK"`
(`0x501C9C`) through `MissionId::FromName`, constructs it with **no target unit
and the attacker's own position** (`lea edx,[edi+0x6a]` at `0x412020`, where
`edi` is the unit), appends it with `0x43AD10`, and returns 5 so that the strike
mission itself is deleted. `AirToGround`, `AirToGroundHover` and `AirToAir` do
the same on their `flags & 0x1000A` / `0x10008` paths, with the mission's last
known target position instead.

So the aircraft's next mission is anchored on **where it was standing the moment
its target went away** — which, at the end of a bombing run, is over or just
past the wreckage.

**`VTOL_SeekAttack` is `0x4103E0`**, arguments `(unit, mission, flags)`.

Prologue:

* `if (flags & 0x40) return 5` — `0x4103EA`. One of the move-goal outcome bits
  ends the mission outright.
* Off-map (`unit+0x82 == [gs+0x142B7]`, the sentinel bucket of §11 of the
  missions document): waypoint = `unitPos + Dir(0x48A980(unitPos, mapCentre)) ·
  800`, arrival tolerance `0x80`, `mission+0x06 |= 0xE0`, return 2.
* Dispatch on `mission+0x05`: 0 → `0x410701`, 1 → `0x41050F`, anything else →
  return 7 (flush the list).

**State 0 (`0x410701`) — take off, and take a bearing.**

* `unit->mover == 0` or `canfly` clear → return 7.
* If `mission+0x16` is a live target after all, hand it to `0x43B1F0` and
  return 0 (restart) if that issues an attack.
* Otherwise: if `mission+0x22`, `+0x26` and `+0x2A` are all zero, fill them
  with the unit's own position (`0x41074A`–`0x410771`); then
  **`mission+0x36 = rand(0x10000)`** (`0x410774`) — a uniform full-circle
  bearing, the state the circuit runs on — and `mission+0x3A = mission+0x36 & 1`.
* `0x4898B0(unit, 3)` (the mission owns all three weapons), detach from a pad
  if `unit+0x86`, `0x48B090(unit, 1, 1)`, and if the mover is in landed mode
  switch to flying and climb to `cruisealt / 2` on the spot. Return 1.

**State 1 (`0x41050F`) — the circuit. This state never advances.**

```
41050f  0x489800(unit, 3)                       ; weapons free again
410518  if (word[unit+0x108] < (dword[def+0x1FA] >> 2) * 3)   ; below 75% health
            pads = 0x40B530(player, &unitPos, 0xF00, &list)   ; within 3840
            if (pads not empty):
                0x4388D0(mission, 0)            ; drop the move goal
                push VTOL_LANDING (0x501B94) on rand(n) of them, 0x43ACB0
                mission+0x06 = 0 ; return 0
4105fa  t = 0x43B700(unit)                      ; the sight-range search
        if (t && 0x43B1F0(unit, t, 0)) return 5 ; found a fight: this mission is done
410620  if (flags & 0xE0)                       ; the last hop's goal completed
            mission+0x36 -= 0x5555 + rand(0x2000)
41063e  r = ([[unit+0x10] + 0xDC] + 0xA0) << 16 ; weapon 0's range, plus 160
410654  waypoint = mission+0x22 + Dir(word[mission+0x36]) * r
4106c4  0x44E730(goal, 0x80)                    ; arrival tolerance 128
4106d0  0x4388D0(mission, goal)
4106d8  0x439E80(mission, rand(30) + 30)
4106ea  mission+0x06 |= 0xE0
4106f2  return 2
```

Three things fall out of that.

* **The bearing steps back by `0x5555 + rand(0x2000)`, i.e. 120° to 165°, and
  only when the previous goal actually completed.** The timer wake re-installs
  the *same* goal at the *same* bearing, so it changes no geometry; its job is
  to re-run the health check and the sight search a couple of times a second.
* **The radius is weapon range plus 160**, which for all four bombers — whose
  bombs all reach 1280 — is **1440**.
* **A step of more than a quarter turn and less than half of one means every
  leg is a chord, not an arc.** A 120° chord of a 1440 circle passes 720 from
  the centre, a 165° chord passes 188 from it. That is exactly the reported
  "sometimes goes circling back over the now destroyed target", and it is why
  the step is worth transcribing rather than rounding to a neat orbit.

Nothing else ends the mission. There is no maneuver leash on it — `0x43A0C0`'s
sixth argument is pushed as 0 at `0x41201C`, so `mission+0x3E` is zero and the
handler never tests it anyway. An undamaged bomber whose target dies **circles
that spot indefinitely**.

### 2. A mission list that merely empties: `VTOL_Standby`

The other case, and the one RWE already had right. §9 records that
`DefaultMissionType` is installed at `0x43B9AD` on the branch of the service
loop taken when a unit's mission list becomes empty. All twenty-one aircraft in
the shipped data name `VTOL_Standby`, whose handler is **`0x40F7D0`**.

**State 0 (`0x40F9B8`)** — `0x4898B0(unit, 3)`, `mission+0x06 |= 0x10000`, wake
next tick, and then `mission+0x2E = word[unit+0x6C]`, `mission+0x30 =
word[unit+0x74]`: the anchor is **where the aircraft was standing when it went
idle**. Return 1.

**State 1 (`0x40F988`)** — `0x43B700` and `0x43B1F0`; if that issues an attack,
clear the mask, reset to state 0 and return 3. Otherwise return 1.

**State 2 (`0x40F7F8`)** — the decision:

```
40f808  if (!(def+0x241 & 0x800))        goto sleep   ; canfly
40f811  if ((unit+0x110 & 3) != 2)       goto sleep   ; not airborne
40f823  if (unit+0x8A == 0)              goto land
40f82f  θ = rand(0x10000) ; r = (rand(0x20) + 8) << 16
        waypoint = anchor + Dir(θ) * r                ; anchor = mission+0x2E, +0x30
40f8bd  0x44E6C0(goal, word[def+0x21C])               ; full cruisealt, no tolerance
40f8d4  0x439E80(mission, rand(15) + 30)
40f8e6  mission+0x05 = 1 ; return 2

sleep (0x40F957):  mask |= 0x10000 ; wake rand(30)+30 ; state = 1 ; return 2
land  (0x40F8F9):  push VTOL_LANDIFCAN (0x5012BC) at mission+0x22 ; return 5
```

`unit+0x8A` is the **head of the list of units attached to this one**: it is
written at `0x48AC82` when something is attached, chained through `unit+0x8E`,
and `unit+0x86` is the reverse pointer to the carrier
(`0x48AC15`–`0x48AC82`).

So an idle aircraft **hops about a ring of 8 to 39 units around the spot it
went idle on, at full cruise altitude, once every 30 to 44 ticks — but only
while it is carrying something.** Empty, it pushes `VTOL_LandIfCan` and goes
and lands. RWE's "no orders, go and find somewhere to set down" is the right
answer for this case; the reported bug was that it was also the answer for
case 1.

### 3. Guarding: `VTOL_Follow`

A guard order on an aircraft becomes `VTOL_Follow` directly, not `VTOL_SeekGuard`.
`0x43F4C7` requires `def+0x245` bit 5 (`canguard`) and a target, then tests
`def+0x241` bit 11 (`canfly`) at `0x43F4E2` and picks the name at `0x505368` =
`"VTOL_FOLLOW"`. **The handler is `0x40FBE0`**, arguments `(unit, mission, flags)`.

Prologue: if `mission+0x16` is null, or `flags & 0x48`, push a `VTOL_SeekGuard`
(`0x501B1C`) carrying the same target and position and return 5 (`0x410341`);
handle the off-map case as above; otherwise **refresh `mission+0x22` from the
guarded unit's live position every tick** (`0x40FCE9`).

* **State 0 (`0x41025D`)** — announce `"Guarding"` (`0x5014E8`),
  `0x4898B0(unit, 3)`, detach, `0x48B090(1, 1)`, and if landed switch to flying
  and climb to `cruisealt / 2` in place. Then `mission+0x36 = rand(0x10000)`
  and `mission+0x3A = mission+0x36 & 1`. Return 1.
* **State 1 (`0x410245`)** — `0x489800(unit, 3)`, free all three weapons.
  Return 1.
* **State 2 (`0x40FD26`)** — everything else, in order: retaliate on whoever
  last hit the guarded unit (`guardee+0xF0`, gated on the alliance byte, on
  `flags & 0x10` and on the guard's own `noChaseCategory` at `def+0x23D`);
  point each armed, free weapon slot at it; then two attempts to copy the
  guarded unit's work — `0x4899B0(unit, guardee)` followed by `0x43F0E0` at
  `0x40FE86`, and cloning the guarded unit's own head mission at `0x40FEDC`
  through a chain of `FromName` comparisons. If none of those bite, control
  reaches `0x41013B`, and that is the circuit:

```
41013b  if (flags & 0xE0)
            mission+0x36 -= 0x4000 + rand(0x2000)
41015b  if (unit+0x110 & 0x80000000)
41016c      r = ([[unit+0x10] + 0xDC] + 0xA0) << 16   ; weapon 0's range, plus 160
        else
4101b0      r = 0x1400000                             ; 320
410193  waypoint = guardeePos + Dir(word[mission+0x36]) * r
410211  0x44E730(goal, 0x80)                          ; tolerance 128
410225  0x439E80(mission, 0x1E)                       ; wake in 30 ticks
41022e  mission+0x06 |= 0xF8
410236  return 2
```

Same shape as the search circuit, with a **quarter-turn** base step instead of a
third, no altitude call (it keeps whatever the take-off left it at), and a
radius that depends on whether the guard is armed.

**`unit+0x110` bit 31 is "this unit's definition names a weapon".** It is
copied out of the definition at `0x485AAD`–`0x485AC5`, where
`(def+0x241 & 0xFFFF0000) << 15` keeps bit 16 alone and everything above it
shifts out of the register; and `def+0x241` bit 16 is set at `0x42CF19` unless
all three of `def+0x1EE`, `+0x1F2` and `+0x1F6` — the weapon-1, -2 and -3
definitions — are null. The test has to be there, because the armed branch
dereferences weapon slot 0's definition and an unarmed unit has none.

So **a construction aircraft guarding a factory works a ring of 320 world units
around it, twenty tiles across, moving 90° to 135° round every time it arrives**,
with a 128-unit arrival tolerance so it really does fly to each point. That is
the reported milling about, and it is a good deal larger than it looks in the
disassembly: `0x1400000` is 320, not 20 — `0x140000` is the 20 that `AirToAir`
uses, and that 20 turns out not to be a hop at all but the length of the probe
vectors in a facing test. See §90.

An armed guard — a Brawler told to guard something — works a much wider ring,
`370 + 160 = 530`.

### 4. Constants

| Value | Meaning | Address |
|---|---|---|
| `0x5555 + rand(0x2000)` | **120°–165°**, the search circuit's bearing step, subtracted | `0x410625`–`0x41063B` |
| `0x4000 + rand(0x2000)` | **90°–135°**, the guard circuit's bearing step, subtracted | `0x410142`–`0x410158` |
| `rand(0x10000)` | the opening bearing of either circuit | `0x410774`, `0x410310` |
| `+0xA0` | **160** added to weapon 0's range for the circuit radius | `0x41064B`, `0x410175` |
| `0x1400000` | **320**, the circuit radius for a unit with no weapon | `0x4101B0` |
| `0x80` | **128**, both circuits' arrival tolerance | `0x4106C4`, `0x410211` |
| `rand(30) + 30` | the search circuit's wake timer | `0x4106D8` |
| `0x1E` | **30 ticks**, the guard circuit's wake timer | `0x410225` |
| `rand(0x20) + 8` | **8–39**, `VTOL_Standby`'s hop radius | `0x40F857`–`0x40F861` |
| `rand(15) + 30` | `VTOL_Standby`'s hop timer | `0x40F8D4` |
| `cruisealt` | `VTOL_Standby`'s hop altitude, full rather than half | `0x40F8BD` |
| `(maxdamage >> 2) * 3` | 75 % health, the search circuit's go-home threshold | `0x410518` |
| `0xF00` | **3840**, its repair-pad search radius | `0x410558` |

### 5. What RWE does with this

Implemented, in `UnitBehaviorService`:

- `UnitState::AirLoiterState` — an anchor, a bearing, and which of the two
  circuits it is. `beginAirLoiter` takes the opening bearing from `sim->rng`;
  `flyAirLoiterCircuit` puts the goal on the ring, steps the bearing when the
  aircraft is inside 128 of it, and hands the point to `AirMovementStateFlying`
  directly, the same way the construction aircraft's work pattern does.
- `attackTargetAir` arms the `AttackEnded` circuit on the tick the target goes
  away, anchored on the aircraft's own position, and the idle path flies it
  from the next tick instead of looking for somewhere to land. The circuit
  takes over from an attack run or from a gunship's ring as well as from level
  flight; the aircraft really will be in one of those when its target dies, and
  a handover that only works from level flight leaves it stuck.
- `handleGuardOrder` flies the `Guarding` circuit around a guarded unit that has
  no work to hand, at weapon range plus 160 or 320 for something unarmed, and
  asks a grounded aircraft to take off first. It used to do nothing at all once
  the guard was within 200 units, which is why the planes hung still.
- Any order other than `Guard` clears the circuit, so an aircraft that has been
  given something else to do goes home afterwards as `VTOL_Standby` would; and a
  cancelled guard clears it on the first idle tick for the same reason.

Decoded here and deliberately **not** ported:

- **The sight-range search `0x43B700`**, which is what both `VTOL_SeekAttack`
  state 1 and `VTOL_Standby` state 1 do before they fly anywhere. RWE has no
  equivalent — see §90 — and its own idle weapon acquisition stands in.
- **The go-home-when-hurt branch** is now ported — see §94. This bullet used to
  say RWE had no air repair pads and nothing to fly to; it has both.
- **`VTOL_Standby`'s carrying-something hop** (8–39 units around the idle spot,
  every 30–44 ticks). It only applies to a transport with units aboard, and RWE
  parks a loaded transport rather than fidgeting.
- **`VTOL_Follow`'s work copying** — the `0x43F0E0` order build at `0x40FE86`
  and the mission clone at `0x40FEDC`. RWE's guard already assists a builder and
  a factory through `handleGuardOrder`'s own branches; what the original does
  that RWE does not is copy a *reclaim*, *repair* or *capture* the guarded unit
  is engaged on.
- **The wake timers.** Both circuits re-enter on a timer as well as on arrival,
  but a timer wake re-installs the same goal at the same bearing, so it changes
  nothing geometrically. RWE checks arrival every tick instead.
- **The off-map recovery** (`0x41041D`, 800 units back toward the map centre),
  which shares the unconfirmed reading recorded in the missions document §11.

One correction to `docs/REVERSE-ENGINEERING-PRIORITIES.md`, entry 6: it says
`DefaultMissionType` is "left decoded but unported" because "`Standby` versus
`Guard_NoMove` is only the question of whether an idle unit walks off to find a
fight". That is true of the two ground missions but not of the third value in
the data. `VTOL_Standby` also decides whether an idle aircraft hops about or
goes and lands, on `unit+0x8A`, and that half of it does not need `0x43B700` at
all.

## 16. Wakes, thrust, and the small unit flags

The five `emit-sfx` types RWE had never implemented. Wakes are behind every ship
in the game, so this is the most visible of the remaining gaps.

### The dispatch, `0x480EB0`

`EmitSfx(pieceIndex, sfxType)` takes two arguments. Before it does anything else
it asks whether the local player can see the unit (`0x465AC0`, tested at
`0x480EEA`) and **returns doing nothing if not** — so the whole SFX system is
client-side and no part of it is simulation state. That matches where RWE
already puts it.

It then takes the emitting piece's **first two transformed vertices**, adds each
to the unit's world position with the z negated (`0x480F59`–`0x480FD8`), and
jumps through the table at `0x481128`:

| Id | Type | Handler | Spawner |
|---|---|---|---|
| 0 | `Vtol` | `0x481000` | `0x472330` |
| 1 | `Thrust` | `0x48101F` | `0x472330` |
| 2 | `Wake1` | `0x48103E` | `0x472430` |
| 3 | `Wake2` | `0x48105B` | `0x472430` |
| 4 | `ReverseWake1` | `0x481078` | `0x472430` |
| 5 | `ReverseWake2` | `0x481095` | `0x472430` |

`SFXTYPE_POINTBASED` (256) falls off the end of the dispatch and does nothing at
all.

### All four wakes are one routine with two knobs

The four call sites differ in **exactly two** things: which of the two vertices
is pushed first, and whether the second argument is 16 or 8.

- `Wake1` → `(vertex0, vertex1, 16)`, `Wake2` → `(vertex0, vertex1, 8)`
- `ReverseWake1` → `(vertex1, vertex0, 16)`, `ReverseWake2` → `(vertex1, vertex0, 8)`

So **"reverse" is nothing but swapping the two vertices**, which turns the drift
round. There is no test on speed, on throttle, or on the direction of travel
anywhere in the engine — which of the four a ship uses is entirely its script's
decision, and the script knows because the engine tells it through
`setSFXoccupy` (see the `waterline` section).

`Init` at `0x474760` normalises the difference between the two points and scales
it by 32768 in 16.16 (`0x4747CC`–`0x474819`), so a dot drifts **half a world
unit a tick whatever the distance between the vertices** — a modeller putting
them further apart makes no difference to the speed. The `16`/`8` is the colour
ramp period, and the life is six of those, 96 or 48 ticks. So a Wake1 dot
travels 48 world units in its life and a Wake2 dot 24.

Each call puts out **two dots: one immediately and one on the following tick**
(`0x474872` emits, then `0x474AEE` sets the next due time to now + 1, and the
emitter's end time is also now + 1, so it fires once more and never again).
There is no cooldown; the repetition rate is whatever `sleep` the ship's script
uses.

The spawn point is jittered by `rand()*7/0x8000 − 3` — a **whole number of world
units in [−3, +3], drawn independently for x, y and z** (`0x4749D9`, `0x474A00`,
`0x474A27`). Note the y: the scatter is not confined to the horizontal.

Per tick (`0x474580`) a dot moves by its velocity with **no gravity and no
wind**, and steps its palette index one place every `rampPeriod` ticks. It dies
either at its end time or, per `0x474720`, **the moment the terrain under it is
at or above sea level** — which is what makes a wake stop cleanly at a shoreline
instead of running up the beach behind the ship.

### `Thrust` is `Vtol` with one number changed

Both go to the spawner at `0x472330` with the vtable at `0x4FD5D8`, on render
layer 7, and the only difference is the constant: **6 for `Vtol`, 7 for
`Thrust`** (`0x481000` versus `0x48101F`). That number is used twice in `Init`
at `0x4742C0` — as the divisor for the per-tick drift (`0x47432C`–`0x47438B`)
and as the emitter's own lifetime (`0x4742CB`) — so a thrust plume is one puff
longer and each puff moves slightly more slowly. Same `flamestream` sequence,
same layer, same class.

### A correction to §4

§4 records that the wake emitter draws `smoke 1`, on the strength of the palette
ramp being loaded at `0x474A7F`. The handle **is** written into the puff at
`+0x00`, but none of the three routines that touch a wake puff — the per-tick
step `0x474580`, the render `0x4745E0` or the death test `0x474720` — ever reads
it. `0x4745E0` builds a one-pixel rectangle and calls `0x4BF6F0`, the generic
palette-index rectangle fill, exactly as the nanolathe spray does at `0x473B1D`.
**A wake dot is a single screen pixel of a solid palette colour, not a sprite.**

§4's slot map is unaffected — the ordering argument for `smoke 1` at
`globals+0x147CF` stands on its own from the loader at `0x429879`–`0x4298B8`.
Only the corroborating remark about the wake using it is wrong.

The colours are palette entries **97–103**, seven water blues, stepped from
palest to deepest: `(203,227,255)`, `(175,207,255)`, `(151,179,255)`,
`(123,151,255)`, `(103,127,255)`, `(83,107,239)`, `(63,91,227)`. Read out of the
shipped `palettes/PALETTE.PAL`; entry 161 in the same file is `(171,231,127)`,
which matches the nanolathe colour already in RWE and confirms the format.
Submarine bubbles use the same class and the same seven entries walked the other
way, starting at 103 with a step of −1 (`0x472584`, `0x4725A2`).

### What RWE now does

All five types are routed. Wakes are one function taking a reverse flag and a
ramp period; `Thrust` is the existing VTOL emitter with the divisor passed in.
The dot's colour steps discretely along the seven blues instead of fading, its
drift is the original's half a unit a tick instead of a guessed third, its life
is 96 or 48 ticks instead of a guessed 120, the jitter is whole units on all
three axes instead of fractional ones on two, the second dot is laid a tick
later rather than alongside the first, and a dot now dies when it drifts over
land.

Two things are left alone. RWE draws its dot as a small world-space quad rather
than a literal screen pixel, which is the same choice it already made for the
nanolathe spray. And the emission rate is still the script's, which is the
original's behaviour anyway.

### The small unit flags

Six flags that the bit tables above name but that had never been followed to
their readers. The parse sites are all in the FBI parser's long run of boolean
keys, where the helper `0x4C46C0` leaves its answer in `eax` and the
mask-and-shift follows immediately, so the §82 pipeline trap does not apply and
each bit is unambiguous:

| Key | String | Read at | Shifted at | Bit |
|---|---|---|---|---|
| `isairbase` | `0x503BC4` | `0x42C5DC` | `0x42C5F1` `shl eax,0x9` | `def+0x241` 9 |
| `noshadow` | `0x503B24` | `0x42C7DC` | `0x42C7EA` `shl eax,0x19` | `def+0x241` 25 |
| `digger` | `0x503AF0` | `0x42C880` | `0x42C895` `shl eax,0x1e` | `def+0x241` 30 |
| `upright` | `0x503B50` | — | `0x42C746` | `def+0x241` 20 |
| `norestrict` | `0x503620` | — | `0x42CB4B` | `def+0x245` 15 |
| `cantbetransported` | `0x5039DC` | `0x42CBB1` | `0x42CBBF` `shl eax,0x13` | `def+0x245` 19 |

Counted over the 189 FBIs of the extracted data RWE actually loads, which is
the base game plus Core Contingency: `isairbase` 4 (ARMASP, CORASP, ARMCARRY,
CORCARRY), `noshadow` 15, `upright` 28 set and 3 explicitly cleared,
`norestrict` 6, `digger` 2 (ARMAMB, CORTOAST), `cantbetransported` 1
(CORSUMO). The last two are worth the correction: earlier notes recorded that
no shipped unit sets either, which is true of the base game on its own and not
of the data on disk.

### `isairbase`: a damaged aircraft goes home

The definition bit is cached onto the instance as a unit is set up.
`0x485AE7`–`0x485B03` takes `def+0x241 & 0x200`, shifts it left twenty-one and
drops it into a cleared bit 30 of `unit+0x110`.

Each player keeps a **list** of its own air bases rather than searching for
them. `0x40AB96` onwards is the per-player sweep that also counts units by
type, and three tests put a unit on the list at `0x40ABE4`:

```
40abc0  mov  eax,[def+0x241]
40abc6  test al,0x40                ; builder
40abca  test ah,0x2                 ; isairbase
40abcf  test BYTE [unit+0x10e],bl   ; bl is 1, set at 0x40AAD0
40abe4  call 0x408F30               ; push_back onto the vector at player+0x25
```

**`unit+0x10E` bit 0 is the on/off state.** `0x403010` ACTIVATE calls
`0x48B090(1, 1)` and `0x403040` DEACTIVATE calls `0x48B090(1, 0)`; that routine
writes the byte at `0x48B0D8`, and unit init zeroes it at `0x485B7F`. The same
byte and bit gates the `istargetingupgrade` accumulator two instructions later
at `0x40ABFE`, which is what settles that it means "switched on" rather than
anything to do with visibility or ownership. So a pad that has been turned off,
or one still going up and so never activated, is on nobody's list and nothing
lands on it.

The query is `0x40B530(playerIndex, position, radius, out)`. It walks that
player's list, repeats the same three tests at `0x40B578`/`0x40B580`/`0x40B589`
— the last against a literal `0x1`, which is what pins the bit — and keeps
every unit inside the radius. The distance is flat: `0x40B5A0` reads only the
x at `unit+0x6A` and the z at `unit+0x72`, multiplies each difference by itself
as a 64-bit product and shifts thirty-two off (`0x40B5BB`–`0x40B5E3`), so the
comparison at `0x40B5E9` is in whole world units squared. **`y` is never
read**, so an aircraft is not pushed out of range by its own cruise altitude.

Seven callers, and **all seven pass the same two numbers**: `0x41055F`,
`0x4109B9`, `0x410F95`, `0x412613`, `0x412B91`, `0x413ADC`, `0x4153E3`. Each is
preceded by the identical health gate,

```
410525  mov  eax,[def+0x1fa]        ; max hit points
41052b  shr  eax,0x2
41052e  lea  ecx,[eax+eax*2]        ; (max >> 2) * 3
410531  cmp  edx,ecx                ; edx is WORD [unit+0x108], current health
410533  jae  <skip>
```

so the threshold is **three quarters of maximum with the quarter truncated
first**, and an aircraft sitting exactly on it is not hurt enough. The radius
pushed is `0xf00` at every site and the query squares it at `0x40B542`:
**3840 world units**.

If the returned vector is not empty — `0x40C560` is its size — the caller picks
**one at random, not the nearest**. `0x4B6C30(n)` returns zero without touching
the generator at `0x51FC88` when `n` is below two, and otherwise steps it once
and takes the remainder (`0x4B6C8C div edi`, `0x4B6C90 mov eax,edx`). The
chosen unit becomes the target of a mission built from the string at
`0x501B94`, **`VTOL_LANDING`** (`0x4105B9`, `0x41267B`).

Four more readers hang off the same flag:

- **On arrival**, `0x411E63`–`0x411ED3`: if the aircraft is below full health
  (`WORD unit+0x108 < DWORD def+0x1FA`) and the mission's target is a
  `builder && isairbase` whose build progress `unit+0x104` is exactly the float
  at `0x4FCC40`, the aircraft is handed a further mission built from
  `0x501C24`, **`SELFREPAIR`**.
- **The cursor**, `0x43EA83`: the selected unit `canfly` (bit 11) with an
  `isairbase` unit under the pointer gives cursor 13.
- **The right-click**, `0x43F735`–`0x43F75E`, `0x43F959`–`0x43F977` and
  `0x43FAEF`–`0x43FB19`: three separate arms of the order dispatcher test the
  same pair and all jump to `0x43FB1B`, the `VTOL_LANDING` mission.
- **Cargo**, `0x48AD0D`–`0x48AD1F`: a unit taken aboard something is unlinked
  from the world by `0x4384A0` **unless** the thing holding it is an air base,
  which is why aircraft parked on a carrier stay selectable.

### `cantbetransported`, and the whole of `CanTransport`

`0x489A90(this = transport, candidate)` in order:

```
489aa3  candidate def+0x245 bit 19  -> cantbetransported: reject
489abe  transport def+0x245 bit 8   -> canload required
489acb  walk transport+0x8A, next at +0x8E, owner back-pointer at +0x86,
489aec  counting against BYTE transportdef+0x22B; >= rejects
489afe  candidate must still exist
489b0b  WORD candidatedef+0x14A <= BYTE transportdef+0x22A
489b24  candidate unit+0x110 & 3 == 2 -> airborne: reject
489b39  a transport that cannot fly also wants WORD candidatedef+0x1C0 < 0
```

Two offsets fall out of that and both need a second site, because the FBI
parser pipelines its stores. `def+0x22A` and `def+0x22B` come from the same run
of keys as `waterline`: the strings pushed are `0x503DC8` "waterline",
`0x503DB8` "transportsize" and `0x503DA4` "transportcapacity", and under the
§82 rule their values land at `0x42C259`, `0x42C26E` and `0x42C284`. That makes
`def+0x22C` `waterline`, which §88 already had from elsewhere and which is the
check that the pipeline is being read the right way round, `def+0x22A`
**`transportsize`**, and `def+0x22B` **`transportcapacity`**.

`def+0x14A` is not parsed from the definition at all. `0x42CD5D` copies
`WORD [+0x4]` and `WORD [+0x6]` out of the **movement class** record into
`def+0x14A` and `def+0x14C`, and the record's fallback builder `0x440340` —
used when the FBI names no movement class — fills `+0x4` from the key at
`0x505484`, `FootPrintX`, and `+0x6` from `0x505478`, `FootPrintZ`, each store
immediately after its own call rather than pipelined. So the size test is
**the candidate's footprint X against the transport's TransportSize**.

That listing stops three checks early, and one conclusion drawn from it was
wrong: the full decode (§30) finds a mobility test after the airborne one --
the candidate's mover pointer at `unit+0x00` must be non-null, so buildings
never ride -- then a submerged test (the candidate's top, `y` plus the model
height at `def+0x16E`, must be above sea level) and a fully-built test. What
remains true: nothing asks whether the candidate is an *aircraft*, only
whether it is airborne right now, so a landed plane may be carried.

### `noshadow` and `digger` are both shadow-pass flags

Two passes, `0x459288`–`0x4592B6` and `0x4594A2`–`0x4594CA`, and they open
identically: a global option `WORD [0x511DE8+0x37F06] & 4`, then bit 25 of
`def+0x241`, then skip the draw. `digger` is tested only in the second, at
`0x4594D0`, where it selects a path through `0x45A470` and changes one
constant:

```
4594f2  shr eax,0x1e / and al,0x1
4594f7  neg al / sbb eax,eax        ; 0 or -1
4594fb  and eax,0x4b                ; 0 or 75
4594fe  add eax,0x32                ; 50 or 125
```

**It is not a terrain flag.** All ten of its read sites are in that one pass.

### `upright` and `norestrict`

`upright` has exactly one reader, `0x48A8BF`, inside the per-tick ground
placement `0x48A870`. Set, the unit stays vertical and takes its height from a
single sample under the centre; clear, `0x48A8CD` falls through to `0x48A938`
and thence `0x48A490`, which samples four rotated footprint corners and writes
`WORD unit+0x68` pitch and `WORD unit+0x64` roll from the slope.

`norestrict`'s four readers — `0x44C15F`, `0x44C4EA`, `0x44C73A`, `0x44CA59` —
are all the Unit Restrictions screen, which skips definitions carrying the bit
so a host cannot switch them off.

### What RWE now does with them

- **`isairbase` is in.** `findAirBaseToLandOn` in `UnitBehaviorService_util.cpp`
  is the original's predicate and query together: below three quarters health
  with the quarter truncated first, the owner's own `Builder` `IsAirBase` units
  that are activated and alive, within 3840 flat world units, and a draw taken
  with a modulo that skips the generator entirely when there is only one
  candidate — which every peer has to agree about or the simulations part
  company. The choice is remembered in `NavigationStateMovingToLandingSpot`,
  which is what stops it being re-rolled every tick the way the original's
  one-off mission swap does.
- **`cantbetransported` is in**, in the load handler and in the AI's ferry
  candidate filter, so the AI cannot book a passenger the simulation will then
  refuse and leave the transport hovering over it for ever.
- **`noshadow` is in**, as `unitCastsShadow` in `GameScene_util.cpp`.

And what was already right: RWE's transport eligibility had the capacity count,
the alive test and a size test, and `CanLoad` already gated the order button.
Its size test measures `max(footprintX, footprintZ)` where the original
measures footprint X alone — of the twelve shipped units with a non-square
footprint every one is a building, so the two readings never disagree on real
data, and the difference has been left where it is. RWE also refuses a
candidate that is not mobile, that `canFly`, or that is itself a transport,
none of which the original asks; those are older choices and were left alone
too.

### Not ported

- **`digger`.** RWE has both shadow passes now (§100), but the constant the
  flag swaps is not a projection constant at all: 50 and 125 are the height
  buffer's bias, `height = modelY + (digger ? 125 : 50)`, which
  `TOTALA-EXE-SHADING.md` §22 reads out of the span filler. RWE has no
  per-drawable height buffer — it has a real depth buffer — so there is still
  nothing for the flag to select.
- **`upright`.** RWE does not conform anything to the ground: `UnitState`
  carries a `roll` for the aircraft bank and no pitch at all. The flag chooses
  between two ground-placement routines neither of which RWE has, so honouring
  it would mean writing terrain conforming first.
- **`norestrict`.** There is no Unit Restrictions screen for it to hide a unit
  from.
- **The `VTOL_LANDING` cursor and the right-click.** RWE's `CursorType` has no
  landing cursor and no sprite loaded for one, and a player cannot yet send an
  aircraft to a pad by hand — only the idle path does. The three dispatcher
  arms above are where that would go.
- **`SELFREPAIR`.** What the pad does once the aircraft is on it was not
  followed past the mission push at `0x411ECE`, so RWE's pads still mend
  nothing. Everything that gets the aircraft there is in; this is the piece
  left.
- **The air base list itself.** RWE walks the unit map where the original keeps
  a per-player vector. The answer is the same and the walk only runs while a
  damaged aircraft has nothing else to do.

## 86. Patrol: what makes a unit leave its route

A patrol route is **N separate missions**, one waypoint each, strung on the
unit's mission list. Reaching a waypoint returns 6, and the service loop's
return-6 arm at `0x43B975` unlinks that mission and appends it at the tail —
that is the whole cycling mechanism. `0x43A020` closes the loop by appending
one more waypoint at the unit's *current* position, once, the first time a
patrol mission runs its state 0.

A builder or repairer given a patrol order gets `RepairPatrol` instead
(`0x43F3E6`), and **neither RepairPatrol handler ever calls the acquisition
search** — a repair patrol does not engage.

**The engagement rule is not route-relative at all.** The handlers poll
`0x43B700`, which

1. requires the fire order to be **exactly Fire At Will** — `cmp ecx,0x200000 /
   jne`, so Return Fire shoots back but never leaves the route; then
2. hands over to `0x40B7B0` in mode 0, which gathers candidates in a **flat
   circle of `SightDistance` around the unit's own current position**
   (`0x40B845`, `0x40B848`, and `0x40AD80` where `dy` never enters); and
3. rejects every candidate that fails `0x49ABB0` at `0x40B914` — the weapon
   eligibility test, **which ends in weapon slot 0's own range check**.

So the radius a unit will break off within is **`min(SightDistance, weapon-0
range)`, measured from the unit**. A Thunder breaks off at 350 because its bomb
reaches 1280; a Peewee breaks off at 180 because its EMG is shorter than its
eyes. Nothing reads `maneuverleashlength`, the leg, or the waypoint.

**A sighting becomes a real mission.** `0x43B1F0` prepends an attack mission
and, on `StandingMoveOrder = Maneuver`, a move back to the exact spot where
contact was made behind it, so the list becomes `[Attack] -> [Move home] ->
[Patrol_k] -> ...`. The patrol mission is never touched, so the route resumes
at **the waypoint it was already heading for**. The attack ends when the target
dies, when health drops below three quarters with a repair pad within 3840, or
when the leash trips — `maneuverleashlength` from the contact point, anchored
at `0x43B330` and tested at `0x4034D2` and in every air attack prologue.

For aircraft the mission is the ordinary one: a patrolling bomber that acquires
a ground unit gets a full `AirStrike` (`0x411F50`), handed the target exactly
as an ordered attack would be. Two traps come with that. A patrolling bomber
**ignores enemy aircraft entirely** — `0x43F2AA` skips `AIRSTRIKE` for an air
target and the fall-through produces no mission at all — and **a bomb never
auto-acquires**, because `0x408A7F` excludes `dropped` weapons from the
per-tick scan. The break-off is the only way a bomber ever drops anything.

> **RWE, 2026-09-24 (#70):** the patrol poll declines a flying candidate for
> a `dropped` weapon before it breaks off (`findEnemyToEngage`), so the
> refusal costs the patrol nothing here too. Before, the poll pushed the
> attack, the air prologue refused it a tick later, and the poll found the
> same aircraft again, so a bomber milled about beside a fighter parked on
> its route for as long as it stayed there.

**Two of the shipped bombers patrol without ever attacking, correctly.**
`ARMTHUND` and `CORSHAD` carry `StandingFireOrder=0`, so `0x43B700` fails on
every poll. `ARMPNIX` and `CORHURC` carry 2 and do engage. This is the same
thirteen units that ship on Hold Fire — the artillery, the nuke silos, the
mines — and it is deliberate.

Cadence: `VTOL_Patrol` polls every 30 ticks, ground `Patrol` every 45–74
(`rand(30)+30` then 15), and never on the tick a waypoint is reached, because
return 6 comes first. The air leg's goal is aimed **320 world units past** the
waypoint with a 336 tolerance (`0x410EC4`–`0x410F21`), which is a 16-unit
arrival radius and a deliberate fly-through; and it carries no altitude
instruction, only the state-0 take-off climb to `cruisealt/2`.

## 87. Pathfinding: one scheduler, a bug-walk, and a unit that never waits

There is one pathfinder in the game -- a singleton at `gm+0x14207`, built by
`0x40E9E0` -- and it is driven from `0x40EB70` as the **first** statement of
the per-tick unit update, before any unit's own update. The pivot that finds
it is the tagged allocation name `"AISearch touched mapentries"` at
`0x50192C`.

### The budget

**1333 work units a tick** (`this+0x48`, set at `0x40EAD3`), divided evenly
among the live players, each banking its share as credit and the scheduler
spending until the pool is empty. The tariff, all read directly:

| Action | Cost |
|---|---|
| Look at one unit slot, whether or not it wants a path | 1 (`0x40ECA2`) |
| Start a search | +100 (`0x40ED6D`) |
| Phase 1, the cheap walk | 1 per cell stepped, **uncapped** |
| Phase 2, the A\* | 1 per node expanded, sliced at 100 (`0x40EEAF`) |

Both numbers are confirmed by a debug console command: `"Search"` at
`0x416780` writes its first argument into `+0x48` and `atof(arg2) * 65536`
into the heuristic weight at `+0x54`.

Because merely *scanning* a slot costs 1, the budget is always fully spent --
a quiet tick sweeps about 1333 unit slots looking for someone who wants a
route.

### It degrades quality, not throughput

Every 150 ticks (`0x40EBA5`) the scheduler counts how many full sweeps of a
player's unit slots it managed and sets the A\* heuristic weight accordingly:
fewer than one sweep gives **9.0**, fewer than two **4.5**, otherwise **1.5**
(`0x18000` in 16.16). Loaded, the search is essentially greedy best-first;
idle, it is weighted A\* at about 1.7 once the already-inadmissible heuristic
(`18*max + 7*min` against step costs of 16 and 22) is counted.

### Two phases

**Phase 1 (`0x40E160`) is not a search.** It is a greedy axis-walk with wall
following -- a bug algorithm -- with no iteration cap at all, run to
completion on the tick the search starts. Its direction tables are at
`0x4FD670`/`0x4FD678`. It produces three things: the best heuristic value it
reached (`this+0x40`), a mark on every cell it stepped on, and an immediate
notification to the mission of whether the destination is reachable
(`0x100`/`0x200`, delivered before any route exists). If it got no closer than
it started, the search is abandoned there and no A\* runs at all
(`0x40E979`).

**Phase 2 is a weighted A\*** over path cells one heightmap square across,
with a 5-direction successor fan after the first node (`0x40EEA8`, so it can
never turn more than 90 degrees in a step and never reverses), step costs
16/22, turn costs `0/40/60/80/100` from `0x4FCA10`, a further 75 for turning
within five cells of the last turn, and +30 for a cell whose passability class
is "tight". Passability is a 2-bit-per-cell table per movement class, kept
current by units stamping themselves in (`0x440830`) rather than by the search
asking. **Unexplored ground reads as passable and free** (`0x40D831`), which
is why the original walks confidently into the fog.

**Nothing is ever truncated.** What is relaxed is the goal: any cell at least
as close as phase 1 managed is marked as a destination (`0x40DCA8`), so a
search for an unreachable place costs one bug-walk rather than an exhausted
open list, and phase 2 always completes at something. The path is emitted as
corners only (`0x40E050`) and the navigator keeps at most **20** of them
(`0x44F0BC`), so a long route is deliberately re-requested part of the way
along.

### The part that matters most: a unit never waits

`Navigator::SetGoal`, `0x44F2A0`, installs a **two-point path** -- where the
unit is standing, then the goal -- and raises "I have a path" alongside "I
want a path" (`0x44F3F2`-`0x44F41B`). The unit is moving on the tick it was
ordered, and the real route overwrites the straight line whenever the
scheduler reaches it. There is a ladder before that: keep the old path if its
tail already satisfies the new goal, or if its endpoint is less than half as
far from the goal as the unit is.

RWE installed nothing and stood still until the search came back, which at
four hundred a side was seconds of an army not moving. That is now fixed, and
it is most of what looked like a budget problem: with the straight line in
place the budget is a quality knob rather than a correctness one.

### Requests

There is **no queue and no coalescing**. The scheduler walks a round robin
over player slots and unit slots, one slot per work unit, asking each unit's
navigator `WantsPath` (`0x44F260`) -- which is rate limited to **once every 60
ticks** per unit. A hundred units given one order produce a hundred separate
searches to a hundred separate points, picked up in slot order. Fairness comes
from the rotation, so a saturated original produces worse paths rather than a
growing backlog.

Repathing has exactly three triggers (`0x44F239`): a new or changed goal, the
path running down to fewer than two waypoints, and the mover's "blocked" bit.

**Aircraft never touch the pathfinder at all** -- `0x438943` tests `canfly`
and installs no goal.

### What RWE cannot copy

`0x43DC4E` gives units belonging to a player of type 3 a stub navigator
holding **three** waypoints, and `0x44F4A0` bit-serialises exactly three plus
the blocked flag. That reads as network replication of paths, which would mean
the original computes routes locally and sends them. RWE is lockstep and
cannot: its budget, its cursor and any adaptive weight would all have to be
hashed simulation state.

### What is ported, and how faithfully

The straight-line stand-in is ported exactly. The first pass and the relaxed
goal are ported in shape rather than instruction for instruction, and the
difference is worth stating plainly.

`BugWalk.cpp` is a greedy walk with wall following, as the original's is, but
its wall trace and its leave test are written from the structure of
`0x40E3D5`-`0x40E445` rather than transcribed: the agent that decoded it could
not make that section byte-exact, and a reimplementation of a routine one does
not have exactly will fail on geometries the original would have walked. Three
consequences follow, and only the first is a real cost:

  - **RWE relaxes its goal more often than the original would**, because the
    walk gives up more often. A relaxed goal means a shorter search horizon
    and another request later, so it trades path optimality for cost. Measured
    on `path_bench` at 400 units the trade is currently favourable in both
    directions: expansions fall from 149,199 to 32,292 over 600 ticks, and
    over 3000 ticks 116 units reach their destination where 96 did before.
  - The walk is used **only to relax, never to refuse**. The original abandons
    a search outright when its walk gets no closer than it started
    (`0x40E979`); doing that here would turn a walk that failed spuriously
    into a unit that will not move, which is a far worse failure than a slow
    search.
  - The walk takes a **step limit** where the original has none. Running out
    of it costs a less relaxed goal and nothing else.

**The slicing is ported too, as of 2026-09-09.** RWE used to stop a search
dead at a thousand expansions and hand back whatever partial route it had,
which the unit walked before asking again; the original has no per-search cap
at all and slices its A\* instead, carrying the same search on next tick
(`0x40EEAF`). RWE now does the same. `AStarPathFinder` is a state machine --
`beginSearch`, `stepSearch(n)`, `takeResult` -- and `PathFindingService` gives
the search whatever is left of the tick's expansion budget and keeps it
exactly where it stopped if that runs out. A search therefore ends at the
goal or at an empty open list and nowhere else, so a partial result now means
one thing only: the place genuinely cannot be reached. The remaining bound is
the map, which is the same bound the original has.

Three things follow, and they are the shape of the original's scheduler
rather than a coincidence:

  - **One search at a time**, because the scratch grid stamps its cells with
    the search they belong to and starting a second search would stamp the
    first one's cells stale. The original is under the same constraint from
    the other end: its pathfinder is a singleton at `gm+0x14207` holding one
    set of map entries, which is why it can slice at all.
  - **A suspended search is checked before it is resumed.** A unit that has
    died, or been given a different order while its search was part way
    through, has no use for the answer, so the search is thrown away and the
    request started again against what the unit wants now.
  - **A save carries it, in five integers.** A half-finished A\* cannot be
    serialised and does not have to be: a search is a pure function of the
    world, the goal, and the cell it started from. The first two are in the
    save already -- the world because it *is* the save, the goal because it
    hangs off the unit's navigation state -- and only the third is lost,
    because a unit whose search is suspended keeps walking its straight-line
    stand-in and is no longer standing where the search began. So the save
    writes down that footprint and how many expansions had been done, and the
    load rebuilds the search from the footprint and runs it forward that far.
    A\* is deterministic, so what comes out is the state that was suspended
    and not an approximation of it. The request needs no name: the search
    always belongs to the one at the head of the queue.

    The obvious alternative -- drop the search on both sides of a save, so
    both timelines start it again from nothing -- is wrong, and it is worth
    saying why, because it looks right. It works for a saved game, where
    nobody can watch both timelines. It breaks a **replay**: keyframes go
    through the same `saveSimulationToJson` during playback, and the
    recording they are being compared against dropped nothing, so every
    keyframe taken while a search happened to be in flight would put the
    playback a few ticks out from the game it is replaying. Measured on
    `path_bench` at 400 units, a search is in flight on about a third of all
    ticks, so that is not a rare case.

The scratch grid's two per-cell indices went from int16 to int32 to pay for
this -- a cell is twelve bytes now rather than eight -- because the static
asserts that made int16 safe were bounded by the thousand-expansion cap, and
a search that can close every cell of a 512x512 map needs more than an int16
addresses.

One number is deliberately not changed with it: the budget stays at 4000
expansions a tick, forty times the original's hundred. Raising or lowering it
is its own question with its own roadmap entry, and doing both at once would
make neither measurable.

Still not ported: the adaptive heuristic weight (it would have to be hashed
simulation state, since RWE is lockstep and the original is not), the
restricted successor fan, the turn and straight-run costs, unexplored ground
reading as free, the 60-tick per-unit cooldown, and the 20-waypoint clamp. RWE
keeps an admissible octile heuristic and an eight-way fan.

**And a warning about the benchmark, found while measuring this.**
`path_bench`'s obstacles were an immobile unit definition with no yard map,
which is an assert in a Debug build and an empty-optional dereference in a
Release one. Every `path_bench` figure recorded before 2026-09-09 was
therefore measured with obstacles that did not reliably obstruct, and none of
them is a valid baseline -- including the "116 units arrive where 96 did"
line the bug-walk entry in the roadmap carries. The definition has a yard map
now.

## 95. What blocks a unit: the map square, the passability class, and why a hovercraft cannot cross a wreck

A play-test expected a hovercraft skimming the surface to pass over a wreck
lying on the sea bed, now that wreckage sinks (`TOTALA-EXE-WRECKS.md`). The
original refuses it, and not narrowly. This section is the whole of what a
feature contributes to movement, read out of the binary, because the answer
turns out to be structural rather than a rule that could have gone either way.

### The map square

The map is an array of **13-byte squares** at `globals+0x14287`, `[globals+0x14233]`
wide and `[globals+0x14237]` high — the stride shows up everywhere as
`lea edx,[eax+eax*2] / lea ecx,[eax+edx*4]`. The fields the movement code reads:

| Offset | Meaning |
|---|---|
| `+0x00` / `+0x02` | the two unit slots occupying the square |
| `+0x05` | the cell's **maximum** ground height |
| `+0x06` | the cell's **minimum** ground height |
| `+0x08` | **the feature type index** occupying the square |
| `+0x0A` / `+0x0B` | for a continuation cell, the y and x step back to the origin square |
| `+0x0C` bit 1 | a terrain flag; bit 0 and the owner nibble are the feature-pool bookkeeping of `TOTALA-EXE-WRECKS.md` |

`+0x08` is `0xFFFF` for an empty square, `0xFFFE` for a **continuation** cell
whose origin is `+0x0A`/`+0x0B` squares away, and `0xFFFB`–`0xFFFD` for void
markers that block outright. Anything else is an index into the feature
**definition** table at `[globals+0x1426F]`, stride `0x100`, bounds-checked
against `[globals+0x14253]`.

**The square names a feature *type*, never a feature instance.** That one fact
settles the play-test: the feature's own record — its position, and so its `y` —
is not reachable from any collision test, because no collision test ever looks
it up.

### The blocking bit and its five readers

`blocking` is bit 6 of `featdef+0xFE` (§15's feature flag table). Grepping the
whole `.text` for reads of that byte with `shr 6 / and 1` finds exactly five,
and they are the complete set:

| Address | In | Role |
|---|---|---|
| `0x47E0F7` | `0x47DFC0` | the per-movement-class footprint test |
| `0x47DEDE` | `0x47DE60` | the same for a single square |
| `0x47DCE8` | `0x47DB70` | the footprint test for a unit *definition*, excluding one unit id (nine callers; a building, `def+0x22F`, is handed to §27's yardmap walk instead) |
| `0x47E4AB` | `0x47E2D0` | "may *this unit* stand at this world position" |
| `0x47D5E6` | `0x47D4B6` | the building placement yardmap walk (§27) |

All five run the same seven instructions, and all five reject the square before
a single field of the movement class or the unit definition has been consulted.
`0x47DFC0`'s copy, in full:

```
47e081  cx = WORD[square+8]
47e085  cmp cx,0xffff / jne 47e090
47e08c  xor ecx,ecx / jmp 47e104          ; empty -> not blocked
47e090  cmp cx,0xfffb / jae 47e0ae
47e097  cmp ecx,[gm+0x14253] / jl 47e0ee  ; a real type index
47e0a7  mov ecx,1 / jmp 47e104            ; out of range -> blocked
47e0ae  cmp cx,0xfffe / je 47e0bc
47e0b5  mov ecx,1 / jmp 47e104            ; 0xFFFB..0xFFFD -> blocked
47e0bc  ... step back by square+0xA/+0xB to the origin square ...
47e0ee  edx = [gm+0x1426f]
47e0f4  shl ecx,0x8
47e0f7  cl = BYTE[edx+ecx+0xfe]
47e0fe  shr ecx,0x6 / and ecx,0x1         ; bit 6: blocking
47e104  test ecx,ecx / jne -> refuse the cell
```

**VERIFIED**, and what is *absent* is the finding:

- **No height term.** Neither the feature's `height` key nor its instance `y`
  is read. Blocking is two-dimensional.
- **No altitude term.** The moving unit's own `y` is not read either; the four
  height bytes the test does read are the map's, at `square+5`/`+0x6`.
- **No exemption.** Not for `canhover` (`def+0x241` bit 12), not for `floater`
  (bit 19), not for `amphibious` (bit 21), not for any movement class by name.
  A blocking feature blocks a hovercraft, a tank, a ship and a submarine
  identically.

### The movement class record, and the parser that fills it

Decoded from the MOVEINFO parser at `0x440340` and the cell test at `0x47DFC0`:

| Offset | Key | Default |
|---|---|---|
| `+0x04` | `FootprintX` (`0x505484`) | — |
| `+0x06` | `FootprintZ` (`0x505478`) | — |
| `+0x08` | `MaxWaterDepth` (`0x505468`) | `10000` — §30's seeded record |
| `+0x0A` | `MinWaterDepth` (`0x505458`), **signed** | `-10000` |
| `+0x0C` | `MaxSlope` (`0x50544C`) | `255` |
| `+0x0D` | `BadSlope` (`0x505440`) | **`MaxSlope / 2`** (`0x4403B9 shr eax,1`), so `127` unseeded |
| `+0x0E` | `MaxWaterSlope` (`0x505430`) | `255` |
| `+0x0F` | `BadWaterSlope` (`0x505420`) | **`MaxWaterSlope / 2`** (`0x4403E7`) |
| `+0x1C` | the crush threshold a standing unit's `+0x26` is tested against | — |

then three clamps, `0x4403F4`–`0x440417`: `MaxSlope = min(MaxSlope,
MaxWaterSlope)`, `BadSlope = min(BadSlope, MaxSlope)`, `BadWaterSlope =
min(BadWaterSlope, MaxWaterSlope)`. The seeded defaults are §30's, and they are
why `TANKHOVER3` — which names neither depth key — crosses any water at all;
§30 also records that a hovercraft FBI's own `MaxWaterDepth=0` is ignored once
a movement class is named.

The cell test spends them, `0x47E145`–`0x47E19E`, after the feature test has
passed:

```
square+6 >= seaLevel - MaxWaterDepth      ; deepest point of the cell
square+5 <= seaLevel - MinWaterDepth      ; shallowest point of the cell
spread = square+5 - square+6
if (square+6 >= seaLevel)                 ; dry
    spread <= BadSlope       -> free
    spread <= MaxSlope       -> tight
    else                     -> blocked
else                                      ; wet
    spread <= BadWaterSlope  -> free
    spread <= MaxWaterSlope  -> tight
    else                     -> blocked
```

Both depth tests are worst-case over the cell — the max-depth test reads the
cell's *lowest* corner and the min-depth test its *highest*.

### Free, tight, blocked — and where "tight" comes from

§87 records that the pathfinder's passability table is two bits a cell per
movement class and that a "tight" cell costs an extra 30. This is what fills
it. `0x47E1F0(grid, x, y)` asks `0x47DFC0` for the footprint itself, returns
straight away if the answer is 0 or 1, and otherwise probes the **four
one-cell-wide strips around the footprint** (`0x47E226`, `0x47E257`,
`0x47E28A`, `0x47E2B2`); if any of them is not free it downgrades the answer
from 3 to 1. So **"tight" means clearance, not slope**: a cell is free only
when the ring around the footprint standing on it is free too. `0x440830`
packs that answer into the grid at `[grid+0x18]`, two bits a cell, sixteen
rows of `y` to a dword, indexed `width*(y>>4) + x` with the pair at
`2*(y & 15)`.

RWE has the same idea in a different place: `AbstractUnitPathFinder::computeRoughTerrain`
calls `isAdjacentToObstacle` and adds a cost, which is the clearance half, and
uses `maxSlope / 2` as its rough threshold — which is exactly the original's
default for `BadSlope`, arrived at independently and now confirmed.

### The wreck, specifically

A feature's footprint is stamped into the squares once, at placement
(`0x423C50`, `0x423D5F`–`0x423D80` walking `featdef+0x94`/`+0x96` from the
square the caller gave). The falling-feature physics that sinks a wreck,
`0x424214`, writes only `feature+0x08..0x10` (position) and `feature+0x14..0x1C`
(velocity) and touches no square at all. **A corpse occupies exactly the cells
it was placed on, from the tick it appears until it is reclaimed, at whatever
depth it has reached.**

The shipped data is what makes this visible now that wreckage sinks. All
thirteen hovercraft in `ccdata` leave a corpse written like a land unit's —
`armah_dead`: `footprintx=3 footprintz=3 height=20 blocking=1` — while a ship's
is a flat plate authored to be driven over — `armroy_dead`: `footprintx=5
footprintz=5 height=4 blocking=0`. So in the original a hover battle really does
leave the sea bed obstructed and a naval one does not, and the difference is
the data rather than the engine.

### The verdict, and what RWE does

**The play-test's expectation is wrong: the original blocks hovercraft with
sunken wrecks too, and nothing was changed.** RWE already matches, item by
item:

| | Original | RWE |
|---|---|---|
| What blocks | `blocking` on the feature *type* | `isCollisionAt` tests `FeatureDefinition::blocking` — same |
| Height / depth | not consulted | not consulted — `occupiedGrid` carries no y |
| Hover exemption | none | none |
| A sunken wreck's cells | stamped at placement, never revised | `addFeature` stamps once; `updateFallingFeatures` moves only the position |
| A ship's corpse | `blocking=0`, driven over | parsed, defaulting false — same |

`src/rwe/sim/wreckcollision.test.cpp` pins it with the shipped numbers, so a
later reader tempted to "fix" the play-test's complaint has to argue with the
binary first.

Two smaller things fell out and are recorded in §91 rather than acted on:
`BadSlope`/`BadWaterSlope` are keys RWE does not parse, and the original's
"tight" band is wet-or-dry aware where RWE's rough test is not.

---

## 102. The ground path follower: an aim point on the segment, and two brakes into the corner

> **Ported, 2026-09-10.** Upstream issue #36, "a unit that overshoots a
> waypoint turns round and goes back for it". Three of the four things below
> are in. The corner the unit has left is kept in the waypoint list, so there
> is a segment to steer along at all — `PathFindingService::pathToWaypoints`
> no longer drops the start cell and `PathFollowingInfo` starts its iterator
> at `begin() + 1`. The aim point is projected onto that segment with the
> original's eighty-unit look-ahead. And the cosine speed factor is gone,
> replaced by the original's two brake tests, written in the original's
> operation order. The straight-line stand-in of §87 is two points now — the
> unit's own position, then the goal, which is what `0x44F3F2` writes — and it
> is walked on the tick the order arrives rather than the one after, because
> `Mover::Update` calls the navigator and then the follower in the same frame
> (`0x43DD28`).
>
> **One departure, and it is measured.** The waypoint advance radius stays at
> RWE's sixteen world units, and eight for the last one, rather than the
> original's five. Five is fine for a unit on its own and costs about a third
> of the arrivals in a crowd: at a hundred units in `path_bench`, five arrived
> 27 against sixteen's 49 and asked for 884 searches against 509, because a
> waypoint is a cell centre and a unit two cells across cannot always reach
> within five of one another unit is standing on — it circles until the
> collision repath rescues it. The original's answer to that is its blocked
> bit, which RWE has; RWE's units get in each other's way differently. With
> the other three in and sixteen kept, the same benchmark arrives 49 where the
> old follower arrived 45, and asks for 509 searches where it asked for 613.
>
> `followPath` in `src/rwe/sim/UnitBehaviorService.cpp` and `followSegment` in
> `src/rwe/sim/UnitBehaviorService_util.cpp`; the old `seek` and `arrive` are
> gone. `src/rwe/sim/pathfollowing.test.cpp` pins it.

§87 decodes the pathfinder and its scheduler and stops at the point where a
route has been handed to a unit; this is the other half, the per-tick step that
walks it.

The short answer is that the original **does** have a look-ahead — a pure
pursuit aim point projected onto the segment the unit is walking, with a
look-ahead of exactly 80 world units — and that it has two braking tests whose
whole job is to stop a unit arriving at a corner too fast to take it. What it
does not have is any test for *having passed* a waypoint. So the original can
turn round for one; it is arranged so that it rarely has to.

### Where it lives

The call graph from the mission to the arithmetic, all of it confirmed by
reading the calls rather than by inference:

| Address | Role |
|---|---|
| `0x4031D0` | ground mission **`Move`** — row 26 of the ground mission table at `0x4FC71A` (base `0x4FC490`, 25-byte records), display string `"Moving"` |
| `0x438930` | `Mission::SetPointGoal(&pos, radius)` — allocates a `PointGoal` and installs it on the navigator. `0x438943` is §87's `canfly` test: an aircraft gets no goal and no path |
| `0x44CF60` | `PointGoal::PointGoal(mission, x, z, radius)` |
| `0x43DD20` | `Mover::Update(unit)` — the per-tick entry. Calls `Navigator::Update` **first**, then the follower, then the collision/integration step |
| `0x44F1A0` | `Navigator::Update()` — the goal test and **the waypoint advance** |
| `0x43CD20` | **the ground path follower**, the subject of this section |
| `0x43D290` | the air follower (§1), chosen instead when `def+0x241` bit 11 (`canfly`) is set |
| `0x43CC20` | `Mover::Move(unit, accel)` — applies the acceleration, caps the speed, writes the velocity vector |
| `0x43D6D0` | integrate: add velocity to position, test the new map square, set the **blocked** bit |
| `0x43DA70` | pick the COB animation band and call `StartMoving`/`StopMoving`/`MoveRate1`..`3` |

`0x43DD20` in full, which is where the ordering comes from:

```
43dd24  ecx = [this]                      ; this+0x00 is the navigator
43dd28  call [[ecx]+0x08]                 ; Navigator::Update  (0x44F1A0)
43dd30  ecx = unit->def (unit+0x92)
43dd36  edx = def->[0x241] >> 11
43dd44  if (edx & 1) call 0x43D290        ; air
43dd4d  else         call 0x43CD20        ; ground
43dd55  call 0x43D6D0 (unit)              ; integrate + collide
43dd5d  call 0x43DA70 (unit)              ; animation band
43dd65  call 0x43DB50 (unit)              ; waterline / height
```

There is exactly **one** ground follower. Ships, hovercraft, tanks and kbots
all reach `0x43CD20`; the only branch anywhere in the movement dispatch is
`canfly`. `0x43CD20` has a single caller, `0x43DD4D`.

Three objects are involved and they point at each other in a cycle worth
writing down, because every offset below depends on it: `unit+0x00` is the
**mover**, `mover+0x00` is the **navigator**, and `navigator+0x08` is the unit.

### Where the path is kept

`Navigator`, vtable `0x4FD458` (the base-class table at `0x4FD428` sits
immediately before it in `.rdata`):

| Offset | Field |
|---|---|
| `+0x00` | vtable |
| `+0x04` | the goal object, or null |
| `+0x08` | the unit |
| `+0x0C` | **the waypoint array**: 20 entries of 4 bytes, `int16 x` then `int16 z` |
| `+0x5C` | waypoint count |
| `+0x60` | tick of the last path request (the 60-tick rate limit of §87) |
| `+0x64` | flags: bit 0 "I have a path", bit 1 "I want a path", bit 3 "dirty, send me" |

Vtable slots used below: `+0x04` `SetGoal` (`0x44F2A0`), `+0x08` `Update`
(`0x44F1A0`), `+0x0C` `GetWaypoints` (`0x44F150`), `+0x14` `HasPath`
(`0x44F290`), `+0x18` `WantsPath` (`0x44F260`), `+0x20` the three-waypoint
bit-serialiser (`0x44F4A0`).

**Waypoints are whole world units in an `int16`, not map squares and not
fixed point.** Two independent confirmations. `Navigator::SetGoal` writes the
*high word* of the unit's 16.16 position straight into a slot
(`0x44F3F9`: `mov dx, WORD PTR [ecx+0x6c]`, and `unit+0x6C` is the high half of
the 16.16 `x` at `unit+0x6A`), and `GetWaypoints` shifts a slot left by 16 to
hand it back (`0x44F178`: `shl ebp,0x10`). And the pathfinder's emitter builds
each corner the same way at `0x40E11F`–`0x40E13A`:

```
world = (2 * cell + footprint) * 8     ; i.e. cell*16 + footprint*8
```

which is `PointGoal::GetPosition`'s formula (`0x44D2E1`: `(footprint + 2*g) << 19`,
`<<19` being ×8 in 16.16) written for integers. A waypoint is therefore the
world position of the **centre of the unit's own footprint** placed on that map
square, and `>>4` on it gives the square index — which is what `SetGoal`'s
ladder does at `0x44F301` before asking the goal whether the path's tail is
already good enough.

The array is walked as `wp[j]` at `nav + 0x0C + 4j`, `j = 0 … count-1`. **`wp[0]`
is the corner the unit has left and `wp[1]` is the corner it is heading for.**
`SetGoal` makes that explicit for the straight-line stand-in of §87: at
`0x44F3F2` it sets `count = 2`, `wp[0] =` the unit's own position, `wp[1] =`
the goal.

The mover, for the fields the follower touches:

| Offset | Field |
|---|---|
| `+0x00` | the navigator |
| `+0x08`…`+0x13` | velocity, three 16.16 world units per tick |
| `+0x20` | **speed**, 16.16 world units per tick |
| `+0x24` | the turn applied this tick, a signed 16-bit angle |
| `+0x2A` | tick the unit last changed square or mode (`0x43D865`) |
| `+0x2E` | flags; **bit 2 is "blocked"**, set at `0x43D92C` from the footprint test `0x47DB70` (§95's third reader) |

And the unit, adding to §82's table: position `unit+0x6A` / `+0x6E` / `+0x72`
(16.16 x, y, z; the high words at `+0x6C`, `+0x70`, `+0x74` are read directly as
`int16` world units in several places), occupied map square `unit+0x76` / `+0x78`
(`int16`), footprint in squares `unit+0x7E` / `+0x80` (`int16`), the unit
definition at `unit+0x92`.

### The waypoint advance — `0x44F1A0`

`Navigator::Update` runs before the follower every tick and does two things.
The second is the advance:

```
44f1d7  edi = this->count
44f1da  if (count < 2) goto repath_check
44f1df  edx = this->unit
44f1e2  eax = (int16) this->wp[1].z          ; nav+0x12
44f1e6  ecx = (int16) unit->z                ; unit+0x74, the high word
44f1ea  edx = (int16) unit->x                ; unit+0x6C
44f1ee  ebp = (int16) this->wp[1].x          ; nav+0x10
44f1f2  ecx -= eax                           ; dz
44f1f7  edx -= ebp                           ; dx
44f1fb  ebp = dx*dx
44f200  edx = dz*dz
44f203  ebp += edx
44f205  cmp ebp, 0x19                        ; 25
44f208  jg repath_check                      ; not there yet
        ; pop wp[0]: memmove wp[1..count-1] down to wp[0..count-2]
44f223  count--
44f22c  if (count < 2) clear bit 0           ; "I have a path" goes away
44f235  set bit 3
```

**The rule.** When the unit is within **5 world units** of the corner it is
heading for — `dx² + dz² ≤ 25`, computed on the truncated integer positions —
the corner behind it is dropped and the list shifts down by one. Exactly one
waypoint is dropped per tick; there is no loop, so a unit cannot skip two
corners in a tick however fast it is going.

Five world units is under a third of a map square. It is a small number and it
is meant to be: nothing else in the original ever advances the list.

There is a `Navigator::PopN(n)` at `0x44F100` that would drop several at once.
Nothing calls it — it is in no vtable, `xref.py` finds no data reference, and
the listing contains no `call 0x44f100`. It is dead code.

**There is no "have I passed it" test anywhere.** No dot product against the
segment, no plane through the waypoint, no skip-ahead. The advance is a radius
and nothing else.

The first thing `Update` does, before the advance, is the goal test:

```
44f1a5  ecx = this->goal
44f1aa  if (goal == 0) skip
44f1b2  if (goal->IsSatisfiedBy(unit))       ; vtable +0x10
        {
44f1bc      Notify(goal, 0x20)               ; mission+0x4E |= 0x20, "arrived"
44f1c8      if (!goal->vf2C())                ; 0x44CEF0, returns 0 for a PointGoal
44f1d4          this->SetGoal(NULL)
        }
```

and the last thing is the repath decision (`0x44F239`), which confirms §87's
three triggers and adds nothing: a goal exists **and** (the mover's blocked bit
is set **or** the waypoint count has fallen below two). Grepping every
instruction in `.text` that ors or ands `[reg+0x64]`, and every `mov` into it,
finds writers at `0x44F0AE`, `0x44F0B1`, `0x44F0E9`, `0x44F0EF`, `0x44F13A`,
`0x44F231`, `0x44F251`, `0x44F2DA`, `0x44F2E1`, `0x44F2E8`, `0x44F313`,
`0x44F3D4`, `0x44F41B`, `0x44F441` and `0x44F55C` — every one of them inside
the navigator's own four methods. **Nothing outside the navigator can ask for a
path**, so there is no "too far from the route" re-request and no stuck timer
feeding one. `mover+0x2A` is stamped with the tick the unit last changed square
but no reader of it turns up in the mover, the navigator or the follower.

### The aim point — `0x43CD20`, first half

The follower opens by asking whether there is a path at all:

```
43cd2d  ecx = this->navigator
43cd31  call HasPath()                        ; vtable +0x14
43cd36  if (!HasPath) {
43cd3c      this->turnDelta = 0
43cd46      eax = -def->brakerate              ; def+0x19A
43cd52      Move(this, unit, eax)              ; 0x43CC20
            return
        }
```

**A unit with no path brakes at `brakerate` and does not turn.** That is the
whole of the stopping behaviour; see "Arrival at the goal" below.

With a path, it asks for three waypoints:

```
43cd61  ecx = this->navigator
43cd63  push 3 ; push 0 ; push &buf
43cd6e  call GetWaypoints(&buf, 0, 3)          ; vtable +0x0C = 0x44F150
```

`GetWaypoints` writes three 12-byte vectors — `(x<<16, 0, z<<16)` — and clamps
the index it reads with `index = min(i, count-1)` (`0x44F16A`–`0x44F172`). So
with a two-point path, `buf[2]` repeats the goal; with a longer one, `buf[2]` is
the corner after next. Name them `prev = buf[0]`, `next = buf[1]`,
`after = buf[2]`.

Then the aim point:

```
43cd75  dx = next.x - unit.x
43cd8c  dz = next.z - unit.z
43cda7  dist = hypot(dx, dz)                   ; 0x4FB440 then ftol at 0x4E43A0
43cdb4  if (dist <= 0x500000) goto aim_at_next  ; 80.0 in 16.16

43cdc3  sx = next.x - prev.x
43cdd5  sz = next.z - prev.z
43cdf4  seglen = hypot(sx, sz)
43ce01  if (seglen < 0x10000) goto aim_at_next  ; 1.0 world unit

43ce1f  ux = (sx << 16) / seglen               ; unit vector along the segment
43ce3b  uz = (sz << 16) / seglen
43ce55  back = min(dist - 0x500000, seglen)
43ce73  aim.x = next.x - ((ux * back) >> 16)
43ce99  aim.z = next.z - ((uz * back) >> 16)

aim_at_next:
43cebc  aim = next
```

**The rule.** The unit does not steer at the corner. It steers at a point on the
segment it is walking, placed so that the aim point is always **80 world units
closer to `next` than the unit itself is**, and never further back than `prev`.

Put on the line, that is plain pure pursuit with a look-ahead of 80 units: a
unit sitting on the segment 300 units short of the corner aims at a point 80
units in front of it. Put off the line, it is a corridor: the aim point is the
point on the segment at distance `dist − 80` from the corner, so a unit that has
drifted sideways is steered back onto the line rather than at the corner, and
the further off it is the further back along the line it aims. Inside 80 units
of the corner the projection stops and the unit homes on the corner itself.

Eighty world units is five map squares. `0x500000 / 65536 = 80`, and
`0x10000 = 1.0`; both are immediates in the instruction stream, not table
lookups.

The `min(…, seglen)` cap means the aim point lies on the closed segment
`prev … next` and never behind `prev`, and the `seglen < 1.0` bail-out is what
stops a degenerate segment dividing by zero. On the tick a unit is given an
order, `prev` is its own position and `seglen` is the whole distance to the
goal, so the stand-in path steers 80 units straight ahead — which is correct
and needs no special case.

### The turn — `0x43CD20`, second half

```
43cf0b  ax = HeadingTo(&unit.pos, &aim)        ; 0x48A980 -> atan2 at 0x4B715A
43cf12  ax -= unit->heading                    ; unit+0x66, 16-bit wrap
43cf1e  err = (int16) ax
43cf8a  if (err == 0) { this->turnDelta = 0; goto speed }

43cf96  rate = def->turnrate                   ; def+0x1BA, a WORD
43cfa5  if (err >=  rate)  this->turnDelta = +rate
43cfb5  else if (err <= -rate) this->turnDelta = -rate
43cfc9  else                   this->turnDelta = err
43cfdb  unit->heading += this->turnDelta
43cfdf  unit->flags(+0x110) |= 0x10000
```

**The rule.** The heading error is clamped to ±`turnrate` and applied. That is
all: no damping, no proportional term, no separate rate for large errors. The
same three lines appear standalone at `0x43CBB0`, which is the helper the rest
of the engine uses to point a unit at something.

`turnrate` is parsed as a plain **integer** (`0x4C46C0`, `atoi`) and stored as a
`WORD` at `def+0x1BA` (`0x42C243`), so it is a raw 16-bit-angle step per tick:
65536 units to the circle, and an FBI `TurnRate=550` is 3.02° a tick, about 90°
a second. `maxvelocity`, `acceleration` and `brakerate` go through `0x4C4800`
instead, which is `atof(value) * 65536.0` truncated (the multiplier is the
double at `0x4FDC20`, and it is exactly 65536.0) — so all three are 16.16 and
**per tick**, with no further division by the frame rate.

### Accelerate or brake — the two tests

This is the part that matters most for #36, and it is the part RWE did not
have.

```
43d000  speed = this->speed
43d00e  edx:eax = |err| * speed
43d022  A = (|err| * speed) / def->turnrate               ; 16.16

43d03e  edx:eax = speed * speed
43d048  t = (speed*speed) >> 16                            ; speed² in 16.16
43d06c  B0 = (t << 16) / (2 * def->brakerate)              ; 16.16
43d080  B  = (B0 * B0) >> 32                               ; squared, integer units²

43d090  C  = 4 * ((A * A) >> 32)                           ; (2A)², integer units²

43d0a2  if (distToAim² > C && distToAfter² > B)
43d0ac      accel = +def->acceleration                     ; def+0x19E
        else
43d0b8      accel = -def->brakerate                        ; def+0x19A
43d0c0  Move(this, unit, accel)
```

`distToAim²` is `ebp`, the squared distance to the aim point above, computed at
`0x43CECC`–`0x43CF09`. `distToAfter²` is `[esp+0x1C]`, the squared distance to
`buf[2]`, computed at `0x43CF37`–`0x43CF7D`. Both are 32.32 products shifted
right 32, so both are in whole world units squared, as are `B` and `C`.

Written out, with the squares removed:

- **The corner test.** Accelerate only while `distance to the aim point > 2 × speed × |heading error| / turnrate`. The right-hand side is twice the distance the unit would cover in the number of ticks it needs to finish turning. It is a turn-radius test in disguise, and it is what makes a unit slow down for a corner in proportion to how sharp the corner is. A unit pointing straight at its aim point (`err = 0`) always passes it.
- **The arrival test.** Accelerate only while `distance to buf[2] > speed² / (2 × brakerate)`, the textbook stopping distance. Because `GetWaypoints` clamps its index, `buf[2]` is the **final** waypoint whenever the path has three points or fewer, so this is the brake into the destination; on a longer path it is a brake into the corner after next, which is a mild look-ahead of its own.

Failing either test brakes at `brakerate`. There is no partial throttle: every
tick the unit either adds `acceleration` or subtracts `brakerate`, and the
speed cap does the rest.

### The speed cap — `0x43CC20`

```
43cc2d  this->speed += accel
43cc37  if (this->speed < 0) this->speed = 0

43cc46  i = unit->pitch >> 11                  ; unit+0x68, 16-bit angle
43cc4d  i = clamp(i, -5, +5)
43cc61  pct = (int8) byte at 0x505205 + i
43cc76  cap = (pct << 16) * def->maxvelocity   ; def+0x192
43cc7c  cap >>= 16
43cc95  cap /= 100.0                           ; 0x640000

43cca8  if (unit->y_high < seaLevel && !(def->[0x241] & 0x81000))
43ccc4      cap = (cap * 0x8000) >> 16          ; halved

43ccd3  if (this->speed > cap) this->speed = cap
43ccde  velocity = (-sin(heading)*speed, 0, -cos(heading)*speed)
```

The slope table at `0x505200`, eleven signed bytes indexed `-5 … +5` from
`0x505205`, is:

| index | −5 | −4 | −3 | −2 | −1 | 0 | +1 | +2 | +3 | +4 | +5 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| percent | 25 | 55 | 70 | 85 | 100 | 100 | 75 | 50 | 25 | 20 | 15 |

Each index step is `1 << 11` of a 16-bit angle, 11.25°, so the table covers
±56.25° of pitch and clamps beyond that. It is asymmetric on purpose: one
direction reaches 25% at the third step and the other only at the second-to-last.

`seaLevel` is the byte at `globals+0x1427F` in whole world units, compared
against the high word of the unit's `y`. The exemption is `def+0x241` bits 12
and 19 — `canhover` and `floater` — so a hovercraft or a ship keeps full speed
in the water and a submerged land unit is halved.

`0x4B70EF` and `0x4B7123` are sine and cosine of a 16-bit angle out of the
512-entry table at `0x509F00` (`(table[a] * r + 0x1000) >> 13`), and both
results are negated on the way into the velocity, which is why
`HeadingTo(&unit.pos, &aim)` at `0x48A980` computes `atan2(unit − aim)` rather
than `atan2(aim − unit)`: the two negations cancel.

`0x43DA70` then bands the speed for the animation — 0, or 1/2/3 by comparison
against `def->moverate1` (`+0x1AE`) and `def->moverate2` (`+0x1B2`) — and calls
the COB functions `StopMoving`, `StartMoving`, `MoveRate1`, `MoveRate2`,
`MoveRate3` (strings at `0x50523C`, `0x505230`, `0x50520C`, `0x505218`,
`0x505224`). A blocked unit reports band 0 whatever its speed.

### Arrival at the goal

`PointGoal`, built at `0x44CF60`, vtable `0x4FD328`:

| Offset | Field |
|---|---|
| `+0x04` | the mission |
| `+0x08` / `+0x0A` | the goal **map square**, `int16` |
| `+0x0C` | the radius in world units |
| `+0x10` | `(radius / 16)²`, in map squares squared |

The constructor converts the requested world position to a square with the same
footprint correction the collision code uses,
`(x - (footprint << 19) + 0x80000) >> 0x14`, and divides the radius by 16
rounding toward zero.

`IsSatisfiedBy(unit)` (`0x44D310`, vtable `+0x10`) is

```
(unit->squareX - gx)² + (unit->squareZ - gz)² <= radiusSq
```

on `unit+0x76` / `unit+0x78`, and `IsSatisfiedAt(x, z)` (`0x44D290`, vtable
`+0x14`) is the same test on supplied square coordinates — that is the one
`SetGoal` uses at `0x44F309` when it decides whether an existing path's tail
already satisfies a new goal.

The `Move` mission passes `mission+0x36 + 4` as the radius (`0x403232`), where
`mission+0x36` is the order's own tolerance. For a plain move order that is a
radius of 4 world units, which divides down to **0 map squares**: the unit must
end up standing on the goal square itself.

So arrival is a three-step sequence rather than one test:

1. `Navigator::Update` finds `IsSatisfiedBy` true, sets `mission+0x4E |= 0x20`, and calls `SetGoal(NULL)`.
2. `SetGoal(NULL)` clears both "I have a path" and "I want a path" (`0x44F2E1`).
3. The follower sees `HasPath` false and brakes at `brakerate` with no turn until the speed reaches zero.

The braking is not abrupt, because the arrival test has already been pulling
the speed down over the last `speed²/(2·brakerate)` world units. The two halves
are designed together: the follower gets the unit to the goal square slowly, and
the goal test then takes the path away.

### So what happens when a unit misses a waypoint?

Putting the advance, the aim point and the two brakes together, and this is the
answer #36 needs.

The original has **no** skip-ahead and **no** passed-the-plane test. If a unit
gets past the corner it was heading for without having come within 5 world
units of it, the corner is still `wp[1]`, and:

- while the unit is within 80 units of it — which it is immediately after passing — the aim point *is* that corner, so the unit turns back toward it;
- once it is more than 80 units past, the aim point becomes a point on the segment `prev → next` pulled back by `dist − 80`, which is behind the corner from the unit's point of view, so it turns back harder.

**So yes, the original will turn round and go back for a waypoint.** #36
describes real TA behaviour, not a divergence.

What the original has that keeps it rare is the corner brake. As a unit
approaches a corner the heading error to the aim point grows, and the corner
test `distToAim > 2·speed·|err|/turnrate` fails: the unit brakes at `brakerate`
every tick until it is going slowly enough that it can turn inside the distance
remaining. A unit therefore normally arrives at a corner at a speed its
`turnrate` can cope with, passes within 5 units, and advances. The
overshoot-and-return is the failure mode of the corner brake, not the normal
path of the code.

Two secondary effects make it rarer still. Sliding sideways off the line is
corrected before the corner arrives, because the aim point is on the segment
rather than at its end. And a unit that ends up genuinely lost will hit one of
the three repath triggers above — most often the blocked bit, since a unit
circling a corner in a crowd collides — and get a fresh route from where it
actually is.

### The FBI fields the follower reads

Confirmed by the string comparisons at `0x42C129`–`0x42C25F`, keeping §82's
warning in mind that the parser stores a key's value one push late.

| FBI key | Offset | Parsed by | Units | Read at |
|---|---|---|---|---|
| `maxvelocity` | `def+0x192` | `0x4C4800`, ×65536 | 16.16 world units per tick | `0x43CC76` (the cap), `0x43D9A2` (halved when blocked) |
| `brakerate` | `def+0x19A` | `0x4C4800`, ×65536 | 16.16 per tick² | `0x43CD46`, `0x43D02F`, `0x43D0B4` |
| `acceleration` | `def+0x19E` | `0x4C4800`, ×65536 | 16.16 per tick² | `0x43D0AC` |
| `moverate1` / `moverate2` | `def+0x1AE` / `+0x1B2` | `0x4C4800`, ×65536, default `maxvelocity × 2` | 16.16 | `0x43DA9A`, `0x43DAA9` — animation bands only |
| `turnrate` | `def+0x1BA` | `0x4C46C0`, `atoi`, `WORD` | 16-bit angle per tick | `0x43CBCA`, `0x43CF96`, `0x43D014` |
| `waterline` | `def+0x22C` | `0x4C46C0`, `BYTE` | world units | `0x43D72E` (§12) |
| `canfly` | `def+0x241` bit 11 | | | `0x43DD3E` — picks air over ground |
| `canhover` / `floater` | `def+0x241` bits 12 / 19 | | | `0x43CCAE` — exempt from the underwater halving |

Notably **not** read by the follower: `footprintx`/`footprintz` (they are baked
into the waypoints by the emitter and into the goal square by the constructor,
so the follower never sees them), and `movementclass`, which belongs to the
search and to the footprint test, not to the walking.

`brakerate` is read three times and the three uses are different: as the
deceleration when there is no path, as the deceleration when either test
fails, and inside the stopping-distance formula. It is *not* the "nose
re-aim only" field §82 describes — that note is about the air path at
`0x43D3BC`.

### What RWE does with this

RWE's follower is `followPath` in `src/rwe/sim/UnitBehaviorService.cpp`, with
the steering in `followSegment` (`UnitBehaviorService_util.cpp`), the turn in
`UnitBehaviorService::updateUnitRotation` and the speed in
`computeNewGroundUnitSpeed`. The path itself is `PathFollowingInfo` in
`src/rwe/sim/UnitState.h` — a `UnitPath` plus an iterator into its waypoints.
As with the original, only ground units come here: `moveTo` branches on
`canFly` and nothing else, exactly as `0x43DD3E` does.

| The original | RWE before this port | RWE now |
|---|---|---|
| Keeps the corner behind (`wp[0]`) as well as the one ahead | Kept only an iterator to the one ahead; `pathToWaypoints` started its loop at `++simplifiedPath.cbegin()`, so the cell the unit started in was dropped | Keeps it. `waypoints[0]` is the corner behind, the iterator starts at `begin() + 1`, and a path is never shorter than two points |
| Aim point projected onto the segment, look-ahead 80 world units | `seek` aimed straight at the current waypoint, always | The projection, the 80-unit look-ahead, the `min(dist − 80, seglen)` cap and the degenerate-segment bail-out |
| Advance when within **5** world units of the waypoint ahead | Advance when within **16**; **8** for the last one | Unchanged, and deliberately — see the note at the head of this section |
| Turn: clamp the heading error to ±`turnrate` | `turnTowards(rotation, targetAngle, turnRate)` — the same rule | Unchanged |
| Accelerate iff `distToAim > 2·speed·abs(err)/turnrate` **and** `distToAfterNext > speed²/(2·brakerate)`; otherwise brake at `brakerate` | Target speed was `maxVelocity × max(0, cos θ)`, squared again inside one turn radius of the goal | Both tests, in the original's operation order. Target speed is `maxVelocity` when both pass and zero when either fails, and `computeNewGroundUnitSpeed`'s accelerate/brake step — which already matched `0x43CC20` — does the rest |
| Arrival brake keyed to `buf[2]`, the final waypoint on a short path | `arrive` did the same `speed²/(2·brakeRate)` test, but only on the last waypoint; intermediate corners got no arrival brake at all | Keyed to the waypoint after next, clamped to the last the way `GetWaypoints` clamps its index, so every corner gets it |
| Speed cap: `maxvelocity × slope%[pitch]`, halved underwater unless `canhover`/`floater` | `computeNewGroundUnitSpeed` caps at `maxVelocity`, halves it below sea level, and scales by `computeSlopeSpeedFactor` — a rise-over-one-tile ratio with a floor of ¼, not the original's eleven-entry pitch table | Unchanged; the pitch table is still not ported |
| No path → brake at `brakerate`, no turn | Reaching the end of the path returned `true` and asked for another; the steering info was left as it was | The end of the path sets the steering to "hold this heading, stop", which is `0x43CD36` |
| Straight-line stand-in is two points: unit position, then goal (`0x44F3F2`) | `groundUnitMoveTo` pushed **one** waypoint, the destination | Two points, and the unit walks it on the tick the order arrives rather than the one after |
| Repath triggers: goal changed, count < 2, blocked | Goal moved, `inCollision` (with a 30-tick cooldown), path finished | Unchanged — the same three in substance |

**The two tests are written the original's way round.** `A = |err| × speed /
turnrate` first and `distToAim² > (2A)²` after, not cross-multiplied into a
division-free form; and `B0 = speed²/(2 × brakerate)` first and
`distToAfterNext² > B0²` after. The original divides first with 64-bit
intermediates, and where that division truncates is part of the arithmetic:
rearranging it moves the tick a unit starts braking on. The determinism worry
about a division is a real one in general and does not bite here — a
`SimScalar` divide is a single IEEE operation with one correctly rounded
result, so every peer gets the same number out of it. Both divisions are
guarded against a zero `turnrate` or `brakerate`, which the shipped data never
has and a test fixture can.

**No new state.** The only thing the follower needed that it did not have is
`prev`, and putting it in `path.waypoints` rather than beside them is what
keeps the bookkeeping free: `save_util.cpp` already serialises the vector and
the iterator's index, and `dump_util.cpp` already dumps the moving state.
`computeHashOf(NavigationStateMoving)` hashes `pathDestination`,
`pathRequested` and `reachableDestination` but not the path — the path is saved
and not hashed, which is defensible under CLAUDE.md's rule only because every
peer computes the same path from the same search, and a follower carrying extra
state must keep it inside `UnitPath` for the same reason rather than inventing a
second unhashed field beside it.

**And one thing found on the way.** A unit picked up by a transport kept the
speed it was walking at, and nothing runs a carried unit's physics, so the
number was still sitting there when it was set down again and the unit coasted
a world unit or two out of the spot the transport had chosen for it, in
whatever direction the transport happened to be facing. `loadUnitIntoTransport`
stops it now.

### What is not settled

- **`mover+0x2A`.** Stamped at `0x43D865` with the tick the unit last changed map square or movement mode. No reader turned up in the mover, the navigator or the follower. It may be a stuck timer read from somewhere further away, or dead. Stated as found.
- **`mover+0x04`.** The scheduler checks it non-null before touching the navigator (`0x40ED4A`) and nothing else read here uses it.
- **The `min(i, count-1)` clamp's intent.** It makes `buf[2]` the last waypoint on a short path, which is what turns the second test into an arrival brake. Whether that was the intent or a convenience that happens to work is not decidable from the code.
- **Bit 2 of `mover+0x2E`.** Read as "blocked" here and in §87, and the only writer found is `0x43D92C`, which sets it when `0x47DB70` — §95's footprint test for a unit definition — refuses the square. But that writer sits inside a branch gated on `unit+0x96` (`0x43D8E3`), so there may be a second writer on a path not walked here.
- **The exact sign convention of the pitch index** into the slope table. The table is asymmetric, so which end is uphill matters; the code is `clamp(pitch >> 11, -5, +5)` with no negation, and the table's shape (100% at −1 and 0, falling faster on the positive side) reads as positive = uphill, but that is inference from the numbers rather than a decode.
- **Whether ships differ.** They do not take a different follower — `0x43DD3E` branches on `canfly` alone — but `0x43DB50`, the waterline step, was not read past its first few instructions and may adjust the speed for a floater in a way the follower does not see.

---
