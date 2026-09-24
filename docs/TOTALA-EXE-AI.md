# The original's computer player

What `TotalA.exe` does when it plays against you. This is the one large
subject in the findings corpus that **was not read by this project**: every
section below comes from an independent clean-room analysis of the same binary
by the Nanolathe project, and is recorded here because the subject was a
standing blank in our own notes and because it changes how several of RWE's
own decisions should be described.

Read [`TOTALA-EXE-EXTERNAL.md`](TOTALA-EXE-EXTERNAL.md) first. It says where
this came from, why the two readings are comparable at all, and what
"unverified" means here — nothing in this file has been checked against the
binary by us, and the corpus's usual standard is that it should be before
anything is built on it.

Their confidence markers are carried through unchanged. **Established** means
their static trace supports it; **Supported inference** means an important
caller or branch is still open. Where a claim is load-bearing for an RWE
decision, the "how to check it" line says what would settle it here.

## 1. Why this matters to RWE, before any of the detail

RWE's computer player is its own design and always has been
(`docs/ai-architecture-proposal.md`). That was a choice made in the absence of
evidence: §103 recorded, in as many words, that *whether the original's
computer player uses transports at all was not traced*, and that nothing
transport-shaped had turned up while walking the order tables — "an absence of
evidence".

The independent reading answers it, and the answer is a flat negative: **the
original's computer player has no transport policy whatever.** If that holds,
then a good deal of what this project has been treating as a gap to close is
not a gap at all. The sea-ferry work, the muster point, the landing search —
none of it has an original behind it. That does not make the work wrong; it
makes it a **divergence by design** rather than a conformance effort, and
§88 is where it should be described.

The same applies to aircraft, guard, capture, repair and reclaim. See §3.

## 2. The shape of the thing

**Established.** The computer player is a task manager with **ten fixed
slots**, run from phase 5 of the tick (see
[`TOTALA-EXE-EXTERNAL.md`](TOTALA-EXE-EXTERNAL.md) for the phase order). The
coordinator runs **every tick for every eligible player**; the work inside it
is gated by per-slot deadlines, and a classifier sweep re-sorts units every
**30 ticks**.

| Slot | Task | Deadline when re-armed |
|---|---|---|
| 0 | empty | — |
| 1 | resource and production queue | `+30` |
| 2 | attack wave A | `+300` |
| 3 | regroup A | `+150` |
| 4 | construction and placement | `+90` |
| 5 | inert — but its group record catches armed buildings, and is the wave's first-choice rally point | — |
| 6 | attack wave B | `+300` |
| 7 | regroup B | `+150` |
| 8 | explore and gather | `+30 + rand(900)` |
| 9 | random-walk rally | `+30 + rand(150)` |

**Slot 9 has no producer.** Nothing ever puts a unit in it; it stays empty for
a whole game unless a save or a control-group assignment fills it.

**The classifier** assigns each unit to one of six groups every 30 ticks, by a
strict predicate order — first match wins:

1. building-class **and** armed → slot 5
2. building-class, unarmed → slot 1
3. builder → slot 4
4. `canfly` → slot 8
5. `MinWaterDepth` ≥ 1 → slot 7
6. armed and mobile → slot 3

Note what that ordering does: **every aircraft is classified as a scout**,
because the `canfly` test comes before the armed test. See §3.

**Waves are not populated by the classifier.** Slots 2 and 6 fill only by
"wave merge" from their paired regroup slot: when the wave is empty it takes
the peer's first member; above a distance threshold it sheds its farthest
member back to the peer; below it, it pulls in nearby peer members. There is
no per-tick scan of combat strength anywhere.

## 3. What it never does

All **Established**, and all of them negative results — which is the class of
claim most worth checking before relying on, because a bounded search proves
only that the reader is not in the searched set.

- **Transports.** It never issues `Ground_Pickup`, `VTOL_Pickup`, `Unload`,
  `VTOL_Landing` or `BeCarried`; the manager emits none of those command
  codes. It never queues a transport for production either — `canload` zeroes
  the unit's economic score, so the weighted reservoir skips it without
  drawing. A transport it happens to own, from a mission placement or a
  capture, gets no special handling at all: an unarmed land carrier stays
  ungrouped for the entire game.
