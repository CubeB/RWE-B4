# Underwater visibility for players and watchers

Spec-room finding for [#899](https://github.com/CubeB/RWE-B4/issues/899) and
[#904](https://github.com/CubeB/RWE-B4/issues/904), 2026-10-27. The owner
reported invisible enemy underwater metal extractors and inability to see
underwater units while spectating. Exact map, models, detection state and
played executable are not yet captured.

## Evidence and limits

Reference: read-only static comparison against byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, from the separate
`D:\RWE-boundary-spec` documentation checkout under ADR-0004. The reference
is the matching GOG v3.1 `TotalA.exe`, 1,178,624 bytes, MD5
`8e74a1dffa1f5988624c52048f5b20cd`; measured executable SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
The visibility, sensor update, watch-mode transition and model compositor
were checked at the addresses below. Only prose findings cross to the
implementation room.

No original-game session, demo oracle, RWE executable or RWE test was run by
the spec room. This is **static evidence**, not a reproduced gameplay result.
No RWE source or tests were changed. The existing
[naval audit's rule 11](NAVAL-SYSTEMS-PARITY.md#11-underwater-model-visibility-and-the-composite-water-pass)
records the player water-cut behavior but did not cover the watcher sensor
initialization that makes it meaningful here.

## WATER-VIS-001: a normal foreign unit needs the applicable detection

The original's unit-display visibility at `0x465AC0` accepts the supplied
viewer's own unit before foreign-unit tests. For a foreign unit it rejects
cloak first, then rejects a model whose **top is strictly below sea level**
unless the unit has the sonar-contact bit **`0x200`**. Otherwise it continues
with the map visibility samples. A model top exactly on the surface is not
rejected by the strictly-below test.

Separately, the composite model draw at `0x459200` checks whether the
parent's whole origin height is below sea level. In that case the underwater
pixels are made transparent when the owner differs from the viewing player
and the sonar-contact bit is absent. For the viewer's own unit or a sonar
contact those pixels receive the blue palette remap instead. The threshold
is inclusive, after adding the model's normal depth bias of 50 (125 for a
digger). This is distinct from deciding whether to draw the whole unit at
all.

Consequently, seeing an enemy's position or above-water particles is not
alone evidence that its underwater model should be shown. The reproduction
must capture owner, model top, sight and sonar. These rules do not authorize
removing the ordinary player's detection-dependent cut to fix a watcher.

## WATER-VIS-002: native watch mode supplies contact bits

The original **does have native multiplayer watch mode**. A conclusion that
all spectator behavior has no original counterpart would be incorrect.
The continue-watching path in `0x464F80` sets the local participant's watcher
flag, clears the two map fog flags, refreshes sight and broadcasts the player
state when the host permits watching or surviving locally hosted AI requires
the host to remain.

At sensor update `0x467440`, when the game has at least two players, every
live unit is initially assigned both radar and sonar contact bits
**`0x300`** if the current viewer is an **active watcher**. This is the same
initial grant used for the viewer's own units, with a separate ally-sharing
grant also present. It does not require the watcher to own a sonar unit or
have sonar coverage at that position.

Those sonar bits satisfy the WATER-VIS-001 underwater-model test and select
tint rather than cut at composition. The absence of a separate watcher
branch in the compositor does not mean that watchers are supposed to lose
foreign underwater models: the grant is upstream in sensor state.

**Qualification:** the sensor update later runs foreign active radar/sonar
jammers. The sonar-jam callback at `0x467980` clears bit `0x200` and sets the
jammed bit `0x400` for units visited in range; that callback itself contains
no watcher exception. Therefore this finding establishes ordinary unjammed
watcher visibility, not unconditional original visibility through all sonar
jamming. A full runtime check of observer-plus-jammer combinations remains
outstanding. The early fewer-than-two-players return is also part of this
static finding, not a guarantee about a one-seat watch setup.

## Implementation contract and verification

- Treat a watcher reported as having full model visibility consistently
  through both model admission and underwater fragment selection. Do not
  base one of those decisions on a nominal player seat's ownership and sonar
  while the other deliberately displays every unit.
- For a normal player preserve WATER-VIS-001: own units, detected foreign
  units, and foreign units without sonar are distinct cases. Capture a
  submerged extractor with and without sonar, rather than inferring a fix
  from the remaining construction/particle effect.
- Check actual shipped underwater extractor and submarine models, plus a
  partly submerged surface unit. Test observer/playback view and ordinary
  player view separately, including owner and nonowner seats.
- An RWE observer with an unconditional full-visibility policy can implement
  its underwater presentation locally without inserting fake sonar contacts
  into the deterministic sim. That is an implementation choice; this finding
  establishes the visible unjammed reference result, not a requirement to
  reproduce the reference's storage mechanism.
- If full-visibility RWE viewing intentionally ignores sonar jamming, record
  that stronger observer policy as a deliberate divergence under §88. The
  unjammed watch-mode case has an original counterpart and should not be
  labelled wholly RWE-original merely because RWE uses a different renderer.
- State the remaining uncertainty honestly: no original-game runtime check
  or RWE visual check is supplied by this static comparison. It does not
  resolve whether #899 was a normal-player detection case, the same watcher
  defect as #904, or an independent construction/model problem.
