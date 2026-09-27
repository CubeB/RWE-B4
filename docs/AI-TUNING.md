# Tuning the computer player

This is the record of what the arena auto-tuner (`tools/ai-autotune.py`,
#390) and the engagement predictor (`src/rwe/ai/EngagementPredictor.*`,
#389) have found about the skirmish AI. It keeps the runs themselves, not just
the conclusions, so that a later run has something to be compared against.

## How a result is judged

The tuner runs every candidate knob set against the **same game with the
knobs left at their defaults**: the same map, seed and seat, and the same
opponent. It judges only the difference between the two. For one game, the
*margin* is the tuned seat's unit count at the end minus the mean of its
opponents' counts, with a dead player counting 0. The *margin delta* is the
tuned game's margin minus the control game's. A delta within ±10 counts as
"same". Each candidate has three verdict counts (better / same / worse), a
Wilson 95% interval on its better-fraction, and a two-sided sign test.

The phases draw from disjoint seed ranges, so no confirmation or verification
game repeats a seed the candidate was chosen on:

1. **Screening** on short games.
2. **Confirmation** of the best four on 16 seeds at 1800 s.
3. A **1v1v1v1** check on Acid Foursome, because a knob tuned only in 1v1 has
   broken the multi-enemy case before.
4. **`--evaluate`**, which re-runs fixed candidates on seeds nobody has used,
   as a final check before anything becomes a default.

The scenarios are both mirrors and the ARM-vs-CORE cross game. The map is
Great Divide unless a search says otherwise. `--tuned-faction` restricts the
tuned seat to one side. All runs below used the frozen binaries in
`D:/RWE-bin/revival-31369ec6`.

## ARM: the kbot lab builds no raiders (applied)

**The search** (`--seed 2`, all three scenarios):
- 24 candidates, screened at 900 s on 6 seeds per scenario.
- The four best were confirmed on 16 seeds at 1800 s.
- The winner changed eight knobs at once. Across all three scenarios it was
  better in 24 of 48 games, with a mean delta of +21.4.

**Splitting it up.** The search gives no credit to any single knob, so
`--evaluate` split the winner into three candidates:

| Candidate | Knobs | Better / same / worse | Mean delta | Sign test |
|---|---|---|---|---|
| **B_labzero** | lab shares only (all 0) | **25 / 2 / 5** | **+74.5** | p < 0.001 |
| A_full | all eight | 29 / 0 / 3 | +71.0 | p < 0.001 |
| C_rest | the other five | 10 / 10 / 12 | −11.7 | p = 0.83 |

The verification ran on seeds 101–116 at 1800 s, 32 games per candidate,
mirror and cross. B_labzero was +77.5 in the ARM mirror and +71.4 in the
cross game. In 1v1v1v1 on Acid Foursome it was better in 6 of 8, mean +11.3.
Its decided-game rate was 0.50 against the control's 0.09: it finishes games.

**The finding.** Everything is in the lab mix; the other five knobs added
nothing and, on their own, cost a little. ARM's artillery share was already 0.
With the raider and rocket-kbot shares also at 0, the kbot lab builds only
what `counterShares` asks for: Rocketeers or Hammers once the enemy's army
calls for them. At twenty minutes the tuned side has no Peewees and about 27
Flashes to the control's 17, because the metal a Peewee would have cost goes
to the vehicle plant. The Flash out-raids the Peewee at the same job.

This is now ARM's faction default (`applyFactionDefaults`,
`AiTuningProfile.cpp`), and it changes the game hash for any game with a
computer ARM player.

## CORE: a candidate, not yet applied

The search used `--seed 3` and `--tuned-faction CORE`, starting at seed 201.
The knob space is in `tools/ai-autotune-knobs-core.toml`. The search:
- Screened 24 candidates at 1200 s on 6 seeds.
- Confirmed the best four on 16 seeds at 1800 s. The opponent was ARM with
  the old shared defaults, *before* the ARM change above.

