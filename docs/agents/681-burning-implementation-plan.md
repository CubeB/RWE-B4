# #681: burning-feature implementation preparation

Status: metadata step committed as af43acd6. The working copy now implements
the core duration/spread/damage correction and is building for focused tests;
the core has passed focused validation, and #681 is not complete.
Owner-approved scheduling is in the continuation brief.

Authority: BT-44, BT-49 and BT-255 in the documentation checkout
`D:\RWE-spec-room`. BT-255 was committed as `b4b094fe`. All are static readings
of byte-tactics `97d7ac59b460417bcdfa629cf90da99f31818c65`, retail GOG v3.1.
No original-game run or demo oracle was executed. BT-255 raises the frame-timing
and wind-unit rules to high static confidence.

Required behavior:

- Read the unsigned low word of each GAF frame-table second dword; each frame
  lasts max(1, word) feature updates. A play-once burn replaces the feature at
  its end. Ignore burnmin/burnmax for behavior.
- At ignition use half of the 16-bit spark-time tick count plus a draw below
  that half, stored as a byte. A bound below two draws nothing. Zero timer
  never spreads. End animation before spending the timer, so equal expiration
  means burnout without spread or damage.
- Snapshot active fires newest first. New ignitions take effect immediately
  for eligibility but receive their first update next tick if started by spread.
  A projectile ignition before the pass receives its first update that tick.
- Try origin cells in the seven-by-seven square, row-major, excluding the source.
  Each eligible candidate uses its own spread chance. Follow with five signed
  fixed-point wind samples, testing only cell transitions. A failed square roll
  may be retried by wind; a successful ignition cannot.
- Apply the named burn weapon's area damage exactly once, at spread, without an
  attacker and with the nobody owner. Shipped burn weapons are unitsonly.
- BT-259 (`db7bba8d`) clarifies blast ignition: parse firestarter as a decimal
  leading integer narrowed to a byte, not a clamped floating percentage. A
  nonzero byte makes an eligible ignition attempt without an extra chance roll.
  Failed ignition for a missing sequence or full pool still replaces ordinary
  feature damage and consumes no timer draw. The blast's continuation redirect
  can supply a normalized origin even when that origin cell itself was not in
  reach. Spread's direct candidate walk still accepts origin cells only.
- Reject model features and other per-instance animation state; a normal shared
  animated sprite alone does not prevent ignition. The 2048-slot allocation
  dependency is covered by BT-48 / #683.

Shipped data inspected with tools/hpi.py from totala1.hpi:

- features/green/TREES.TDF Tree1: footprint 1x1, height 40, sparktime 5,
  spreadchance 90, burnweapon TreeBurn, burnt form Tree1Dead; burn sequence
  Leafyburn01 in anims/TREES.GAF.
- Leafyburn01 has 96 frames, each timing word 2: 192 feature updates. Tree1's
  burnmin 5 / burnmax 15 must have no effect. Tree2/Tree3 and treeburn1/2/3 also
  have 96 frames of two ticks. shruburn has 42 frames of two ticks (84 total).
- TREES.GAF SHA-256:
  `93c7955d24af8c18ca07d0c519061d1cc115e86fec1b4d2a68da30451b2e37b4`.

A further read-only metadata census used the installed totala1.hpi, worlds.hpi,
ccdata.ccx, btdata.ccx and rev31.gp3, retaining feature section names from the
resolved file set. All 79 flammable definitions naming a burn sequence resolved
to GAF timing; their lengths span 46..282 updates and all declare sparktime 5.
This is a separate content snapshot and simple section-name census, not a claim
that it is identical to BT-44's 78-definition population or to the game's exact
duplicate-definition resolution. Scratch report:
`D:\tmp\rwe-681-timing-census.json`.

Resolved by BT-258, spec-room commit `90a729bd`: an independent census matches
all 79 names. A comment between Tree7's header and opening brace causes a strict
whitespace-only header census to omit Tree7 alone and return 78. The original
accepts this valid commented record. BT-44/49 now carry correction notices; use
79 named definitions for this archive population. The discarded old census
script was not retained, so attributing its exact defect remains inference.
Tree7 shares Tree6's 192-update timing; duration ranges and spark conclusions
are unchanged. No original runtime population was measured.

Metadata implementation step (in progress): GafArchive::Entry exposes unsigned
per-frame update counts; FeatureDefinition retains burnFrameTicks; the loader
reads the named sprite sequence directly through the VFS/GAF metadata reader
without using TextureService. Model definitions do not acquire sprite burn
timings. No mutable sim state or RNG draw changes in this step. Tests cover the
shipped Tree1 burn/death/reclaim frame counts and low-word/minimum-one boundaries.
The Debug game and focused [gaf],[burning] build/run script is
`D:\tmp\rwe-681-timing-build.sh`; logs share the rwe-681-timing prefix.
This step alone does not correct the fire behavior or complete the issue.

