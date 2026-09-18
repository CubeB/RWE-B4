# TA aircraft attack missions

How `TotalA.exe` (GOG release, image base `0x400000`, the binary identified by
hash in `docs/TOTALA-EXE.md`) makes aircraft attack. Everything below was read
out of the binary; anything inferred rather than read is marked **[inferred]**.

Companion to `docs/TOTALA-EXE.md` §1 (the flight model), which this does not
contradict: that document covers how an aircraft *moves toward a point*; this
one covers *which point it is sent to*.

---

## 0. Conventions

### Numbers

* Positions are three dwords `{x, y, z}` in **16.16 fixed point**. The integer
  part of `x` is therefore the word at `+2` of the vector, which the code reads
  directly (`unit+0x6C` for `x`, `unit+0x74` for `z`).
* Angles are a 16-bit turn: `0x10000` = 360°, `0x4000` = 90°.
* Move-goal **arrival tolerances are in whole world units**, not 16.16. Proved
  at `0x44E5FF`–`0x44E602`: the goal's distance test multiplies the raw 16.16
  separation by the float at `0x4FD3E8` = `1.52587890625e-05` = 1/65536 before
  comparing it against the tolerance word.

### The direction convention — get this right or every waypoint mirrors

`0x4B70EF(angle, v)` returns `sin(angle)·v` and `0x4B7123(angle, v)` returns
`cos(angle)·v`, both off the 512-entry table at `0x509F00` (13 fractional bits,
byte index `((angle+0x20)>>6) & 0x3FE`).

Every waypoint in these handlers is built as `base − (sin θ·r, ·, cos θ·r)`,
i.e. **minus** the polar vector. That minus is not an inversion — it *is* the
direction convention. The proof is the `BrakeRate` nose re-aim in the air
mover, `0x43D444`–`0x43D479`, which strips excess speed and re-injects it
**along the nose**:

```
43d444   mov cx, WORD PTR [ebx+0x66]   ; the unit's heading
43d452   call 0x4b70ef                 ;   sin(heading)·excess -> ebp
43d466   neg ebp
43d468   call 0x4b7123                 ;   cos(heading)·excess -> eax
43d472   neg eax
43d477   add edx, ebp                  ; vel.x += -sin(heading)·excess
43d479   add ecx, eax                  ; vel.z += -cos(heading)·excess
```

So, writing it once and for all:

| | |
|---|---|
| `Angle(v)` | `atan2(v.x, v.z)` as a 16-bit turn — routine `0x4B715A` |
| `Dir(θ)` | `(−sin θ, −cos θ)` — the direction an object with heading θ **faces** |
| `0x48A980(a, b)` | `Angle(a − b)`, so `Dir(0x48A980(a,b))` is the unit vector **from a toward b** |

With that, every waypoint below reads as `base + Dir(θ)·r`, which is how this
document states them. (The prompt's phrasing "`0x48A980(a,b)` = bearing from b
to a" describes the same instruction sequence; it is the polar convention that
differs, and under the `−polar` sign the game actually uses, `Dir(0x48A980(a,b))`
points from `a` to `b`.)

### Verification

The arithmetic of every waypoint below was transcribed into
`scratchpad/vtolgeom.cpp` (built with the MSYS2 g++, output in
`vtolgeom.exe`) and replayed with real FBI numbers. It reproduces the exact
fixed-point table trig. Results are quoted inline where they matter.

---

## 1. Mission name → handler

### How a name becomes a mission

`0x438760` is `MissionId::FromName(const char*)` — a **binary search** over a
runtime array of 25-byte records at `ds:0x512344 .. ds:0x512348` (the count is
`(end − start) / 25`, done with the `0x51EB851F`/`sar 3` magic at `0x43877E`).
It compares `[record+0x15]` with `0x4F8A70`, which is **`stricmp`** (`0x4F8A9A`
onward folds case), so the uppercase names in the order code match the mixed-case
names in the table. It stores the record's index as a **byte** in the caller's
buffer, or `0` when not found (`0x438819`). `0x438830` / `0x438850` turn that
byte back into a record pointer.

### The record

The static tables are in `.rdata`. The **VTOL table begins at `0x4FCA18`**, 22
records of 25 bytes.

> **Corrected 2026-09-10** (`TOTALA-EXE.md` §103). This sentence used to say
> `0x4FCA7C` and 18 records, disagreeing with the table below it, which has
> always been right: `0x4FCA7C` is row 4, `VTOL_Unload`, and the array runs
> from row 0 at `0x4FCA18` to row 21 at `0x4FCC25`.

| Offset | Field |
|---|---|
| `+0x00` | `char*` display name ("Engaging target", "Airstrike", …) |
| `+0x04` | **handler function pointer** |
| `+0x08` | second function pointer (`0x439740` / `0x4394E0` / `0x438C00` / null) |
| `+0x0C` | dword flags |
| `+0x10` | word, `+0x12` word, `+0x14` byte |
| `+0x15` | `char*` mission name |

The handler is at `+0x04`, confirmed independently by the three dispatch sites
`0x43A21A`, `0x43B87C`, `0x43BB21`, each of which does
`call DWORD PTR [eax + edx*1 + 4]` with `edx = tableBase` and `eax = 25 × missionId`.