The winner, `7e50bbe4a7`:

```
labRaiderShare=0  labRocketKbotShare=4  labArtilleryKbotShare=2
vehicleTankShare=3  vehicleMissileTruckShare=1  vehicleMediumTankShare=2
attackArmySize=14  counterShareBonus=2  counterShareTrigger=0.3606
```

- Better in 21 of 32 games, same in 6, worse in 5; sign test p = 0.002.
- Mean delta +40.4: +48.8 in the cross game, +32.1 in the CORE mirror.
- Decided-game rate 0.47 against 0.22.
- 1v1v1v1: 5 better, 1 same, 2 worse, mean +15.9.

Like ARM, CORE does better with **no kbot raiders**. The winner shifts the
lab to Storms and Thuds, and the vehicle plant to Instigators and Raiders.

**This is not the default yet, for two reasons:**
1. Its opponent was the old ARM. Now that ARM plays differently, the result
   has to be re-checked against the new ARM.
2. It has not been through `--evaluate` on fresh seeds. The ARM split showed
   that a search winner can carry passenger knobs, which are knobs that do
   nothing, or worse.

The next step is to rebuild the frozen binaries with the ARM default, then
`--evaluate` the winner. Also evaluate a lab-only split (the three lab shares
alone) and the rest on their own. Use seeds from 301.

## The engagement predictor

`EngagementPredictor` estimates who wins a stand-up fight between two
compositions. The estimate is a Lanchester-square score: the sum of hp × dps
over each side's units. A unit's dps gets a bonus for range, which is how long
it fires before the enemy closes, measured against the opposing side's
average range and closing speed and clamped at 3×. It is plain float and
advisory: no decision reads it unless a knob turns it on.

**Milestone 1 (#391, merged): outpost raids.** The knob
`outpostResponseUsesPredictor` lets the army answer a raid on an outpost only
when the predictor says the answering force wins. Against the plain rule, on
36 paired games, the result was **0 better / 35 same / 1 worse**: no effect.
Measuring why told us more than the null result did. About 90% of CORE's
early extractor losses happen with **no army anywhere near them**, so the
choice of whether to answer a raid rarely comes up. The knob stays off. The
losses are a coverage problem, not an engagement-judgement one.

**Milestone 2 (branch `389-engagement-predictor-m2`, unbuilt and
unmeasured): the tech decision.** When the knob `techUsesPredictor` is on,
the ratio that decides whether to build the advanced lab changes. Today it is
the plain hp × dps per metal of the advanced assault unit against the
tier-one unit. The knob replaces that with each unit's predictor score per
metal against the enemy composition actually known. Before anything has been
seen, it uses each enemy side's own raider and rocket kbot instead. The
advanced unit is also discounted by min(1, its speed / the fastest enemy
unit).

The case it targets is Crystal Maze. There, CORE teches into the Can, which
scores 9.4× under the plain ratio, while ARM's Flashes raid its extractors
unanswered. The tests on that branch use shipped data. Against six Flashes, the
Can's ratio comes out at about 1.28. That is still an edge per metal, but it
no longer clears the 1.5 threshold.

ARM's own number moves further than expected. The Zeus never outranges
anything in CORE's roster, so no CORE composition gets ARM's ratio to 1.5.
Whether that is right has to be shown in the arena before the knob is
considered. The plan is Crystal Maze and Great Divide at 1800 s, ≥10 seeds
each, with a byte-identical hash log while the knob is off.

## What the numbers do not cover

- **Maps.** Every 1v1 result above comes from Great Divide. A share tuned
  there may be wrong on a water map or a small one.
- **Difficulty.** All of it was run at Standard difficulty.
- **Metric.** The margin counts units, not metal. A side that trades a few
  expensive units for many cheap ones scores worse than it played.
  `decidedDelta` (games actually ended) is the check against that, and it
  moved the same way in every accepted result.
