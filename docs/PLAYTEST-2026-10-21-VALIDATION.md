# Construction and visibility playtest investigation

Implementation-room record, 2026-10-21. Reference comparisons are in
`PLAYTEST-CONSTRUCTION-VISIBILITY-FINDINGS.md`,
`PLAYTEST-RECLAIM-ASSIST-FINDINGS.md` and
`PLAYTEST-ENVOY-RENDERING-FINDINGS.md`. Those are static byte-tactics checks
against the identified GOG 3.1 build, not executions of the original game.

## Changes

- CV-02: presentation now uses the simulation's any-projected-corner visibility
  predicate with the local presentation picture. This preserves watching-view
  visibility without changing the simulation's picture. The former centre and
  all-ground-corners admission rule delayed enemy factories and extractors.
- CV-01: construction sight already begins with a live nanoframe; no change.
- CV-03: factory products retain independent visibility; no parent gate added.
- RA-04: feature orders use per-constructor elapsed working ticks and the
  documented ground/aircraft completion boundaries. Aircraft feature reclaim
  neither invokes StartBuilding nor waits for INBUILDSTANCE. Unit reclaim's
  sixteen-tick damage cadence is unchanged.
- Feature payout occurs in one operation at timer completion. The existing
  incremental helper remains for legacy shared-progress accounting and feature
  lifecycle handling. This change does not claim conformance for every payout,
  difficulty-scaling or animation-refusal detail described by RA-04.

## RWE-specific old-save policy

Feature-job elapsed ticks reuse the already saved and hashed reclaim-state
counter. Old RWE feature jobs have zero there and start a fresh timer. Old
shared feature progress is retained only so that already credited resources
are deducted from the completion payout. Fresh features have no shared work
during the countdown. This compatibility policy has no original counterpart.

## Shipped-data diagnostic scenarios

The offline harness loads the actual installed TA archives, unit definitions,
3DO models and COB scripts through loadGameSimulation. It uses a flat test
terrain, abundant human resources and no AI. Artifacts and logs are under
`D:/tmp/rwe-playtest-20261021/`.

- CORTSHIP destroyed into CORTSHIP_DEAD; ARMCA ordered to reclaim while the
  wreck sinks. Before the change its 710 metal and 24000 HP require 6710 work
  ticks, approximately 224 seconds at this constructor's rate. The wreck
  eventually disappears: the scenario reproduces slow reclaim, not an
  infinite order.
  After the change the first working tick is 93 and removal is tick 479:
  exactly 386 elapsed working ticks (12.87 seconds), matching RA-04's
  aircraft T=385 completion boundary. Approach takes about three seconds.
- ARMCV and ARMCOM ordered to repair a damaged ARMFLASH both approach and
  restore it to full HP using the shipped scripts.
- ARMCV and ARMCOM guarding an ARMLAB queued to build two ARMPW both approach
  and complete the factory jobs. The reported standing-still failure has not
  been reproduced by these fresh-order scenarios; interrupted or obstructed
  jobs remain an investigation item.
- Shipped ARMMEX model height is 28.6 and footprint 3x3; ARMLAB height is
  14.75 and footprint 6x6. The visibility regression uses those dimensions.
- Envoy door texture CorSea5b in CORSHIPS.GAF has zero transparency-key pixels
  among its 4096 pixels. The shipped doors are single-sided sliding panels;
  there is no demonstrated alpha-hole or SHOW/HIDE fault. The reported camera
  and animation-dependent glitch remains unconfirmed.

This harness executes RWE only. No original-game timing or visual oracle was
executed during this investigation.

## Build and regression checks

The isolated Release game target builds successfully. Focused presentation,
visibility, factory assistance, reclaim, reclaim reach, repair and automatic
reclaim tests pass: 73 cases, 1657 assertions. This includes shipped-dimension
partial visibility, explicitly supplied watching pictures, independent worker
speed/wreck-HP timing, aircraft stance bypass and no pre-completion payout.
The patrol regression accepts a completion credit still waiting in the
production buffer for the normal resource settlement.

The changes were also applied to the main `D:/RWE` checkout while preserving
concurrent edits. Its Release `rwe` target builds successfully. Linking the
shipped-data probe against that checkout's rebuilt library reproduces the
same first-work tick 93, removal tick 479 and 386-tick working interval.
