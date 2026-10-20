# Deterministic adaptations for mission profiles and elimination

2026-10-20. Companion to `383-699-700-findings.md`, which records the
byte-tactics comparison, executable and evidence. This appendix records
deliberate differences for eventual linking from the existing divergence
register. It does not modify an owner-edited copy of that register.

The owner selected #383 option B, all three modes for #699, and #700's
continued-watching flow. The implementation policies below preserve those
features in RWE's shared simulation. They are not claims of original
implementation parity.

## Mission profile preferences in the RWE AI (#383)

Profile limits and multiplier parsing follow the documented file meaning,
including difficulty gates, direct-unit locks and category membership.
RWE's strategic AI consumes those results through its own structure
priority rankings and combat-unit mix shares. The original instead feeds
the multiplier into an economy-weighted selection of its own. There is no
claim that translating the same file yields identical build sequences.

RWE reserves capacity for live units and queued/commanded production when
enforcing a profile cap. That prevents several simultaneous factories or
builders from overshooting the mission's intended restriction. It is
stricter than the original's completed-unit-count selection gate. Existing
units are retained, and zero caps do not remove units already in a mission.
The exact scoring and reservation implementation remains an implementation
room choice and should be documented with the change that introduces it.

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
displays the seat. RWE skirmish can use the same endless-respawn policy as
multiplayer; the original multiplayer victory guard is established, whereas
the separate original skirmish test does not contain that guard.

Original-generator seed equivalence is not required when RWE's established
deterministic generator supplies replacement candidates. The documented
candidate bounds, draw order, attempt count and suitability rules remain
the reference. A safe failure result after failed unit creation is an RWE
robustness choice; the original dereferences the creation result unchecked.

## Spectator state and presentation (#700)

The eliminated seat's status is shared simulation state. Permission comes
from the host's advertised game option, rather than a peer's local setting.
Dialog choice, camera and rendering masks are presentation; they do not
restore units, give control to a defeated seat or remove another peer's
command stream merely because a local dialog appears.

The hosted-computer exception originates in the original's distribution of
simulation ownership: exiting its host would terminate those computers.
RWE should apply its equivalent only to computers actually dependent on the
departing peer, rather than treating every computer in a shared simulation
as locally hosted by every peer. A departing spectator still uses the
existing agreed peer-drop protocol so remaining peers do not stall.

The host permission's default and a renderer-wide omniscient view are not
established by the reference inspection; any selected RWE policy must be
named explicitly instead of being presented as established original parity.