- **Aircraft attacks.** Because the classifier sends every flyer to the
  explore slot before the armed test is reached, **no aircraft ever receives
  an attack order from the computer player.** Air combat happens only through
  return fire and the engine's own autonomous target acquisition.
- **Guard and capture.** Never issued.
- **Repair and reclaim.** Not decisions. They are a side effect of the
  ordinary `RepairPatrol` / `VTOL_RepairPatrol` handler that any
  `canreclamate` builder falls into when idle-patrolled.

**How to check the transport negative here.** We have the binary and the
tooling. A first attempt (2026-09-24) went looking for the AI reading the
`canload` bit and found `0x4067C4` testing bit 8 of `def+0x245` — but that is
the **order handler**, not the issuer: the branch requires `canload`, then
compares `transportsize` (`def+0x22A`) against the cargo's field at `+0x7E`
and refuses with the user-facing strings at `0x501704` ("Loading unit") and
`0x501714` ("Unit is too large to transport"), the COB entry point
`TransportPickup` being named at `0x5016F4`. That path is common to every
player and says nothing about what the AI emits. Settling the negative means
enumerating the order codes written by the manager's own order-emission sites,
which has not been done here.

## 4. Naval policy, in full

**Established.** Three `MinWaterDepth` word tests and nothing else:

- grouping into wave B at a threshold of **50,000**, against wave A's **20,000**;
- a **×3** multiplier on the production score;
- a wider set of placement regions.

There is no surfaced/submerged test, no awareness of which weapons work in
water, and no coastline search.

## 5. Attack waves — the only thing resembling retreat

**Established.** A wave gathers at the base — the centroid of the slot 5, then
slot 1, then slot 4 records — until it has **6** members. It then latches
"engaged" and, every 300 ticks, attacks **the single nearest hostile unit to
the wave's centroid**, regardless of what that unit is or what the wave is
made of. When attrition brings it to **3 or fewer** it un-latches and falls
back to base.

No health test, no per-unit disengage, no evaluation of the target's threat.

## 6. Construction and placement

**Established**, with the constants as given.

- **Placement root.** The origin walks from the builder toward the "strategic
  centre" by **160 world units** per failed attempt, capped at the map extent.
  The strategic centre is a weighted centroid of that player's **buildings
  only** — mobile combat units contribute nothing to it.
- **Extractors.** Draw `rand(255)` against the schema's `SurfaceMetal`. Below
  it, do an exhaustive nearest-deposit search over a precomputed metal-spot
  vector held in a binary max-heap; otherwise scatter statistically over 30
  trials, with region sizes drawn once at match start.
- **The metal-spot vector is built once at battle entry from the features and
  never rebuilt**, so a deposit whose wreck has since been cleared stays in
  the list.
- **Selection.** A cumulative-weighted-random reservoir over a per-type score
  `trunc((other × otherMix + metal × metalMix + energy × energyMix) × weight / 10000)`,
  followed by a side-string filter that discards a pick belonging to the wrong
  side **with no re-draw**.
- **Hard gates before any scoring:** reject if energy < 50, if metal < 25, if
  the session mode is 1 and the candidate is `downloadable`, or if the
  completed count has reached the profile's limit.

**A latent defect they record.** The scatter helper's site-acceptance limit is
built from the **schema's** `SurfaceMetal`. Read instead from
`[GlobalHeader]`, which never authors that key, it comes out 0 — and 0 rejects
every non-extractor placement trial, giving a computer player that builds
nothing but metal extractors for a whole battle. Recorded here because if RWE
ever ports this scoring, that is the mistake to not make.

## 7. Difficulty and the profile system

**Established.** Three directives — `plan`, `weight`, `limit` — read from
`ai\default.txt` or from the file named by the mission's `aiprofile` key, plus
a per-definition `ai_weight` in the FBI text.