Metadata-step validation: Debug game build and focused [gaf],[burning] tests
passed (3 cases, 21 assertions), including after moving the VFS read inside
the malformed/unreadable-animation exception boundary. GameScene.cpp remains
24276 sections. Full-suite/all-target/Release validation will follow the actual
fire-rule change; the Release executable still contains the completed #687
behavior. No original-game execution was performed.

Implementation considerations still to resolve in code review:

- Load timings from GAF metadata even with no texture service, so graphical and
  headless simulations receive identical definitions. Do not derive sim timing
  from uploaded textures or presentation time.
- Preserve newest-first ignition order explicitly through save, load, hash and
  dumps. The existing generic vector hash sums its entries and cannot distinguish
  a reordered active set without an order-sensitive fold.
- Keep animation age/countdowns in sim state so same-tick and next-tick first
  updates behave correctly; presentation selects the corresponding play-once
  frame rather than looping on the global clock.
- BT-1 records RWE's existing broader tick-order divergence. Its wind selector
  currently runs before the feature pass. Do not silently reorder the whole
  simulation in this issue. Decide and document how spread reads the previous
  wind vector on change ticks; exact original RNG stream parity is not claimed.
- SimScalar is float; reconstruct signed fixed-point wind sampling carefully.
  BT-255 states that if W is the original particle wind in world units, the
  fire step is 32W world units. RWE's computeWindVector uses the same nominal
  scale, but its trig arithmetic differs from the integer table rounding.
- The current feature-damage sweep does not distinguish ignition from damage;
  verify the weapon ignition path as part of the change, rather than relying
  solely on direct igniteFeature tests.
- Verify kill credit separately from damage source. BT-49 requires a burn hit
  to leave last-attacker fields unchanged and apply no attacker bonus, while a
  lethal burn credits the previously recorded attacker, if any. Reusing a
  no-source blast alone is insufficient if the existing kill path credits only
  the current damage call's attacker. Keep this burn-specific correction scoped
  to the documented case, with a regression for both no-prior-attacker and
  prior-attacker outcomes.

Validation must cover shipped frame lengths, zero/equal/short timers, one spread,
candidate-specific rolls, square reach, continuation refusal, deferred cascades,
newest-first order, wind retry/direction, no-source damage and unchanged credit,
headless loading, save continuation, deterministic repeat hashes, and pool
exhaustion once #683 is integrated. Changes are hash-visible and break old replay
outcomes; account for the replay/version policy before claiming completion.

Core implementation in the working copy:

- The three fire methods moved to GameSimulation_fire.cpp to protect the dense
  GameSimulation.cpp section budget. Model presence is retained in definitions;
  ignition counts existing models and burns against the 2048-state limit. Model
  placement exhaustion and temporary death/reclaim states remain #683 work.
- MapFeature carries remaining animation updates, remaining spark updates and
  relative ignition order. Save/hash/dump include them. The simulation hash also
  associates ignition order with feature identity, detecting a swapped order.
- The renderer selects the play-once frame from sim age rather than global time.
  Spread uses the previous wind vector on a wind-change tick while preserving
  RWE's already-documented broader phase/RNG-order divergence (BT-1).
- Burn damage has no firing unit or attacker bonus; lethal burn damage uses the
  previous attacker for credit without updating last-hit fields.
- RWE-only format policy: save JSON version 3 carries the new counters. Older
  nonburning saves remain accepted; legacy active-fire state without counters is
  refused explicitly. RWE command replay version 3 rejects older command replays
  whose outcomes depended on the former fire rules. These formats have no
  original-game counterpart and are not original-parity claims.
- Blast reach and active-sprite hit-list accounting still use the pre-#682
  implementation; #682 now has owner approval for its deterministic policy.

First core build found an obsolete currentTime parameter name unused after
switching the renderer to sim age (-Werror). Its name was removed only after
make and all workers exited. The first focused run passed 55 of 57 cases. Two
fixture assumptions were corrected: grid placement puts the solar box 24 units
from the fire (53 damage), and byte-conversion input needs a named TDF block.
The second focused run passed: 57 cases, 4348 assertions, including GAF,
burning, save/load and replay cases. Debug game also built. Full permitted suite,
all targets, section budget, Release game and repeated headless hashes remain
pending for the combined feature batch. Ignition sound and the remaining
stateful sprite lifecycle are not yet implemented, so this is not a closing fix.