The record boundary is settled by the display names: with this alignment all 22
rows read sensibly (`VTOL_LandIfCan`→"Seeking to land", `VTOL_GetRepaired`→"Under
repair", `AirStrike`→"Airstrike", `VTOL_Patrol`→"Patrolling"); shifted by one
record they are all nonsense.

### The VTOL table, in full

| # | Record | Mission name | Handler | Display |
|---|---|---|---|---|
| 0 | `0x4FCA18` | `VTOL_Standby` | `0x40F7D0` | Standby |
| 1 | `0x4FCA31` | `VTOL_Move` | `0x40FA20` | Moving |
| 2 | `0x4FCA4A` | `VTOL_Landing` | `0x4118E0` | Landing |
| 3 | `0x4FCA63` | `VTOL_Pickup` | `0x4111B0` | Loading |
| 4 | `0x4FCA7C` | `VTOL_Unload` | `0x411560` | Unloading |
| 5 | `0x4FCA95` | `VTOL_Follow` | `0x40FBE0` | Guarding |
| 6 | `0x4FCAAE` | `VTOL_Patrol` | `0x410E70` | Patrolling |
| 7 | `0x4FCAC7` | **`AirStrike`** | **`0x411F50`** | Airstrike |
| 8 | `0x4FCAE0` | **`AirToAir`** | **`0x412D40`** | Engaging target |
| 9 | `0x4FCAF9` | **`AirToGround`** | **`0x412710`** | Engaging target |
| 10 | `0x4FCB12` | **`AirToGroundHover`** | **`0x413470`** | Engaging target |
| 11 | `0x4FCB2B` | `VTOL_MobileBuild` | `0x413D80` | Nanolathing |
| 12 | `0x4FCB44` | `VTOL_HelpBuild` | `0x414380` | Nanolathing |
| 13 | `0x4FCB5D` | `VTOL_RepairPatrol` | `0x4152F0` | Repair patrol |
| 14 | `0x4FCB76` | `VTOL_RepairUnit` | `0x414E70` | Repairing |
| 15 | `0x4FCB8F` | `VTOL_Reclaim` | `0x414770` | Reclaiming |
| 16 | `0x4FCBA8` | `VTOL_ReclaimUnit` | `0x414A80` | Reclaiming |
| 17 | `0x4FCBC1` | `VTOL_Evade` | `0x413BC0` | Evading |
| 18 | `0x4FCBDA` | `VTOL_SeekAttack` | `0x4103E0` | Seeking to attack |
| 19 | `0x4FCBF3` | `VTOL_SeekGuard` | `0x410850` | Seeking to guard |
| 20 | `0x4FCC0C` | `VTOL_GetRepaired` | `0x415250` | Under repair |
| 21 | `0x4FCC25` | `VTOL_LandIfCan` | `0x40F2A0` | Seeking to land |

A second table of the same shape holds the ground missions, starting at
`0x4FC490` (`Stop` → `0x401C20`, `Attack_NoMove` → `0x402160`,
`Attack_Chase` → `0x4034A0`, `Attack_Kamikaze` → `0x403260`,
`Suppress` → `0x4038A0`, …). Both are merged and sorted into the runtime vector
at startup, which is why the binary search works on names that are not in
source order in either table.

### **Answer to question 1**

> **`AirToGround` (the bomber/strafing pass) is `0x412710`.
> `AirToGroundHover` (the gunship) is `0x413470`.**
> `0x411F50` is **`AirStrike`**, not `AirToGround` — the prompt's hypothesis
> was wrong, and this matters: `AirStrike` is the only handler that reads
> `attackrunlength`, and it is the mission a *dropped-weapon* aircraft (the four
> bombers) gets. `AirToAir` is `0x412D40`.

---

## 2. The mission framework

### Mission object

Allocated 0x56 bytes (`0x4B4F10(0x56)`), constructed by
`0x43A0C0(thiscall; missionId, targetUnit, position, a, b, leash)`:

| Offset | Field | Set at |
|---|---|---|
| `+0x04` | mission id byte (index into the merged table) | `0x43A0E0` |
| `+0x05` | **state byte** | `0x43A13C` (0) |
| `+0x06` | **wake mask** — which events re-enter the handler | `0x43A13F` (0) |
| `+0x0A` | timer deadline (game tick), `-1` = none | `0x43A145` |
| `+0x0E` | owning unit | `0x43B798` |
| `+0x16` | target unit | `0x43A0E3` |
| `+0x22` | target position (3 dwords, 16.16) | `0x43A169` |
| `+0x2E`, `+0x30` | **leash anchor** (integer x, z) | caller, `0x43B330` |
| `+0x36` | scratch — the gunship's swing-side toggle | `0x43A116` |
| `+0x3A` | scratch — the gunship's out-of-range counter | `0x43A119` |
| `+0x3E` | **leash radius** = `maneuverleashlength` | `0x43A11C` |
| `+0x42` | flags | |
| `+0x46` | creation tick | `0x43A12E` |
| `+0x4A` | next mission in the unit's list | |
| `+0x4E` | pending-event mask | |
| `+0x52` | the currently installed move goal | `0x438921` |

Unit fields used: `+0x00` mover, `+0x04 + 28·i` weapon slot *i*,
`+0x10 + 28·i` weapon *i*'s definition, `+0x5C` mission list head,
`+0x66` heading, `+0x6A` position, `+0x82` spatial bucket,
`+0x86` carrier/pad, `+0x92` unit definition, `+0x96` player,
`+0xBA` pending unit events (word), `+0x108` current health,
`+0x10E` activation flags, `+0x110` state flags.

### The service loop, `0x43B7C0`

Per unit per tick, on the head mission:

1. If `currentTick ≥ mission+0x0A`, set bit 0 of `mission+0x4E` and clear the timer.
2. `flags = (mission+0x4E | (word)unit+0xBA) & mission+0x06`.
3. If `mission+0x06 ≠ 0` and `flags == 0`, do nothing this tick. **A mission
   whose wake mask is 0 therefore runs every tick.**
4. Clear the consumed bits from `unit+0xBA` and `mission+0x4E`; set `mission+0x06 = 0`.
5. If `flags & 0x10000`, clear all three weapons' targets (`0x48A0F0(unit, 0..2)`).
6. `ret = handler(unit, mission, flags)` — argument order is
   **(unit, mission, flags)**, `0x43B870`–`0x43B87C`.

### Return codes, jump table at `0x43BAA4`

| Return | Effect |
|---|---|
| 0 | `mission+0x05 = 0` — restart the state machine (`0x43B89F`) |
| **1** | **`mission+0x05++` — advance one state** (`0x43B897`) |
| **2** | **nothing — stay in this state** (`0x43B99F`) |
| 3 | set the timer to `rand(15) + 30` ticks (`0x43B890`) |
| 5, 8 | unlink and delete the mission — it is finished/abandoned (`0x43B8A8`) |
| 6 | rotate the mission to the end of the list (`0x43B975`) |
| 7 | **flush the whole mission list** — the unit goes idle (`0x43BA3D`) |
| 9 | if there is no next mission, restart at state 0 with a `rand(30)+30` timer; else delete (`0x43B8E5`) |

### Move goals

A goal is a 0x36-byte object built by one of:

| Routine | Meaning |
|---|---|
| `0x44E2D0(thiscall; mission, &pos)` | goal at an **absolute position**. flags `0x20`. |
| `0x44E190(thiscall; mission, unit)` | goal at a **unit's current position**. flags default. |
| `0x44E330(thiscall; mission, unit, &pos)` | absolute position **with an attached unit**. flags `0xA3`. |

Fields: `+0x08` flags (word), `+0x0A` **arrival tolerance** (word, world units),
`+0x0C` altitude above ground, `+0x0E` heading offset, `+0x12` unit,
`+0x1A` attached unit, `+0x26` position, `+0x2A` absolute target Y.

| Routine | What it does |
|---|---|
| **`0x44E730(thiscall; int tol)`** | `flags \|= 0x10`; `goal+0x0A = tol`. **Sets the arrival tolerance.** |
| **`0x44E6C0(thiscall; int alt)`** | `flags \|= 0x08`; `goal+0x0C = alt`; and if `flags & 0x20`, `goal+0x2A = min(max(groundHeight(goalPos), seaLevel) + alt, 511) << 16`. **Sets cruise altitude above ground, clamped to 511.** |
| `0x44E720(thiscall; int)` | `flags \|= 0x40`; `goal+0x0E = v` (heading offset, used by the relative-goal resolver). |
| **`0x4388D0(thiscall on the mission; goal)`** | Releases the previous goal, clears `mission+0x4E` bits 5–9, hands the new goal to the unit's navigator (`unit->mover->nav->vtable[1]`), stores it at `mission+0x52`. **Installs the move goal.** Passing null just cancels. |
| **`0x439E80(thiscall on the mission; int ticks)`** | `mission+0x06 \|= 1`; `mission+0x0A = currentTick + ticks`. **Sets a wake timer.** |
| `0x44E5B0` (goal vtable `+0x10`) | `IsSatisfied`. If a tolerance was set, arrival is purely `horizontalDistance < tolerance`. If not, it is `distance < 0.5` **and** (flag 8) `|Δy| ≤ 1.0`. |
| `0x44E3C0` (goal vtable `+0x20`) | Resolves the goal's world position; re-derives it from the attached unit only when flag `0x01` is set **and flag `0x80` is clear**. `0x44E330`'s `0xA3` has `0x80` set, so those goals are **fixed world points**, not tracking points. |

### Weapon control

| Routine | What it does |
|---|---|
| **`0x489800(thiscall on the unit; slot)`** | Sets bit `0x10` of the slot's flag byte (`unit + 0x1F + 28·slot`) and clears its target. Bit `0x10` is what the automatic target-acquisition scan at `0x408A67` requires, so this is **"weapon free — pick your own targets"**. `slot == 3` means slots 0, 1 and 2. |
| **`0x4898B0(thiscall on the unit; slot)`** | Clears bit `0x10` and clears the target: **"the mission owns this weapon"**. |
| `0x48A060(unit, targetUnit, slot)` | Point weapon `slot` at a **unit**. |
| `0x48A0A0(unit, &pos, slot)` | Point weapon `slot` at a **ground position** (stores `pos.x>>16`, `pos.z>>16`). |
| `0x48A0F0(unit, slot)` | Clear weapon `slot`'s target. |
| `0x49ABB0(unit, target, slot)` | **Is the target inside weapon `slot`'s range** (and at a legal depth for the weapon's water flags)? `dx² + dz² ≤ range²`, computed in whole world units at `0x49AC9E`. |
| `0x48AAC0(unit, 0, -1, 2)` | Detach from a carrier/pad. **[inferred]** — called only when `unit+0x86 ≠ 0`, and `unit+0x86` is the field the spatial-bucket code (`0x47CB73`) uses to mean "not on the grid". |
| `0x48B090(thiscall on the unit; set, mask)` | Sets/clears bits of `unit+0x10E`. Called `(1, 1)` in every state 0. |
| `0x43D210(thiscall on the mover; unit, mode)` | Sets the mover's movement mode (`mover+0x2E` bits 0–1). Mode 1 also resets the bank accumulator. |
| `0x40B530(playerIdx, &centre, radius, &out)` | Collects the player's units within `radius` that match `def+0x241` bits 6 and 9 and `unit+0x10E` bit 0 — **the player's active air repair pads**. |
| `0x43ACB0(unit, mission)` | **Push** the mission to the front of the unit's list. |
| `0x43AD10(unit, mission)` | **Append** the mission to the end of the list. |
| `0x438880(thiscall on the mission; str)` | Announce once: if `mission+0x42 & 0x2000`, clear that bit and call `0x47F780(unit, 5, str)`. Every attack mission passes `"Attacking"` at `0x50129C`. |
| `0x4B6C30(n)` | `rand(n)` — LCG state at `0x51FC88`, returns `state % n`, and **returns 0 for n < 2** (`0x4B6C38`). **Confirmed.** |

