# Rocket-bot targeting and combat micro

## Original targeting review (before the fix)

This records the pre-fix behavior found during code review, not confirmation
of a particular replay. The follow-up changes are described below.

- `PerceptionManager::refresh` only records units passing `canSeeUnit`, unless
  `cheatModeOmniscient` is enabled. Brutal enables that option; Hard does not.
- Visibility belongs to the whole AI player, not to an individual rocket bot.
  Another unit spotting a target can legitimately enable a longer-range shot.
- `ArmyManager::nearestKnownEnemy` accepts remembered contacts for
  `targetMemoryTicks` (150 simulation ticks, or five seconds, by default).
  `contactStillStanding` preserves an unseen remembered contact without
  exposing its live unit pointer, so strategic scouting can revisit it.
- However, the nearest-enemy selector can still return that hidden unit's ID.
  `kiteBackFrom` then fetches the live unit and uses its current position and
  weapon range. This can make rocket bots react to a hidden unit's movement
  during the memory window. Similar live reads exist in close-range
  repositioning and stalled-attack checks.
- A newly issued attack also has an information leak:
  `UnitBehaviorService::resolveAttackTargetPosition` returns the live position
  when a hidden target has no stored `lastSeenPosition` for that order. An AI
  attack issued against a remembered contact can enter that path. Existing
  orders with a stored position instead use that remembered position for
  their approach/navigation.
- Weapon tracking is a separate issue affecting human units too: an existing
  `UnitWeaponStateAttacking` resolves its unit target through
  `getTargetPosition`/`tryGetSweetSpot`, without a fresh visibility check in
  that tracking path. Ground attack orders also assign the unit ID to the
  weapon. Remembered navigation therefore does not guarantee that aim/fire
  has stopped following a hidden unit's live position.

These are plausible explanations for the report, beyond legitimate spotting
by another unit. They do not establish that every out-of-sight rocket shot is
incorrect. The follow-up addresses tactical acquisition, remembered movement,
and persistent weapon tracking separately.

## Targeting fix

- Non-omniscient ground and air AI only choose tactical unit targets whose
  contact was refreshed by perception in the current simulation tick. Losing
  sight for even one tick prevents a new unit attack or live-state kiting
  decision. Remembered contacts remain available for strategic searches.
- An attack order navigates to its own last-seen point while its unit target
  is hidden, without reading the target's current position or movement type.
  It finishes that search when it reaches the remembered point. A new order
  with no remembered position waits for sight instead of revealing a location.
- Weapon tracking drops a unit target when its owner loses sight, before aim
  or burst firing can use the hidden unit. Restoring sight permits ordinary
  reacquisition. The rule applies equally to human and AI weapons.
- Unit-targeted D-gun orders also require sight before moving or aiming.
  Ground-targeted fire, interceptor projectile targets, and projectiles already
  launched retain their ordinary behavior.
- Brutal still deliberately knows enemy positions for planning. That setting
  does not bypass the simulation's sight requirement for firing at a unit.

The former five-second `targetMemoryTicks` setting remains relevant to builder
threat assessment; it no longer authorizes unit-targeted ground/air micro.
This intentionally tightens the former held-target behavior documented from
the original executable.

Fog regression tests cover one-tick sight loss, pending unseen orders,
remembered navigation and reacquisition, and stopping an AI weapon mid-burst.
Combat-mechanics arenas explicitly reveal the map to isolate flight, range,
water eligibility and formation mechanics from fog. Fog tests use real sight.

Validation: the Windows Release game and test executables built successfully.
The full native suite passed 1,930 cases and 211,792 assertions; three cases
requiring external retail game data were skipped. No Linux runtime or replay
of the reported match was available for this validation.

## Micro change

The RWE AI now shares a combat-order budget across its ground and air managers:
60 unit orders per rolling simulation minute on Easy, 120 on Medium/Standard,
and no added cap on Hard or Brutal. An initial/refill burst holds at most four
orders, rather than allowing the full minute's quota instantly.

Move, attack, patrol, guard and D-gun orders from those two managers count.
Each unit order counts individually, including queued patrol waypoints.
Economy, construction, repair, scouting and transport planning keep their
existing behavior. Autonomous movement and weapon firing are not AI commands
and are not throttled. The existing tactical decision cadence still applies
on Hard and Brutal.

Units not served recently receive priority, and queued orders for a unit stay
together. Unsent commands are discarded so the next tactical pass can generate
fresh decisions. The limiter uses simulation ticks and deterministic ordering.
