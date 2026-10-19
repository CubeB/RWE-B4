# #681: burning-feature implementation preparation

Status: reference research and shipped-data inspection complete; implementation
has not started. Owner-approved scheduling is recorded in the continuation brief.

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

Validation must cover shipped frame lengths, zero/equal/short timers, one spread,
candidate-specific rolls, square reach, continuation refusal, deferred cascades,
newest-first order, wind retry/direction, no-source damage and unchanged credit,
headless loading, save continuation, deterministic repeat hashes, and pool
exhaustion once #683 is integrated. Changes are hash-visible and break old replay
outcomes; account for the replay/version policy before claiming completion.