### Unit definition fields used here

Confirmed in the FBI parser (remember the compiler pipelines the stores: each
`mov [ebp+off], ax` holds the result of the *previous* `call 0x4C46C0`):

| Key | Offset | Site |
|---|---|---|
| `maxdamage` | `def+0x1FA` (dword) | `0x42C38F` / `0x42C39E` |
| `canfly` | **bit 11** of `def+0x241` | `0x42C6E1` / `0x42C6F4` (`shl eax,0xb`) |
| `hoverattack` | **bit 27** of `def+0x241` | `0x42C823` / `0x42C83C` (`shl eax,0x1b`) |
| `builddistance` | `def+0x212` | prior finding |
| `maneuverleashlength` | `def+0x214` | prior finding |
| `attackrunlength` | `def+0x216` | prior finding |
| `cruisealt` | `def+0x21C` | prior finding |
| weapon *n* definition | `def+0x1EE`, `+0x1F2`, `+0x1F6` **[inferred from `+0x1EE` being weapon 1]** | `0x43F292` |
| `range` (weapon TDF) | `weapondef+0xDC` | `0x42E512` / `0x42E52A` |
| `dropped` (weapon TDF) | **bit 8** of `weapondef+0x111` | `0x42EAFB` / `0x42EB16` (`shl eax,0x8`) |

The mover object at `unit+0x00`: `+0x00` navigator, `+0x08..0x13` velocity,
**`+0x20` current speed as 16.16** (written at `0x43D688` as
`ftol(sqrt(vx²+vy²+vz²))` of the raw fixed-point velocity), so **`word[mover+0x22]`
is the integer part of the current speed in world units per tick**,
`+0x24` last turn step, `+0x2E` bits 0–1 movement mode (1 = landed, 2 = flying).

---

## 3. Which attack mission an order produces — `0x43F154`

