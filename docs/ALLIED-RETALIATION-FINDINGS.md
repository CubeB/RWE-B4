# Allied damage and retaliation findings

Research date: 2026-10-21. Dedicated ADR-0004 spec-room research from
`D:/RWE-spec-room`; documentation only. No RWE implementation or tests were
inspected or changed. The byte-tactics checkout was read-only.

## Reference and validation level

Reference revision: `e89721c46c3fa06e59558419b54fe26061fafb73`.
Reference binary: GOG Total Annihilation v3.1, SHA-256
`3b9c0fadabf3dc67ed5f05a70f1e1505a0c65deadd1a3c930adfe30e2a84995e`.
All conclusions below are static reference checks. No original executable,
shipped-data combat scenario or demo oracle was executed. The existing
`TOTALA-EXE-WEAPONS.md` firing-modes finding is corroborated for the direct
victim response, with the guard qualifications below.

## AR-01: Direct victim retaliation excludes its allies

At `0x406F80`, a directly damaged unit is eligible to retaliate only if its
owner does not mark the attacker's owner allied. The tested relationship is
the **victim owner's outgoing alliance toward the attacker owner**: the byte
at victim player offset `0x108 + attacker player index`. A nonzero value
rejects the attacker before either a retaliation mission or per-weapon target
assignment is attempted.

This is not merely a same-owner exclusion. It covers a different owner that
the victim regards as allied, including a teammate represented in that
alliance row. The routine has no separate team-number exception or team-number
comparison. An implementation whose team representation produces the same
friendly relationship must reject that friendly attacker for direct
retaliation. This does not prevent friendly damage itself; it prevents this
automatic response to it. Explicit player attack commands are outside AR-01.

Same-owner damage is also rejected in a normally initialized game. Player
initialization at `0x464290` sets the player's self-alliance byte; reset also
restores it, and original save loading at `0x466050` explicitly forces that
self byte to one after loading alliances. The response routine relies on that
invariant, rather than performing a separate owner-equality test.

Asymmetric alliances remain directional. If the victim regards the attacker
as allied but the attacker does not reciprocate, direct retaliation is still
refused. If only the attacker regards the victim as allied, this eligibility
gate does not refuse retaliation. This states the gate, not a guarantee that
all subsequent range, category, weapon or mission requirements succeed.
The alliance update at `0x452960` maintains outgoing/incoming rows separately
and only conditionally performs the reciprocal outgoing update; therefore the
direction must not be inferred away as necessarily symmetric.

Other direct-response requirements include a valid existing attacker, an
active human/computer owner (types 1 or 2), an armed or kamikaze victim and
completed construction. Mission/category/reach constraints additionally govern
whether an attack mission can be inserted or individual weapons redirected.
The alliance veto precedes those two response routes.

**Implementation acceptance scope:** when damage is otherwise eligible for
automatic direct retaliation, same-owner and allied-owner damage must not
assign the attacker as a new weapon target or insert a retaliation attack
mission. Preserve existing legitimate enemy-response conditions. Record the
alliance direction explicitly in any asymmetric-relation check.

## AR-02: Both ordinary firing modes retaliate; Hold Fire does not

The normal direct response allows Return Fire (mode 1) and Fire At Will
(mode 2). The per-weapon fallback at `0x4070F2` requires nonzero fire-mode bits
20-21; zero is Hold Fire and does not receive that target assignment.
The direct-response attempt to insert a mission uses `0x43B1F0` with its
ordinary, unforced setting; that routine independently rejects Hold Fire and
also applies the standing movement restrictions. Consequently Hold Fire does
not bypass the direct-response firing-mode gate through the mission route.

The ordinary target-search helper at `0x43B700` enables autonomous acquisition
only for mode 2. Thus Return Fire and Fire At Will share the ability to respond
to eligible enemy damage, while only Fire At Will performs that ordinary
proactive search. The allied-attacker veto in AR-01 applies to both modes.
The standing-fire command at `0x403100` clears weapon targets on selecting
Hold Fire or Return Fire, as already recorded in the weapons register.

