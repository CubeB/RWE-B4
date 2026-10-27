# Script effects from hidden units

Spec-room supplement **WATER-VIS-003** for
[#899](https://github.com/CubeB/RWE-B4/issues/899), 2026-10-27. The owner
clarified that the normal-play defect is **bubble particles revealing a
hidden enemy underwater extractor**. It is not a request to expose the
extractor's model without sonar. The separate watcher case remains #904.

## Evidence and limits

Reference: byte-tactics revision
`e89721c46c3fa06e59558419b54fe26061fafb73`, inspected read-only from
`D:\RWE-boundary-spec` under ADR-0004. Build: matching GOG v3.1
`TotalA.exe`, 1,178,624 bytes; MD5
`8e74a1dffa1f5988624c52048f5b20cd`; executable SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.

The script-effect dispatcher, whole-unit display visibility, bubble creation,
particle records, update, expiry and drawing were statically compared at
the addresses below. **No original-game runtime, demo oracle or RWE test was
run by the spec room.** No RWE source or tests were changed. This supplies
the missing reference comparison, not an executed reproduction of the
reported game. Only prose, behavioral constants, layouts and addresses cross
to the implementation room.

Existing findings: [movement §16](TOTALA-EXE-MOVEMENT.md#16-wakes-thrust-and-the-small-unit-flags),
[render §4](TOTALA-EXE-RENDER.md#4-effects-a-script-asks-for-emit-sfx), and
[player/watcher underwater visibility](PLAYTEST-UNDERWATER-VISIBILITY-FINDINGS.md).

## WATER-VIS-003A: whole-unit visibility gates each script request

The script-effect dispatch at **`0x480EB0`**, including the admission test
identified in the older binary finding at **`0x480EEA`**, calls the
**whole-unit display visibility predicate at `0x465AC0`** for the current
viewing player. A rejected unit produces no effect preparation or particle
system from that request. This happens before piece transformation and
before selecting the effect type; it is not limited to smoke, wakes or one
particular bubble-emitting unit.

The exact recognized types in this dispatch are **0–5 and 257–259**. The
gate therefore covers the **259** underwater-bubble request as well as its
sibling script effects. Unsupported types do not gain a supported emission
merely by passing visibility. Other sources such as piece explosions use
different entry points and are not given a new blanket rule by this finding.

For the ordinary foreign underwater case, the display predicate rejects a
cloaked unit, and then rejects a model whose top is **strictly below sea
level** if sonar-contact bit **`0x200`** is absent. This rejection precedes
terrain-information sampling. Seeing the terrain at the extractor's origin
is therefore **insufficient** to admit its new script bubbles.

The owner's own unit passes the display predicate's initial ownership check.
A foreign sonar contact continues to the normal sight/exploration tests;
sonar alone is not an unconditional replacement for every remaining
visibility condition. Active native watchers normally receive the sonar bit
upstream, as WATER-VIS-002 documents, so unjammed visible watched units can
emit. Foreign cloak and the later sonar-jamming qualification still apply
to the original watcher case.

## WATER-VIS-003B: an admitted bubble outlives the unit admission decision

For type **259**, the dispatcher creates one point from the emitting piece's
computed world position. Its second point uses the same X/Z and sea-level Y.
The bubble wrapper at `0x472530` creates the wake-family system with period
**8**, short emitter lifetime **1**, reverse color ramp, and presentation
layer **7**. Initialization at `0x474760` stores geometry and emits
immediately. The short system can emit its second dot on the next tick.

After admission, this system and its dot records do **not** retain a source
unit identity for subsequent display-visibility decisions. Update at
`0x473170`, emission eligibility at `0x473220`, and per-dot stepping at
`0x474580` do not recheck that source unit's ownership, sonar or current
display predicate. Thus losing source visibility after a request was admitted
does not immediately delete the emitted dots or cancel their short system.

Each dot is independently considered for drawing at **`0x4745E0`**. It tests
the viewer's map information at the **particle's projected position**,
including the particle's own height. With dynamic sight it requires the
appropriate nonzero current-sight sample; otherwise it checks the viewer's
permanent-information bit. Out-of-grid positions fail. This can hide a dot
without deleting it, and a later draw can show it again if that point becomes
visible before expiry. It does not reapply the source unit's sonar gate.

Dot expiry at **`0x474720`** is separate: it expires after its end tick, or
when the terrain beneath its current position is at or above sea level.
The emission at `0x474880` assigns an end tick **48 ticks** after that dot's
creation, from six periods of eight. The end tick itself survives the
time comparison. Losing unit visibility is not an expiry condition.

Consequently a short residual bubble trail after sonar or unit sight is lost
can agree with the original, provided those requests were admitted earlier
and the individual dots still pass their point-visibility test. Continuous
new requests from an already-hidden enemy are a different case.

## Implementation and verification contract

- Apply the whole-unit visibility decision when consuming a script `emit-sfx`
  request, before dispatching the supported effect. A position-only terrain
  visibility check at a bubble helper is insufficient for the reported case.
- Keep that admission in presentation. Do not suppress COB execution, alter
  the deterministic event stream or manufacture sensor contacts to hide a
  particle locally.
- Preserve ordinary player model hiding without sonar. Verify no new bubbles
  arise from a wholly submerged foreign extractor when its terrain is visible
  but its model is hidden. Contrast with an owner, a visible sonar-detected
  foreign extractor and an unjammed full-view watcher.
- Separately test loss of visibility after an admitted request: retained dots
  may finish their lifetimes while passing point visibility. Do not add a
  source-unit-based purge and describe it as original conformance.
- Reproduce with a shipped bubble-emitting extractor script/model. Record
  owner, viewer, sonar, model-top height and request time; distinguish newly
  emitted dots from residual dots. This static finding does not establish
  those values for the 2026-10-26 report or supply an original runtime capture.