The order-type jump table is at `0x4401EC`; the `ATTACK` case starts at
`0x43F154`. `ebp` = the attacking unit, `[esp+0x20]` = its definition, `edi` =
the target unit, `edx` = the target's definition, and `eax` is set to 1 at
`0x43F128` when there is a target unit and it is **not** allied (the alliance
byte `player[targetPlayerIdx + 0x108]` is 0).

```
if attacker def+0x245 bit 4 clear          -> no mission                (0x43F154)
if attacker+0x110 bit 31 clear             -> ATTACK_KAMIKAZE branch    (0x43F171 -> 0x43F38E)

--- attacking a position, or an ally (eax == 0) -----------------  0x43F17E
    if attacker weapon0 def +0x111 bit 17  -> no mission
    if attacker def+0x241 bit 11 (canfly) clear -> "SUPPRESS"
    if weapon1 def (def+0x1EE) +0x111 bit 8 (dropped) -> "AIRSTRIKE"    (0x43F1C6)
    else                                              -> "AIRTOGROUND"

--- attacking an enemy unit (eax == 1) --------------------------  0x43F1D4
    ... depth / water-weapon eligibility checks ...
    if attacker def+0x241 bit 11 (canfly) clear -> ATTACK_CHASE / ATTACK_NOMOVE
    dropped = weapon1 def +0x111 & 0x100                            (0x43F29D)
    targetIsAircraft = target def+0x241 & 0x800
    if  dropped and not targetIsAircraft   -> "AIRSTRIKE"            (0x43F2AA)
    if !dropped and     targetIsAircraft   -> "AIRTOAIR"             (0x43F2CB)
    if !targetIsAircraft and !hoverattack  -> "AIRTOGROUND"          (0x43F2FA)
    if !targetIsAircraft and  hoverattack  -> "AIRTOGROUNDHOVER"     (0x43F323)
    otherwise                              -> no mission
```

Confirmed in the shipped data: the four aircraft with `attackrunlength`
(`ARMTHUND`, `ARMPNIX`, `CORSHAD`, `CORHURC`) are exactly the four whose
`Weapon1` (`ARMBOMB`, `ARMADVBOMB`, `CORBOMB`, `CORADVBOMB`) has `dropped=1`
and `range=1280`. `ARMBRAWL`'s `VTOL_EMG` has `range=370` and no `dropped`;
`CORAPE`'s `VTOL_ROCKET` has `range=450`.

Two consequences worth writing down:

* **A bomber ordered onto an enemy aircraft gets no mission at all** — the
  `dropped` path skips `AIRSTRIKE` at `0x43F2AA`, then falls through
  `0x43F2CB` → `0x43F2F0` → `0x43F31B` and bails to the default.
* **Attacking a bare ground position never produces `AIRTOGROUNDHOVER`.** That
  path (`0x43F17E`) chooses only between `AIRSTRIKE` and `AIRTOGROUND` and
  never looks at `hoverattack`. A Brawler told to attack empty ground flies the
  `AirToGround` strafing pattern, not the pendulum.

---

## 4. `AirStrike` — the bomber, `0x411F50`

`sub esp,0x1C`; flags at `[esp+0x2C]`, unit at `[esp+0x30]`, mission at `[esp+0x34]`.
`esi` = unit, `edi` = mission, `ebx` = flags.
7 states, jump table at `0x4126F0`:
`0x4120F6, 0x4121BA, 0x4122A5, 0x41252D, 0x412394, 0x41247F, 0x41253C`.

### Prologue, every tick

```
0x411F5A  if (flags & 0x1000A):                       ; the order became untenable
              if mission+0x4A != 0            -> return 5
              if !(unit+0x110 & 0x300000)     -> return 5
              append a new "VTOL_SEEKATTACK" mission carrying the same target
              unit and position (0x43AD10)    -> return 5   ; i.e. replace this one
0x411FEA  target = mission+0x16
          if target == 0 and (mission+0x42 & 0x200):  ; the target unit has died
              append VTOL_SEEKATTACK at the attacker's own position -> return 5
          if target != 0:
              mission+0x22 = target->position           ; refresh, every tick
0x412084  if mission+0x3E != 0:                         ; the maneuver leash
              d = hypot(unit.xInt - mission+0x2E, unit.zInt - mission+0x30)
              if d >= mission+0x3E            -> return 5   ; abandon the attack
0x4120E1  dispatch on mission+0x05
```

### State 0 — take off (`0x4120F6`)

* `if (unit+0x00 == 0)` → return 7 (flush the mission list).
* `if (!(def+0x241 & 0x800 /* canfly */))` → return 7.
* `0x438880("Attacking")`; `0x4898B0(unit, 3)` — mission takes all three weapons;
  if attached to a pad/transport, detach (`0x48AAC0`); `0x48B090(unit, 1, 1)`.
* `if ((mover+0x2E & 3) != 1)` → **return 1** immediately (already airborne).
* Otherwise: `0x43D210(mover; unit, 2)` — switch to flying mode; goal at the
  aircraft's **current position**, `0x44E6C0(goal, cruisealt/2)`, **no arrival
  tolerance**, so arrival means "within 0.5 horizontally and 1.0 vertically" —
  a vertical climb to half cruise altitude on the spot.
* `mission+0x06 |= 0xE0`; **return 1**.

### State 1 — set up the run (`0x4121BA`)

* `0x489800(unit, 3)` then `0x4898B0(unit, 0)` — weapons 1 and 2 free, **weapon 0
  (the bomb) reserved for the mission**.
* `d = hypot(target − unit)`. **If `d ≥ 0x1E00000` (480.0), return 1** with no
  goal — the mask stays 0, so state 2 runs on the very next tick.
* If `d < 480`: the aircraft is too close to line up. Waypoint =
  `unitPos + Dir(0x48A980(unitPos, targetPos)) · 2240.0` (`0x8C00000`), i.e.
  **2240 units along the unit→target line, measured from the aircraft** — it
  flies straight through the target and well out the other side.
  Arrival tolerance `0x3C0` = **960**. `mission+0x06 |= 0xE2`. **Return 1.**

### State 2 — reposition (`0x4122A5`)

* `d = hypot(target − unit)`; `θ = 0x48A980(unitPos, targetPos)`;
  `θ' = θ + rand(0x4000) − 0x2000` (**±45°, uniform**);
  waypoint = `unitPos + Dir(θ') · (d/2)`.
* `d/2` is `(d − sign) >> 1` on the raw 16.16 value, i.e. halved toward zero.
* Arrival tolerance `0x1E0` = **480**. `mission+0x06 = 0x100E8`. **Return 1.**

