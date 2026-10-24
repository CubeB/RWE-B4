# Rocket-bot targeting and combat micro

## Targeting review

This is a code review, not confirmation of a particular replay. Targeting has
not been changed as part of this review.

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
incorrect. A follow-up visibility fix should retain remembered positions for
strategic movement while preventing hidden live-unit reads in tactical
decisions, initializing attack orders without leaking the live position, and
reviewing persistent weapon tracking separately.

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