## AR-03: Guard response is a separate, directional path

A damage response begins by notifying references to the damaged unit with
event value 16, before the direct victim's alliance eligibility is tested.
Evidence: `0x406F80` and the reference-notification walk at `0x4897B0`.
This means the direct victim alliance veto cannot be described as a global
veto on all guards learning that their target was attacked.

The ground-follow/guard handler at `0x406300` and aircraft-follow handler at
`0x40FBE0` inspect the guarded target's recorded attacker when handling that
damage notification. Their alliance test is **the attacker's outgoing
alliance toward the guard owner**, the reverse direction from AR-01. If that
byte is nonzero, these handlers do not react by attacking. With ordinary
symmetric teams, allied attackers are excluded. With asymmetric alliances,
these guard eligibility tests can differ from the directly damaged unit's
eligibility, including when guard and victim share an owner.

These handlers attempt to insert a forced attack through `0x43B1F0`. That
forced setting bypasses the helper's normal standing movement and fire-mode
checks. Therefore this static evidence does not support a blanket statement
that guards on Hold Fire can never react to an attacked guard target. If the
forced mission cannot be inserted, the fallback per-weapon redirection is
still explicitly gated by nonzero fire-mode bits. Weapon/mission selection
remains relevant: the order-type dispatcher at `0x43F0E0` separately classifies
the guard owner's relationship to the proposed target and can choose different
attack mission types. No unconditional final-shot guarantee follows from the
forced-call observation.

**Scope limit for a direct-retaliation fix:** AR-01 authorizes reproducing the
victim's alliance rejection. It does not establish that changing every guard
reaction to the same direction or adding an unconditional Hold Fire veto
would conform. Such changes need their own accepted specification or an
explicit divergence entry. An RWE-wide friendly guard exclusion may be a
reasonable product choice, but it must not be presented as this decoded
original rule for asymmetric alliances.

## AR-04: Damage delivery and remaining uncertainties

The damage-application routine at `0x489CE0` returns early for healing cause
10 and omits the response call for cause 11. Other damage reaches the response
before that invocation stores the new last-attacker/cause fields. Thus the
current explicit attacker argument controls direct retaliation; guard
notification timing and its recorded-attacker field warrant care. This
research did not execute the reference event scheduling to establish precisely
which later order update consumes which attacker after multiple same-tick hits.

The original's per-tick weapon-maintenance path at `0x4089A0` also drops an
existing target if its owner has become allied to the firing unit's owner.
This supports the distinction between receiving friendly damage and permission
to keep shooting a former enemy after alliance changes.

No original runtime check has been performed for same-owner hits, teammate
splash damage, asymmetric alliances, either firing mode or guard reactions.
No claim is made here that RWE already follows these findings. For validation,
use actual shipped armed units and distinguish a focused RWE regression check
from an executed original-game or demo-oracle check. This document records no
deliberate divergence.

## Implementation-room validation, 2026-10-21

The direct response now asks RWE's existing alliance predicate rather than
owner equality. RWE represents alliances as symmetric teams; this change does
not introduce asymmetric alliance support or alter guard-order handling.

The regression fails before the change for a teammate's hit in both Return
Fire and Fire At Will. After the change all 27 targeting/guard-assist cases
pass (216 assertions), covering damage still applying, no friendly target or
new attack order, same-owner immunity, enemy retaliation and Hold Fire.
Both isolated and main Release game builds succeed.

An offline RWE probe loads the installed TA archives and spawns a shipped
ARMFLASH and CORAK units. An allied CORAK's hit removes HP without assigning
a return-fire target; an enemy CORAK's hit assigns one. Both firing modes
pass. This uses the actual unit definitions, models, weapon definitions and
COB scripts, with damage injected through the ordinary simulation damage API.
It is not an original executable check or a projectile-impact timing oracle.
Artifacts are under `D:/tmp/rwe-retaliation-20261021/`.