Replayed: with the target 1000 away, the waypoint lands 500 from the aircraft
and 500–737 from the target depending on the jitter — it closes to roughly half
the range on a randomly skewed line.

### State 3 — pass-through (`0x41252D`)

Literally `mov eax,1; ret` — no action, **return 1**. It exists so that the
loop can come back here from state 6 and re-enter state 4 on the next tick.

### State 4 — arm, and wait for the release point (`0x412394`)

```
412394  if (flags & 0xE0) return 1;              ; the goal completed -> release
4123A8  gravity  = [[gs+0x391E9] + 0xD3C]        ; the map's gravity, per second²
        cruisealt = (signed word)def+0x21C
        if (gravity == 0) return 7
        speed    = (signed word)[[unit+0]+0x22]  ; current speed, world units/tick
        lead     = ftol( sqrt(2·cruisealt / gravity) · 30.0 · speed )
                                                 ; 30.0 is the float at 0x4FCC50
        trigger  = 1 + attackrunlength + lead     ; word def+0x216
        goal     = 0x44E190(mission, targetUnit)  or  0x44E2D0(mission, &targetPos)
        0x44E730(goal, trigger)                   ; arrival tolerance = trigger
        0x4388D0(mission, goal)
        0x439E80(mission, 1)                      ; wake again next tick
        mission+0x06 |= 0x100E8                   ; (plus bit 0 from 0x439E80)
        return 2                                  ; stay in state 4
```

`sqrt(2h/g)` is the ballistic fall time **in seconds**; `× 30` converts it to
ticks; `× speed` gives the horizontal distance a released bomb travels while it
falls. So the state re-computes the release distance **every tick from the
current speed**, hands the goal that distance as its arrival tolerance, and
advances the moment the aircraft is that close.

Worked, ARMTHUND (`cruisealt=200`, `attackrunlength=120`), gravity 112,
speed 9: fall time 1.890 s = 56.7 ticks, lead 510, **trigger = 631 world units**.
At half speed (4) the trigger falls to 347.

### State 5 — bomb (`0x41247F`)

* `0x4898B0(unit, 0)` — take weapon 0 back under mission control.
* **`0x48A0A0(unit, &mission+0x22, 0)`** — point weapon 0 at the target ground
  position. **This is the release order**; the weapon fires on its own schedule
  from here.
* `θ = 0x48A980(unitPos, targetPos)`; `r = (attackrunlength + 960) << 16`;
  waypoint = **`unitPos + Dir(θ)·r`** — the base point is the aircraft's own
  position, read at `0x412494` (`lea ebp,[esi+0x6A]`) and consumed at
  `0x4124D3`. **This resolves the prompt's open question.**
* Arrival tolerance `0x3C0` = **960**. `mission+0x06 = 0xE2`. **Return 1.**

The waypoint sits `attackrunlength + 960` ahead and the goal is satisfied at 960,
so the goal completes after the aircraft has flown exactly **`attackrunlength`
world units forward from the release point**. That is what the field means: how
far the bomber holds its line after dropping before it is allowed to break off.
Reading the base point as the *target's* position instead makes the run length
depend on the release range, which is not what the name says and is not what the
instructions do.

### State 6 — egress, and the decision to go home (`0x41253C`)

* `0x48A0F0(unit, 0)` — clear weapon 0's target, stop bombing.
* `h = word[unit+0x66]`; waypoint = `unitPos + Dir(h) · 1440.0` (`0x5A00000`) —
  **1440 units straight ahead along the nose**. Arrival tolerance `0x80` = **128**.
  `mission+0x06 = 0xE2`.
* Then: `if (word[unit+0x108] < ((dword[def+0x1FA] >> 2) · 3))` — i.e. **health
  below 75 % of `MaxDamage`** — search for the player's own active air repair
  pads within `0xF00` = **3840** units (`0x40B530`), and if any are found, cancel
  the move goal, pick one with `rand(n)`, push a **`VTOL_LANDING`** mission to
  the front of the list (`0x43ACB0`), zero `mission+0x06` and **return 0** (the
  attack mission restarts at state 0 once the landing mission finishes).
* Otherwise `mission+0x05 = 3` and **return 2**.

### The loop

```
0 take off  ->  1 line up  ->  2 close to half range  ->  3  ->  4 wait for release point
                                                          ^                     |
                                                          |                     v
                                              6 egress 1440 <-------------- 5 bomb
```

The run repeats 3 → 4 → 5 → 6 → 3 indefinitely, ending only when the leash
trips (return 5), the target dies, health drops below 75 % and a pad is in
range, or the order is replaced.

---

## 5. `AirToGround` — the strafing pass, `0x412710`

`esi` = unit, `edi` = mission, flags at `[esp+0x38]`.
At `0x41271F`, **`ebx = [[unit+0x10] + 0xDC]` = weapon 0's `range` in world units**
(`unit+0x10` is weapon slot 0's definition — confirmed independently at
`0x49ABCB`, where `[unit + 28·slot + 0x10]` is the weapon def; `+0xDC` is the
`range` key, confirmed in the parser at `0x42E512`/`0x42E52A`). Call it **R**.
Prologue and leash are identical to `AirStrike`, except the flag test is
`flags & 0x1000A` and the special case at `0x412828` (see §10).
6 states, jump table at `0x412D1C`:
`0x4128B5, 0x412977, 0x412A4B, 0x412AB4, 0x412B3F, 0x412CF9`.

| State | Address | What it does | Tolerance | Mask | Ret |
|---|---|---|---|---|---|
| 0 | `0x4128B5` | Identical to `AirStrike` state 0: announce, `0x4898B0(3)`, detach, and if landed climb to `cruisealt/2` on the spot | none | `\|= 0xE0` | 1 |
| 1 | `0x412977` | `0x489800(unit,3)` weapons free; `θ = 0x48A980(unitPos, mission+0x22)`; `θ' = θ + rand(0x4000) − 0x2000`; waypoint = `unitPos + Dir(θ')·(d/2)` | `0x80` = **128** | `= 0x100E8` | 1 |
| 2 | `0x412A4B` | `0x4898B0(unit,0)`; **aim weapon 0** — `0x48A060(unit, target, 0)` if there is a target unit, else `0x48A0A0(unit, &mission+0x22, 0)`; goal **at the target position** | **`ebx` = R** | `= 0x100E8` | 1 |
| 3 | `0x412AB4` | `θ = atan2(unit.x − target.x, unit.z − target.z)` (inline at `0x412AC6`); waypoint = `targetPos + Dir(θ)·3R` — **overshoot to 3× weapon range past the target** | `rand(128) + 128` = **128–255** | `= 0x100EA` | 1 |
| 4 | `0x412B3F` | Health check (as `AirStrike` state 6): below 75 % → `VTOL_LANDING`, return 0. Otherwise `s = rand(2)`; `a = heading ± 0x4000`; waypoint = `unitPos + Dir(a)·R` — **a 90° break, left or right at random, one weapon-range long** | `0x80` = **128** | `= 0x100EA` | 1 |
| 5 | `0x412CF9` | `mission+0x05 = 2` | — | — | 2 |

