# RWE AI strength: audit and first batch

Working note, 2026-10-20. This is about RWE's own computer player
(`--ai-engine rwe`), not the classic/original engine. The classic controller
under `src/rwe/ai/classic/` is unchanged; ADR-0005 makes it the default and its
conformance is a separate contract.

## What the audit found

The largest recorded lever is **army composition**, not income. ARM's lab-share
change was worth +74.5 mean unit margin over 32 games; CORE's measured
candidate (+40.4 mean, 21/32 better, decided-game rate 0.47 against 0.22) had
never been shipped (`docs/AI-TUNING.md` 34-101). After the expansion work the
AI was spend-limited rather than income-limited: 28% more metal, 13% of the
game at the metal cap, and no gain in army size.

The ladder was also not monotonic. `applyFactionDefaults` runs after
`makeProfileForDifficulty`, and CORE's flat `attackArmySize = 14` overwrote
every tier, so Hard and Brutal CORE were as cautious as Standard. Easy ARM
lost both its lab raiders and its vehicle plant.

The combat model has three broad weaknesses:

- targets are acquired at a flat 450 units whatever the unit's weapon range, so
  rocket kbots and artillery walk into range they did not need to;
- one homogeneous wave with nearest-target acquisition, no focus fire, no odds
  check and no attack-move;
- mobile anti-air is leashed to the base and does not escort the wave.

Production only tops a factory up when its queue is empty, and a factory has no
affordability test for units. The full ranked list is in the audit reports
under `D:\tmp\rwe-ai-baseline` (not in the repo).

## First batch

1. **CORE composition defaults.** `applyFactionDefaults` now uses the measured
   candidate: lab shares 0/4/2, vehicle shares 3/1/2, `counterShareBonus = 2`,
   `counterShareTrigger = 0.3606`. Cites `docs/AI-TUNING.md` 76-82.
2. **CORE aggression follows the tier.** The flat 14 became `+6` over the
   difficulty profile, so Standard stays at 14, Hard attacks at 13, Brutal at
   12 and Easy at 20. This fixes the overwrite without discarding the measured
   Standard result.
3. **Acquire at the unit's own weapon range.** A new `acquireWeaponRange` knob
   (default on) makes the ordinary ground acquisition radius
   `max(engageRadius, longestWeaponRange - kiteRangeMargin)`. Kiting,
   anti-air and mission radii are untouched; turning the knob off restores the
   exact old radius for a paired run.

Tests: the AI suite (`[ai]`, 270 cases) passes; the new cases pin the CORE
shares, the difficulty delta, the radius arithmetic and a 600-unit engagement
with a 700-range unit. The full permitted suite result is in the session
report.

## Not done yet

These are the next highest-value, lower-risk items from the audit and are not
in this batch:

- factory queue depth and an affordability/skip rule for a stuck queue head;
- vehicle-plant composition counters and a decayed enemy-composition memory;
- detaching part of the mobile anti-air to escort the wave;
- wave staging/odds check using `EngagementPredictor`;
- battle-field reclaim with two builders and richest-first ordering;
- proportional base defence instead of the binary `enemiesNearBase` trigger.

Every one needs a paired arena run before it becomes a default. The measurement
protocol is in `docs/AI-TUNING.md` section 1: `--ai-engine rwe`, dealt seats,
random starts, paired runs, unit margin and decided-game rate, confirmation on
fresh seeds.

## Status

The batch has been built and measured against a pre-change control binary built
from the same tree. Four seeds on Coast To Coast (RWE against the classic
personality, dealt seats): the odd (ARM) seeds were identical in both arms, as
expected because ARM's composition and T1 weapon ranges are unchanged; on the
even (CORE) seeds one timeout became a decided win and one win took 3,690 ticks
longer. Details and the table are in `docs/AI-TUNING.md`, "First strength
batch". Four games do not establish a strength gain, and the acquisition knob
has not been exercised by a long-range fixture yet. The full permitted suite
passed (1861 cases / 194009 assertions).

The CORE candidate was measured against the pre-ARM-change ARM and still needs
`--evaluate` on fresh seeds. No original-game check applies: this is
RWE-original behaviour.
