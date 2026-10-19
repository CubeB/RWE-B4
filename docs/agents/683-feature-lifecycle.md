# #683: shared stateful-feature capacity and sprite lifecycle

Status: implemented and validated locally. Release game built successfully;
see feature-batch-local-build.md for the playable artifact and validation.
Owner-approved scheduling is in the continuation brief; the owner requested a
finished local playable build on 2026-10-19. No unrelated backlog work belongs
in this finishing batch.

Authority: BT-48, BT-255, BT-257 and supplements BT-261 (`fa9d1362`) / BT-262
(`856f24d3`) / BT-263 (`5f259e67`) in the separate documentation checkout. Reference byte-tactics
`97d7ac59b460417bcdfa629cf90da99f31818c65`, retail GOG v3.1. These are static
findings; no original runtime capacity, sound or animation check was executed.

Required normal transitions:

- One shared limit of2048 includes every model (resting too), burning sprite,
  and death/reclaim sprite. Ordinary shared sprite animation consumes none.
- Placement clears ordinary occupants in row-major order before checking a
  model's allocation. Normal removal refuses indestructible features, including
  during clearing. Earlier successful removals are not rolled back on refusal.
- Replacement releases its previous state before acquiring successor state.
  Model-to-model replacement can succeed at capacity. A slotless sprite replaced
  by a model can disappear with no successor if capacity is exhausted.
- A selected death/reclaim animation needs capacity before starting. Failure
  keeps the original sprite and does not commit threshold-crossing damage.
  A model or sprite without that sequence replaces directly.
- Primary GAF animation duration controls expiry and immediate replacement;
  shadow completion does not delay it. Newest-first snapshots prevent same-pass
  stepping of animations newly created by spread or replacement.
- Reclaim credit precedes the transition attempt, including when a required
  reclaim animation cannot be allocated. Preserve the existing RWE work/payout
  calculation in this capacity patch; do not redesign reclaim economics.
- Active sprite states reject further damage and ignition. All three states
  skip blast-history accounting under the already-approved BT-257 policy.
- Starting an animation preserves its definition, continuation cells and
  blocking until actual removal/replacement. It does not free collision early.

Presentation completion for #681:

- Emit one treeburn request only after successful ignition. Resolve the shipped
  ALLSOUND TREEBURN entry (`sound=burn02`) through the existing effects system.
  Known X/Z is the origin cell corner. BT-262 identifies uninitialized original
  Y; existing RWE positional/fog shortcomings remain #583. No exact original
  sound-position/audibility parity is claimed.
- Rendering reads the current simulation age; it adds no compulsory first-frame
  render or lifetime delay. Per-instance death/reclaim sequences must replace the
  former detached reclaim particle so their lifetime and pool occupancy agree.
- Lifecycle shadows use only the explicit burn/death/reclaim shadow keys. No
  normal-shadow fallback applies. A shorter shadow disappears at its own end;
  a longer one is cut off by primary expiry. Installed data has eight burn
  shadows and no death/reclaim shadows, as documented by BT-263.

Shipped data fixtures: Tree1's burn/death/reclaim sequences last192/60/30 updates
(TREES.GAF, totala1.hpi). DragonsTeeth is a2x2 model with4000 HP and RockTeeth
is its2x2 sprite successor with2000 HP (features/all worlds/dragonsteeth.tdf).
Test positions, cap boundaries and reduced victim HP are controlled conditions.

Additional shipped shadow fixture, independently extracted from the installed
archives with tools/hpi.py: MarsPlant01 uses footprint2x2, blocking1, energy50,
sparktime5, spreadchance90 and TreeBurn. MARSPLANTS.GAF has plant01flame46x2
(92 updates), plant01shad1x10 (10 updates), plant01boom18x2 (36) and plant01rec
12x2 (24). Its burn shadow therefore disappears while its primary continues;
the ordinary shadow must not return during the remainder of the burn.

Mutable animation state must be saved, hashed and dumped. Save/replay version3
is the new format for this still-local batch. Existing earlier nonburning saves
remain accepted; older command replays and legacy active-fire saves cannot
reproduce the new rules and are explicitly rejected.

Capacity is recomputed from feature definitions plus saved animation state,
rather than maintained as another mutable counter. It is therefore preserved
by save/load and hash/dump without redundant state that could drift. The shared
counter/order names are now animationTicksRemaining and animationOrder, covering
all three sprite states; animation identifies burn/death/reclaim. Legacy
over-cap snapshots restored through the existing slot-restoration API are not
silently truncated; normal new allocations refuse while occupancy is at the cap.

Implementation-room limitations remain explicit: RWE's existing work-sliced
reclaim payout arithmetic is preserved, including no repeat payout from an
already-complete work counter after allocation refusal. BT-261's whole original
reclaim economics are not newly claimed by this cap change. Existing unit-death
and blast interleaving gaps are BT-260; positional/fog audio is #583. Owner visual
checks have not been run and original runtime parity has not been measured.

Validation completed:

- Debug game, test executable, all Debug targets and Release game built.
- Focused selection: 74 cases / 12,911 assertions.
- Full permitted suite: 1,826 cases / 193,627 assertions, plus five permitted
  packet/lobby checksum cases / 33 assertions. Total 1,831 distinct cases /
  193,660 assertions. Neither TaChecksums case ran.
- Complete section scan: 1,053 objects, peak 24,276 against 24,575.
- Two fresh seed-7, 180-second Coast To Coast RWE games produced byte-identical
  5,399-tick hash logs. SHA-256:
  d0ba8a9b5afe4f849f5326710e740e9c7ebaa6e17f80818d909af38945076ca8.
  Both loaded 120 shipped features with the new saved animation fields.

An additional real-data RWE command replay drove ARMCOM's shipped D-gun at
Palm02 on Coast To Coast. Independently read ARCHIPELAGO.GAF palm02flames has
100 frames x 2 updates = 200. The loaded simulation ignited it at tick 134,
showed 199 remaining updates after that tick's feature pass, counted down once
per update, spent its 139-update spread timer once at tick 272, and replaced it
at tick 333. Counter and successor checks passed for every captured tick.
Replay, fixture, tick states and result are in D:\tmp\rwe-feature-final-fire.
This is executed RWE with shipped data, not an executed original-game oracle.
No owner visual check was performed.