So the cycle is `0 → 1 → 2 → 3 → 4 → 5(→2) → 3 → 4 → 5 → …`: aim, fly at the
target until inside weapon range, overshoot to 3R on the far side, break 90°,
come round and do it again. Replayed with R = 370 against a target 1000 away:
state 2's goal completes at 370 from the target, state 3's waypoint is 1110
beyond it, state 4's is 370 abeam.

Note that the aim set in state 2 is **never cleared** by this handler — the
weapon holds the target through states 3 and 4 and keeps firing whenever it is
in range. Only the service loop's `flags & 0x10000` path clears it.

---

## 6. `AirToGroundHover` — the gunship, `0x413470`

`sub esp,0x34`; **`esi` = mission, `edi` = unit** (the opposite of the other two
handlers). Flags read at `0x413470` from `[esp+0xC]`, tested against `0x10008`.
At `0x4136C7`, **`ebx = [[unit+0x10] + 0xDC]` = R**, weapon 0's range.
4 states, jump table at `0x413BA8`:
`0x4136E5, 0x4137A7, 0x4138A0, 0x413902`.

Same prologue: replace with `VTOL_SEEKATTACK` on `flags & 0x10008`; refresh
`mission+0x22` from the live target every tick; abandon (return 5) if the
maneuver leash trips at `0x413670`.

### State 0 — take off (`0x4136E5`)

Byte-for-byte the same as the other two: `canfly` check, `"Attacking"`,
`0x4898B0(unit, 3)`, detach, `0x48B090(1,1)`, and if `(mover+0x2E & 3) == 1`
switch to flying mode and climb to `cruisealt/2` in place. `|= 0xE0`, return 1.

### State 1 — close to half range (`0x4137A7`)

* `0x489800(unit, 3)` — weapons free while repositioning.
* `d = hypot(targetUnit.pos − unitPos)` (the **live** target unit position, not
  `mission+0x22`); `θ = 0x48A980(unitPos, targetUnit.pos)`;
  `θ' = θ + rand(0x4000) − 0x2000` (**±45°**);
  waypoint = `unitPos + Dir(θ')·(d/2)`.
* Arrival tolerance `0x80` = **128**. `mission+0x06 = 0x100E8`. Return 1.

### State 2 — take the target (`0x4138A0`)

* `0x4898B0(unit, 0)`; **`0x48A060(unit, targetUnit, 0)`** — weapon 0 locks onto
  the target unit and holds it for the rest of the mission.
* Goal at the target unit's position, arrival tolerance **`ebx` = R**, so the
  goal completes while still one full weapon range away — the gunship never
  actually flies to the target.
* `mission+0x06 = 0x100E8`; **`mission+0x36 = 0`** (swing side) and
  **`mission+0x3A = 0`** (miss counter). Return 1.

### State 3 — the pendulum (`0x413902`)

```
413902  inRange = 0x49ABB0(unit, targetUnit, 0)      ; dx²+dz² <= range²
41390D  if (!inRange) mission+0x3A++
413914  if (mission+0x3A >= 2)  -> reroll  (0x41391E)
                                else       -> swing  (0x4139AD)
```

**Swing (`0x4139AD`) — the behaviour the Brawler and Rapier are recognised by:**

```
4139B8  θ = 0x48A980(unitPos, targetUnit.pos)     ; the unit's current bearing off the target
4139BD  if (mission+0x36 != 0) { θ += 0x2000 ; mission+0x36 = 0 }   ; +45°
4139D0  else                   { θ -= 0x2000 ; mission+0x36 = 1 }   ; -45°
4139E0  r = ((2·R) / 3)                            ; signed, rounded toward zero
4139F7  r <<= 16
4139FA  waypoint = targetPos - Dir(θ)·r            ; == targetPos + polar(θ)·r
413A52  goal = 0x44E330(mission, targetUnit, &waypoint)
413A5D  0x44E730(goal, 0x10)                       ; arrival tolerance 16
413A76  0x44E6C0(goal, cruisealt)                  ; full cruise altitude
413A7E  0x4388D0(mission, goal)
413A83  mission+0x06 = 0x100E8
413A8A  health check -> VTOL_LANDING (return 0) or return 2
```

Geometry, verified by replay with ARMBRAWL numbers (R = 370, `cruisealt` 60):

* The station is on a **circle of radius `(2·R)/3` centred on the target** —
  246 world units for a Brawler, 300 for a Rapier (R = 450).
* Each pass moves it **exactly ±45° around that circle**, alternating side on
  every arrival. `mission+0x36` starts at 0 (set in state 2), so the **first
  swing is −45°**.
* Because the new bearing is measured afresh each pass, the machine settles into
  a two-point oscillation: it shuttles between two stations **45° apart** on the
  ring, a chord of `2·r·sin(22.5°)` = **188 world units** for a Brawler. It
  swings, it does not orbit.
* The arrival tolerance is **16**, so it really flies to each station.
* **The swing is not randomised.** The `rand(0x4000) − 0x2000` pattern seen in
  state 1 (and in `AirStrike` state 2 / `AirToGround` state 1) is *not* used
  here: `+0x2000` / `−0x2000` are hard-coded immediates at `0x4139C2` and
  `0x4139D0`. The only randomness in state 3 is the reroll below.
* **Return 2 — the state never advances.** State 3 is the terminal state, so
  the gunship pendulums indefinitely.

**Reroll (`0x41391E`) — what happens when it cannot get a shot:**

```
41391E  mission+0x3A = 0
413926  r = R << 16
413929  φ = rand(0x10000)                          ; a uniform full-circle bearing
413932  waypoint = targetPos + Dir(φ)·R
413987  0x44E730(goal, 0x80)                       ; tolerance 128
413993  mission+0x06 |= 0x110E8
41399E  return 2
```

