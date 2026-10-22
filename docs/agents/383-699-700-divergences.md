# Deterministic adaptations for mission profiles and elimination

2026-10-20. Companion to `383-699-700-findings.md`, which records the
byte-tactics comparison, executable and evidence. This appendix records
deliberate differences recorded in section 88 of the existing divergence
register in `D:/RWE-spec-room/docs/TOTALA-EXE.md`, anchor
`mission-profiles-deathmatch-and-watching-383-699-700`, and summarized in
[Compatibility](../compatibility.md). The register edit is in the separate
spec-room checkout (commit `55a834434d6820f1caee6d862652717ae4bc1eb1`) and does not modify the owner's dirty main-checkout register.

The owner selected #383 option B, all three modes for #699, and #700's
continued-watching flow. The implementation policies below preserve those
features in RWE's shared simulation. They are not claims of original
implementation parity.

## Mission profile preferences in the RWE AI (#383)

Profile limits and multiplier parsing follow the documented file meaning,
including difficulty gates, direct-unit locks and category membership.
RWE's strategic AI stably sorts its wanted-structure list by decreasing
effective multiplier and multiplies integer combat-unit mix shares by that
multiplier. The original instead feeds
the multiplier into an economy-weighted selection of its own. There is no
claim that translating the same file yields identical build sequences.

RWE reserves capacity for live units, nanoframes, issued construction orders,
factory queues and same-pass planning reservations when
enforcing a profile cap. That prevents several simultaneous factories or
builders from overshooting the mission's intended restriction. It is
stricter than the original's completed-unit-count selection gate. Existing
units are retained, and zero caps do not remove units already in a mission.
The classic AI's existing profile selection is unchanged by this translation.

## Per-player countdowns in a lockstep simulation (#699/#700)

The original's one machine-local countdown freezes that machine's locally
simulated economies. Copying that dependency on which human owns a machine
would make RWE's peers disagree. RWE instead stores deterministic per-seat
elimination/respawn countdowns and freezes the corresponding seat's economy.
After expiry, a successfully respawned seat can settle its economy again.
Other players continue normally while an eliminated player waits.

The original branch replaces only its local player; RWE applies Deathmatch
replacement to all participating human and computer seats through the same
shared simulation. The reason is that all RWE peers simulate those seats,
and a selected game mode must have one outcome regardless of which peer
displays the seat. RWE skirmish uses the same endless-respawn policy as
multiplayer; the original multiplayer victory guard is established, whereas
the separate original skirmish test does not contain that guard.

Original-generator seed equivalence is not required when RWE's established
deterministic generator supplies replacement candidates. The documented
candidate bounds, draw order, attempt count and suitability rules remain
the reference. A safe failure result after failed unit creation is an RWE
robustness choice; the original dereferences the creation result unchecked.
The commander type is the side-resolved stock initial commander type.
Replacement enables base storage with a 200 floor on each starting amount,
and finished units' FBI storage adds on top. That storage arithmetic follows
the reference rather than being a divergence.

## Spectator state and presentation (#700)

The eliminated seat's status is shared simulation state. Permission comes
from the host's advertised game option, rather than a peer's local setting.
Dialog choice, camera and rendering masks are presentation; they do not
restore units, give control to a defeated seat or remove another peer's
command stream merely because a local dialog appears.

The hosted-computer exception originates in the original's distribution of
simulation ownership: exiting its host would terminate those computers.
RWE's lockstep peers independently simulate the computers, so its Yes/No
watching choice has no equivalent host-dependency exception. A departing
spectator still uses the
existing agreed peer-drop protocol so remaining peers do not stall.

The host permission defaults to true in RWE, a chosen default not established
by the reference inspection. Spectators see the union of player visibility
masks with cloak and sonar restrictions retained. The reference does not
establish renderer-wide omniscience. Ordinary multiplayer preserves the
original no-combat-human fallback with a deterministic mapping: once all
participating humans are dead, and at least one has fielded units, the match
reports no survivors and advances the existing match-result countdown.
Remaining AI units do not prevent this end flow. Deathmatch bypasses the
fallback, as in the reference multiplayer test.

## Existing cause-3 destruction gap

The commander-death wipe uses direct 30000 damage with RWE's existing
veterancy treatment and cause-3 death handling. This implements the trigger,
damage and selected cause from the findings, but does not port issue #409's
missing Killed-script return ladder. Complete cause-3 corpse, delay and
destruction behaviour parity is not claimed. That existing gap remains
separate from #699 and from the fixed factory-held-product cleanup.