- **`ai_limit` is parsed and never read anywhere.** A retail defect.
- The economic handicap — every positive production contribution and every
  inter-player transfer to a computer player scaled by **0.5 / 0.7 / 1.0** —
  is the one part of this we already had: see
  [`TOTALA-EXE-ECONOMY.md`](TOTALA-EXE-ECONOMY.md) §23 and `0x40144E`, which
  records the same three numbers from our own reading. **The two readings
  agree**, which is the only cross-check in this file that has actually been
  performed.
- New here: the directive grammar, the fact that `weight` and `limit` apply
  catalog-wide to *every* manager including human slots, the tokeniser and
  `atof` rules, and the two-passes-per-computer-player fragment semantics.

## 8. Smaller behaviours

**Established.**

- **Reaction to being attacked** is two unrelated things: a construction
  throttle that freezes new building starts from `cancapture` builders for
  `rand(300) + 30` ticks, and ordinary engine return fire — which is not
  AI-specific and applies to human units identically.
- **Standing orders are overwritten every 30 ticks.** The classifier forces
  every computer-owned unit to fire-at-will, puts `cancapture` units on
  *maneuver* and everything else on *roam*, regardless of what a script or a
  capture left behind.
- **The displayed name** is `AI:` followed by the *local human player's own
  name*, not a profile or personality name.
- A production-score bonus applies while `unitLimit >> 1 < liveUnitCount`.
- The weapon-maintenance sweep is a rotating cursor with no targeting logic of
  its own; it reuses ordinary acquisition.

## 8a. A second, independent source: the modding community

Added 2026-09-24. Everything above is one clean-room reading of the binary.
There is a second body of evidence about the same AI that owes nothing to any
disassembler: the profile-modding community, which spent years from 1998
onward making this computer player less bad from the outside and writing down
what it did wrong. [`TA-COMMUNITY-AI.md`](TA-COMMUNITY-AI.md) has that in full
— Switeck's "AI Design Guide" and nine shipped AI packs.

It matters here for two reasons.

**It corroborates the decode, from the other end.** A player in 1999 with no
access to the code reports that the AI's aircraft "wander alone randomly around
the map" and are never a threat; §3 says no aircraft ever receives an attack
order because the `canfly` test precedes the armed test. He reports that the AI
"looks for the nearest enemy unit to its attack force, that's almost all it
knows"; §5 says the wave attacks the single nearest hostile to its centroid. He
reports that a `limit` is only counted against *completed* units — he calls it
the Overbuild bug — and §6 has the same gate. He reports that the AI builds
nuke silos and never fires them, and §3 has no such order being issued
anywhere. Four independent confirmations of four separate claims, one of them a
negative, is about as good as this kind of evidence gets.

**It confirms the profile grammar** — `plan`, `weight`, `limit`, first
occurrence wins — which §7 read out of the parser. One clarification the two
sources together make necessary: §7's "`ai_limit` is parsed and never read"
is about the **FBI definition key**. The profile file's own `limit` directive
is a different thing, it plainly works, and the entire community practice rests
on it. Do not read §7 as saying limits do nothing.

The community also reports two behaviours the decode does not cover, both worth
looking for: a damaged commander freezing in place for up to fifteen seconds,
and attack units halting for a second or two after clearing everything near
them (the "Coffee Break" bug, which he rates the AI's single worst trait).

## 9. What this says about RWE

Not findings — consequences, and the reason the file is worth having.

1. **Our AI is not behind the original; it is a different thing entirely.**
   The original has no transport policy, never attacks with aircraft, never
   guards or captures, and picks its wave target by plain nearest-enemy. RWE
   does all of those. That belongs in §88 as deliberate divergence, stated
   once, rather than being re-litigated every time a play-test finds the AI
   doing something the original never did.
2. **The 30-tick classifier cadence and the 300/150/90/30 deadlines** are a
   sanity check on our own tuning: `tacticalTickInterval` and the various
   `*Seconds` knobs are in the same territory, which is mild evidence that our
   cadences are not absurd.
3. **The "wave of 6, fall back at 3" rule** is a much cruder version of what
   `navalAttackFleetSize` and `reinforcementSize` do. Ours gathers, sails and
   recalls on the same shape of rule, with the numbers measured rather than
   copied.
4. **`ai_limit` being dead in the original** is worth knowing before anyone
   tries to honour it for compatibility.