After **two arrivals at which the target was out of weapon range**, it jumps to
a uniformly random bearing at **exactly** weapon range and starts the pendulum
again from there. (The goal here is a plain `0x44E2D0` absolute-position goal;
no altitude call, so it keeps whatever altitude the previous goal set.)

**What stops it closing in.** Three things, all explicit:

1. State 2's goal is at the target but with an arrival tolerance of the **full
   weapon range**, so the approach terminates R away.
2. State 3 only ever commands points at **`(2·R)/3` from the target** (or R on a
   reroll) — the closest point the mission ever asks for is two-thirds of a
   weapon range.
3. `0x44E330`'s flag word is `0xA3`, which has bit 7 set, and the goal resolver
   at `0x44E3D5` skips re-derivation when bit 7 is set. The station is therefore
   a **fixed world point** computed once per pass, not a point that slides with
   the target; the gunship re-aims only when it arrives.

And the whole thing is bounded by the maneuver leash (§8).

---

## 7. `AirToAir` — `0x412D40`

Two states, dispatched by a `cmp` chain at `0x412FA0` rather than a jump table.
State 0 (`0x413397`) is the same take-off/announce block. State 1 (`0x412FB2`)
sets `0x489800(unit,3)` then `0x4898B0(unit,0)` and `0x48A060(unit, target, 0)`,
and pushes a **`VTOL_EVADE`** mission (name at `0x501CAC`) when it needs to
break off (`0x413352`). It shares the same leash, and its own off-map handling
is the gunship's rather than `AirToGround`'s.

**Corrected, September 2026.** This section used to say the mission "flies
short hops (`0x140000` = 20.0 units) around the target". It does not. That
constant is the length of two probe vectors whose dot product asks whether the
target lies in the forward half-plane, and nothing in the mission ever moves
twenty units. What it actually does — lead the target by forty-five ticks of
his own velocity, extend when it overshoots, break off when he ends up behind
— is decoded in full in `TOTALA-EXE.md` §90, along with the goal object that
advances itself and the two calls in it that turn out to be stubs.

---

## 8. `maneuverleashlength` — `def+0x214`

Read at three sites; all three are decoded here.

**`0x43B311` and `0x43B60F`** are the two mission-creation paths (order
issue / order queue). Both do the same three things:

```
43b309  ecx = unit->def
43b311  dx  = word [ecx+0x214]            ; maneuverleashlength
43b31C  push edx ...                      ; -> the 6th argument of 0x43A0C0
43b327  call 0x43A0C0                     ; Mission::Mission(...)
43b330  mission+0x2E = word [unit+0x6C]   ; the unit's integer X, right now
43b338  mission+0x30 = word [unit+0x74]   ; the unit's integer Z, right now
```

and `0x43A11C` stores that sixth argument into **`mission+0x3E`**.

Every attack handler then opens with (`0x412084`, `0x41284E`, `0x413675`):

```
if (mission+0x3E != 0) {
    d = hypot(unit.zInt - mission+0x30, unit.xInt - mission+0x2E);
    if (d >= mission+0x3E) return 5;      ; delete the mission
}
```

**So `maneuverleashlength` is not a radius around the target and not the radius
the gunship holds.** It is a radius around **the spot the aircraft was standing
when the order was given**, and leaving it abandons the attack mission outright.
Every aircraft in the shipped data has `maneuverleashlength=1280`, so an
aircraft will chase a target 1280 world units from where it started and no
further. That, not any distance term in the pendulum, is what stops a Brawler
following a fleeing target across the map.

The third site, **`0x4393A9`**, is unrelated to behaviour: it is the order-cursor
overlay, drawing a labelled ring of that radius around the unit — the label
string is `"maneuver"` at `0x505144`, sitting next to `"build distance"` at
`0x505150` which is drawn the same way from `builddistance` (`def+0x212`) at
`0x439373`.

---

## 9. What `hoverattack` actually gates

`hoverattack` is **bit 27 (`0x08000000`) of `def+0x241`**, parsed at `0x42C823`
with `shl eax,0x1b` at `0x42C83C`.

A search of the whole `.text` for that constant finds **exactly three sites**:

| Address | What |
|---|---|
| `0x42BA5B` | the unitdef **copy** routine — `xor`/`and`/`xor` bit-copy, not a behaviour read |
| **`0x43F2FA`** | `test ecx,0x8000000; jne 0x43F31B` — if set, do **not** take the `AIRTOGROUND` exit |
| **`0x43F323`** | `test ecx,0x8000000; je default` — the `AIRTOGROUNDHOVER` exit requires it |

The two live sites are the two halves of one decision. Reached only when the
attacker `canfly`, its weapon 1 is **not** `dropped`, and the target is a unit
that is **not** an aircraft:

```
hoverattack == 0  ->  AIRTOGROUND        (0x412710)
hoverattack == 1  ->  AIRTOGROUNDHOVER   (0x413470)
```

Nothing else in the binary reads the bit. In particular it does **not** affect:

* attacking a ground **position** (that path, `0x43F17E`, never consults it — a
  Brawler told to attack empty ground flies `AirToGround`);
* attacking an aircraft (`AIRTOAIR` is chosen before the bit is tested);
* the flight model, the weapon code, or anything inside the mission handlers.

`ARMBRAWL` and `CORAPE` are the only OTA units that set it.

---

## 10. Constants

