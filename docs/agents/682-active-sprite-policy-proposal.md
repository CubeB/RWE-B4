# #682: approved deterministic treatment of active sprite blasts

Status: owner approved on 2026-10-19 with the explicit reply "Use the
deterministic policy (recommended)". Implementation is complete locally for
burn, death and reclaim sprites. Final validation passed 1,831 permitted cases,
all Debug targets, the section scan, Release build and repeated shipped-data
hashes; see feature-batch-local-build.md. The
dedicated spec room recorded this decision in the existing section 88
register before the #682 code change, commit `9d39950e`. The same commit updates
BT-257, its discrepancy row and the spec-room compatibility summary. It has not
been cherry-picked into this checkout because it touches the protected register.

BT-257 (spec-room commit `68bf7709`) establishes that the original uses active
sprite animation bytes as a reach point, including a loaded GAF address as Y.
This is static evidence at byte-tactics `97d7ac59`, retail GOG v3.1; no executed
original check. Ordinary burning Tree1 is an affected shipped example.

The original never damages or reignites an already-active sprite. The bogus
point can only consume one entry of the 64-feature deduplication list. Once
that list is full, further identities stop being recorded and can be damaged
again through other cells. Active sprites' contribution can therefore affect
repeat damage in blasts containing more than 64 qualifying features. Their
contribution depends on process addresses and recycled animation bytes.

Approved policy: skip burning/dying/reclaiming sprites before reach and
hit-list accounting. Preserve their original immunity and reproduce the
documented ordinary-sprite and model reach rules from BT-47 / BT-256. Do not
emulate heap addresses or invent arbitrary coordinates in the lockstep sim.

Proposed protected section 88 entry:

> **Active sprite blast bookkeeping is deterministic (#682, BT-257).** A sprite
> playing a burn, death or reclaim animation takes no further blast damage,
> as in the original. RWE skips it before feature hit-list accounting. The
> original instead interprets animation bytes, including a GAF pointer, as a
> reach point and may consume a deduplication entry. RWE deliberately omits
> this address-dependent bookkeeping. Only overflow behavior in blasts with
> more than 64 qualifying features can differ; the ordinary reach and damage
> rules are otherwise unaffected by this policy.

Proposed compatibility entry:

> **Burning scenery does not consume blast bookkeeping slots.** Like the
> original, RWE does not damage scenery again while its individual burn, death
> or reclaim animation plays. RWE also leaves it out of blast hit accounting,
> avoiding a memory-address-dependent quirk that can change repeat damage in
> exceptionally crowded explosions.

Deterministic reproduction of actual process addresses is not a viable lockstep
implementation. The approved policy does not change the
existing 64-feature cap or authorize unrelated behavior changes.