| Value | World units / meaning | Address |
|---|---|---|
| `0x1E00000` | **480** — `AirStrike` state 1: above this range, skip the fly-through | `0x41220B` |
| `0x8C00000` | **2240** — `AirStrike` state 1 fly-through length, from the aircraft | `0x41221F`, `0x412231` |
| `0x3C0` | **960** — `AirStrike` state 1 and state 5 arrival tolerance | `0x41227A`, `0x412512` |
| `0x1E0` | **480** — `AirStrike` state 2 arrival tolerance | `0x41236A` |
| `0x4000` | **±45°** random jitter span (`rand(0x4000) − 0x2000`) in all three "close to half range" states | `0x4122ED`, `0x4129CF`, `0x413806` |
| `0x2000` | **45°** — the gunship's fixed pendulum half-swing | `0x4139C2`, `0x4139D0` |
| `0x10000` | **full circle** — the gunship's reroll bearing, `rand(0x10000)` | `0x41391E` |
| `30.0f` | **ticks per second**, converting bomb fall time to ticks | float at `0x4FCC50`, used `0x4123EC` |
| `+1` | one extra world unit on the release trigger | `0x412402` |
| `attackrunlength + 960` | `AirStrike` state 5 waypoint distance ahead of the aircraft | `0x4124AA`–`0x4124B7` |
| `0x5A00000` | **1440** — `AirStrike` state 6 egress, straight ahead | `0x412548`, `0x41255A` |
| `0x80` | **128** — `AirStrike` state 6, `AirToGround` states 1 and 4, gunship states 1 and reroll | `0x4125A7`, `0x412A44`, `0x412CCF`, `0x413876`, `0x413987` |
| `rand(0x80) + 0x80` | **128–255** — `AirToGround` state 3 arrival tolerance | `0x412B2A`–`0x412B34` |
| `3 × R` | `AirToGround` state 3 overshoot, R = weapon 0 `range` | `0x412ACB` |
| `1 × R` | `AirToGround` state 4 break length; gunship state 2 tolerance and reroll radius | `0x412C70`, `0x4138D6`, `0x413926` |
| `(2 × R) / 3` | **the gunship's pendulum radius** — 246 for the Brawler, 300 for the Rapier | `0x4139E0`–`0x4139F7` |
| `0x10` | **16** — the gunship's pendulum arrival tolerance | `0x413A5D` |
| `2` | consecutive out-of-range arrivals before the gunship rerolls | `0x413914` |
| `cruisealt / 2` | the take-off climb height in every state 0 | `0x412193`, `0x41294E`, `0x413780` |
| `cruisealt` | the gunship's pendulum altitude | `0x413A6C` |
| `0x1FF0000` | **511** — hard ceiling on a goal's altitude above ground | `0x44E702` |
| `(maxdamage >> 2) × 3` | **75 % health** — the go-home threshold | `0x4125D5`, `0x412B52`, `0x413A9D` |
| `0xF00` | **3840** — the repair-pad search radius | `0x41260C`, `0x412B8A`, `0x413AD5` |
| `0x3200000` | **800** — the distance an off-map aircraft heads back toward the map centre | `0x4135CF`, `0x40F32B` |
| `0x1E` | **30 ticks** — the delay in `AirToGround`'s off-map special case | `0x41283C` |
| `0.5f` | the default arrival radius when no tolerance is set | float at `0x4FD3EC`, used `0x44E631` |
| `1/65536` | converts the raw 16.16 separation to world units before the tolerance test | float at `0x4FD3E8`, used `0x44E602` |
| `1280` | `maneuverleashlength` on every OTA aircraft | game data |
| `1280` | `range` on all four bombs; `370` `VTOL_EMG`; `450` `VTOL_ROCKET`; `659`/`650` the advanced VTOL missiles | game data |

Relevant FBI values, for reference:

| Unit | MaxVelocity | TurnRate | cruisealt | attackrunlength | leash | Weapon1 (range) |
|---|---|---|---|---|---|---|
| ARMTHUND | 9 | 356 | 200 | 120 | 1280 | ARMBOMB (1280, dropped) |
| ARMPNIX | 9.5 | 365 | 220 | 180 | 1280 | ARMADVBOMB (1280, dropped) |
| CORSHAD | 8 | 335 | 205 | 220 | 1280 | CORBOMB (1280, dropped) |
| CORHURC | 9.1 | 256 | 160 | 290 | 1280 | CORADVBOMB (1280, dropped) |
| ARMBRAWL | 6.6 | 800 | 60 | — | 1280 | VTOL_EMG (370), **HoverAttack=1** |
| CORAPE | 7.63 | 600 | 50 | — | 1280 | VTOL_ROCKET (450), **HoverAttack=1** |

---

## 11. Not confirmed

* **Exactly when the bomb leaves the aircraft.** The mission's part is fully
  decoded: state 5 hands weapon 0 a ground aim point (`0x48A0A0` at `0x41248F`)
  and state 6 takes it away again (`0x48A0F0` at `0x41253F`). The bombs
  therefore fall during the state-5 run, whose length is `attackrunlength`. The
  actual trigger inside the weapon update (reload, aim convergence) was not
  traced. What *is* certain is that the release **range** is set by the mission,
  not the weapon: the bomb's `range` is 1280 for all four bombers, while the
  mission withholds the target until `1 + attackrunlength + falltime·speed`
  (631 for a Thunder), so the mission is what decides where the run begins.
* **The off-map special case.** `[unit+0x82]` is the unit's spatial-hash bucket
  (`0x47CBB9` writes it when a unit is linked into a bucket, chained through
  `unit+0x8E`), and `[gs+0x142B7]` is a single bucket object created once during
  map setup at `0x482C4E` with a sentinel field `[+2] = 0x1F`. When they match,
  `VTOL_LandIfCan` (`0x40F2E9`), `AirToGroundHover` (`0x41358D`) and the goal
  resolver (`0x44E3F4`) all treat the unit specially, and the gunship sends it
  800 world units back toward the map centre (`[gs+0x1422B]/2`,
  `[gs+0x1422F]/2` — the map dimensions, used the same way at `0x40820E`).
  Reading this as "the unit is off the playable map" fits everything except
  `AirToGround`'s response at `0x412828`, which merely forces state 2 with a
  30-tick timer. **[inferred]**
* **The exact meaning of the individual wake-mask bits.** Confirmed: bit 0 is
  the timer (`0x439E80`), bits 5–9 are move-goal outcomes (`0x4388D0` clears
  exactly `0x3E0` when installing a new goal, and the handlers advance on
  `flags & 0xE0`), bit 16 makes the service loop clear all weapon targets before
  the call (`0x43B840`). Bits 1, 3 and 12 come from `unit+0xBA` and were not
  traced to their setters; the handlers use them only through the composite
  masks `0x1000A` / `0x10008` (replace the mission with `VTOL_SEEKATTACK`) and
  the values `0xE0`, `0xE2`, `0x100E8`, `0x100EA`, `0x110E8`.
* `def+0x1F2` and `def+0x1F6` as weapon 2 and 3's definitions — only `+0x1EE`
  (weapon 1) was actually read out of the binary. **[inferred]**
* `0x48AAC0(unit, 0, -1, 2)` named as "detach from carrier/pad". **[inferred]**
  from its guard on `unit+0x86` and that field's role in `0x47CB73`.
