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

## Individual metal-maker budgeting (2026-10-14, experimental)

The original checks positive net energy before activating a converter
(`TOTALA-EXE-COMPUTER-PLAYER.md` section 121). RWE's controller switched all
makers together from storage percentages. The new `metalMakerUseSurplus`
experiment budgets them individually, preferring the most metal per energy
and breaking ties by unit id. It subtracts the makers' own draw from the same
resource sample that supplied the demand, rather than subtracting the current
activation count from an older sample. Pending commands are remembered, so an
emergency can cancel a queued activation without flooding the command stream.

Storage hysteresis and the existing stall/attack shutdown remain. A surplus
smaller than one converter's cost can fund one extra converter in a cycle:
start at 95% storage and stop at `metalMakerOnAbovePercent` (70% by default).
Otherwise that fractional surplus would be discarded indefinitely.

**It has not earned a default.** `metalMakerUseSurplus` remains false. In 18
paired comparisons on Great Divide, Standard difficulty, seeds 701-706, at a
1,800-second cap (ARM mirror, CORE mirror and ARM-versus-CORE):

| Measure | Result |
| --- | --- |
| Better / same / worse, unit-margin deadband 10 | 8 / 3 / 7 |
| Mean unit-margin delta | -15.6 |
| Mean delta, ARM mirror / CORE mirror / cross | -32.5 / -10.0 / -4.2 |
| Tuned-seat decided-win rate, experiment / control | 0.22 / 0.33 |
| Mean resource-waste delta | +1.8 percentage points |
| Sign test | p = 1.0 |

These are 36 actual games; the cross comparison applies the tune to both
seats, as the existing tuner does. The results do not establish a strength
improvement. In five experimental CSV samples, energy was at least 95% full,
metal below 25, and desired energy demand exceeded income by more than 120;
the controls had none. A possible next investigation is whether unfulfilled
construction demand during metal stalls makes this budget too conservative.
That is a hypothesis, not a confirmed explanation of the match results.

The candidate is in `tools/ai-evaluate-surplus.toml`; use it with
`tools/ai-autotune.py --evaluate`, explicitly using `--ai-engine rwe` for
manual arena runs. `--ai-tune 0:metalMakerUseSurplus=true` enables the experiment
for one seat. Great Divide results and hashes are retained locally under
`.local/ai-surplus/`. The initially requested Acid Foursome four-player check
could not load because this installation has no OTA for that map. The check
was rerun on installed Painted Desert, ARM/CORE/ARM/CORE, seeds 707-708 and a
1,200-second cap: both paired comparisons improved, mean margin delta +34.5,
sign-test p = 0.5. Four successful games are under
`.local/ai-surplus/painted-desert-fourplayer/`. Two pairs are a spot check and
do not overturn the mixed 1v1 results.

Release game and test builds succeeded. All 1,549 runnable test cases and
174,961 assertions passed (one external game-data lookup skipped). Four
transient Windows socket-bind failures on the first full run passed both a
focused host recheck and the repeated full run. The policy has a real resource
settlement test with a 14-tick command delay as well as threshold, efficiency,
sample alignment, frame/death and pending-command cases. With the experiment
disabled, a 900-second control has byte-identical hashes to the previous
executable; two enabled 1,800-second games also have byte-identical hashes.

## First strength batch: CORE composition, tier aggression, weapon-range acquisition

2026-10-20. This is RWE's own AI (`--ai-engine rwe`); the classic engine is
untouched. The batch:

- CORE faction defaults adopt the measured composition candidate
  `7e50bbe4a7` from the section above: lab shares 0/4/2, vehicle shares
  3/1/2, `counterShareBonus = 2`, `counterShareTrigger = 0.3606`.
- CORE's flat `attackArmySize = 14` becomes `+6` over the difficulty profile,
  so Standard stays at the measured 14 while Hard attacks at 13 and Brutal
  at 12 instead of also being 14.
- `acquireWeaponRange` (default on) makes ordinary ground acquisition use
  `max(engageRadius, longestWeaponRange - kiteRangeMargin)`, so a unit that
  outranges its target acquires at its own reach. With the knob off the
  radius is exactly the old 450.

A pre-change control binary and the batch binary were built from the same
uncommitted tree, the only difference being these files. Four seeds
(4101-4104), Coast To Coast, Standard, 1800 s cap, RWE against the classic
"Original TA" personality, dealt seats (RWE ARM on odd seeds, CORE on even):

| Seed | Control (pre-batch) | Batch |
| ---: | --- | --- |
| 4101 | RWE won, 32101 ticks | RWE won, 32101 ticks |
| 4102 | timeout, 54000 ticks | RWE won, 40591 ticks |
| 4103 | RWE won, 41281 ticks | RWE won, 41281 ticks |
| 4104 | RWE won, 35011 ticks | RWE won, 38701 ticks |

The odd seeds are ARM, whose composition is unchanged, and the acquisition
knob did not change the T1 short-range armies; they are byte-for-byte the
same length, which is the expected null. The even seeds are CORE: one timeout
became a decided win and one win came 3,690 ticks later. This is four games,
so it does not establish a strength gain; it is consistent with the CORE
composition record and says nothing yet about the acquisition knob. A
longer-range fixture and more seeds are needed before either becomes a
default claim.

The full permitted suite passed: 1861 cases / 194009 assertions. The AI suite
alone is 270 cases / 9717 assertions. The full governing document is
`docs/agents/rwe-ai-strength.md`; the raw games are under
`D:/tmp/rwe-ai-ab/` (not checked in).

## Second and third AI batches: production capacity and adaptability

2026-10-20. RWE-AI only; the classic engine is untouched. Maps are **Great
Divide** (0% water) and **Dark Side**; Coast To Coast is a ferry test, not a
general strength map.

### Factory queue depth and AA escort

Four seeds (4301-4304), Great Divide, 1800 s, batch 1 against batch 2, RWE
against the classic personality, dealt seats. All eight games were RWE wins,
but batch 2 was slower on three of four (4301 22831 -> 27091; 4302 20071 ->
25051; 4303 23431 -> 21061; 4304 21871 -> 43861). Isolating the two knobs on
the same seeds:

| Arm | 4301 | 4302 | 4303 | 4304 |
| --- | ---: | ---: | ---: | ---: |
| Both off | 22831 | 20071 | 23431 | 21871 |
| Queue depth 2 | 22891 | 22261 | 21061 | 44761 |
| AA escort 1 | 23071 | 20011 | 23431 | 21871 |

"Both off" is byte-for-byte batch 1. **Queue depth 2 is the regression**; the
AA escort is neutral in these classic-AI games (no air) but is kept for human
air play. The default is therefore `factoryQueueDepth = 1`; the depth and the
stalled-head drop remain knobs.

### Composition memory and value odds

Four seeds each on Great Divide (4401-4404) and Dark Side (4501-4504), 1800 s,
defaults against `enemyCompositionMemory=false` and `attackUsesValueOdds=false`
for the RWE seat. All sixteen games were decided RWE wins, the same winner in
each pair. Close-out ticks:

| Map/seed | Defaults | Both off |
| --- | ---: | ---: |
| GD 4401 | 27841 | 24811 |
| GD 4402 | 27631 | 29071 |
| GD 4403 | 19231 | 19231 |
| GD 4404 | 20641 | 20641 |
| DS 4501 | 24901 | 25291 |
| DS 4502 | 21331 | 20881 |
| DS 4503 | 18691 | 18691 |
| DS 4504 | 20281 | 19711 |

The changes are close-out-time neutral in this sample: the features are not
disproven and not shown as a gain against the classic AI, which changes
composition little and rarely leaves its base. They are aimed at a human
opponent, where the enemy histogram and the odds gate have something to react
to. The AI suite passes (279 cases / 9853 assertions); the full permitted
suite result is in the session report.

## Fourth direction: roles, reserve and replay-derived openings

The next batch adds a small group record (wave, reserve, raid, feint) and an
attack-move posture, then the replay-mining work's opening/expansion templates.
The design and the ranked alternatives from Nanolathe's `aikit`, Open
Annihilation and the outside-box review are in
`docs/agents/rwe-ai-adaptability.md`.

## Fourth AI batch: reserve, attack-move and a replay-derived opening

2026-10-20. RWE-AI only. The control is the batch-3 binary; the new binary
adds `reserveWave`, `waveCommitFraction`, `reserveReleaseSeconds`,
`waveAttackMove` and the `humanlikeOpening` timing posture (default off).

Sixteen games on Great Divide (4601-4604) and Dark Side (4701-4704), 1800 s,
RWE against the classic personality, dealt seats, plus a third arm with
`humanlikeOpening=true` for the RWE seat:

| Map/seed | Batch 3 | Batch 4 | Humanlike opening |
| --- | ---: | ---: | ---: |
| GD 4601 | 27091 | 25861 | 25861 |
| GD 4602 | 28651 | 29671 | 29671 |
| GD 4603 | 24331 | 24331 | 24331 |
| GD 4604 | 21811 | 23341 | 23341 |
| DS 4701 | 24841 | 21841 | 21841 |
| DS 4702 | 18931 | 18931 | 18931 |
| DS 4703 | 17041 | 17041 | 17041 |
| DS 4704 | 18301 | 18241 | 18241 |

All 24 games were decided RWE wins, the same winner in every group. The reserve
and attack-move are close-out neutral in this sample; the humanlike opening
produced the identical game in every pair, so the timing posture did not bind
against the classic AI. That is expected: the classic opponent does not punish
the timings the report found, and the opening knob is a bias with existing
affordability/defence gates rather than a script. It stays off by default until
a human play-test or a stronger opponent shows a difference.

The AI suite passes (287 cases / 9982 assertions). The full permitted suite
result is in the session report. Raw games are under `D:/tmp/rwe-ai4/`.

## Fifth AI batch: expansion-frame raids and focus fire

2026-10-21. RWE-AI only. The batch adds `raidExpansionFrames` (default true,
30 s window), informed by the replay study's finding that winners hit economy
and base rather than the middle, and `focusFire` (default **false** after this
measurement) with a 5 s latch.

Twenty-four games on Great Divide (4801-4804) and Dark Side (4901-4904),
1800 s, RWE against the classic personality, dealt seats: batch 4, batch 5
defaults, and batch 5 with both knobs off. The "off" arm is byte-identical to
batch 4 in all eight seeds, which is the control. Batch 5 defaults were slower
on five of eight (GD 4801 18991 -> 19141, 4802 22021 -> 23461, 4804 44551 ->
49171; DS 4904 29671 -> 36391) and faster on three (GD 4803 25141 -> 23281;
DS 4901-4903 slightly faster). All were RWE wins.

Isolating the two knobs on the three worst seeds (GD 4802, GD 4804, DS 4904):

| Map/seed | Focus fire only | Raid only | Both off |
| --- | ---: | ---: | ---: |
| GD 4802 | 23461 | 22021 | 22021 |
| GD 4804 | 45721 | 44551 | 44551 |
| DS 4904 | 36391 | 29671 | 29671 |

**Focus fire is the regression**; the frame/builder raid is neutral on these
seeds. `focusFire` therefore defaults to **false** until a human/stronger
opponent shows the focusing behaviour pays. The raid remains on: it is the
replay-informed economy pressure and showed no close-out cost.

The AI suite passes (292 cases / 10044 assertions). The full permitted suite
result is in the session report. Raw games are under `D:/tmp/rwe-ai5/`.

## Sixth AI batch: directional threat memory and proportional defence

2026-10-21. RWE-AI only. Adds 16-sector threat memory (decayed metal value of
armed contacts near the base), proportional responder sizing, a wave-recall
threshold, and threat-directed defensive tower siting. All are behind knobs
(`threatMemory`, `proportionalDefence`, `threatDirectedDefence`).

Twenty-four games on Great Divide (5001-5004) and Dark Side (5101-5104),
1800 s, RWE against the classic personality: batch 5, batch 6 defaults, and
batch 6 with all three threat knobs off. **Batch 6 and the off arm produced
the identical game in all eight seeds** � the classic opponent does not raid
the base often enough for the new response or siting to bind. Batch 5 versus
batch 6 is not a clean isolation because the saved batch-5 binary predates the
`focusFire` default change; its differences are the focus-fire arm, not the
threat features.

Conclusion: the threat features are a no-op against the classic AI on these
maps, and remain default-on for human play, where base raids are the case they
exist for. No strength claim is made. The AI suite passes (297 cases / 10202
assertions); the full permitted suite passes at 1892 cases / 194709
assertions. Raw games are under `D:/tmp/rwe-ai6/`.

## Seventh AI batch: synchronized second axis and replay expansion target

2026-10-21. RWE-AI only. `syncRaidWithWave` (default true, 15 s window) holds a
formed raid at the rally until the wave records contact, reaches its objective,
or the window expires, then releases it at the outlying target. The
`humanlikeOpening` posture gains `openingExpansionSites = 10` and moves its
first-expansion deadline to **213 s**, matching the replay study; it remains
default-off.

Twenty-four games on Great Divide (5201-5204) and Dark Side (5301-5304),
1800 s, RWE against the classic personality: batch 6, batch 7 defaults, and
batch 7 with `syncRaidWithWave=false`. **Batch 7 produced the identical game to
the off arm in seven of eight seeds** (GD 5204 26071 vs 26341); all RWE wins.
The synchronized raid does not bind against the classic AI, and the expansion
posture is off by default, so this batch is neutral here. Keep sync on for
human play, where the second axis has something to time against.

The AI suite passes (299 cases / 10224 assertions); the full permitted suite
result is in the session report. Raw games are under `D:/tmp/rwe-ai7/`.

## Eighth AI batch: air/ground strike sync and exposed-commander hunting

2026-10-21. RWE-AI only. `airStrikeSync` (default true, 600 radius) holds
bombers and gunships until the ground wave is near their target or in contact;
home defence and fighters are unchanged. `commanderHuntExposedOnly` (default
true) hunts the enemy commander only when it is at least 600 from its base
centroid and its known escort is under 600 metal, with a 10 s latch.

Twenty-four games on Great Divide (5401-5404) and Dark Side (5501-5504),
1800 s, RWE against the classic personality: batch 7, batch 8 defaults, and
batch 8 with both knobs off.

| Map/seed | Batch 7 | Batch 8 | Off |
| --- | ---: | ---: | ---: |
| GD 5401 | 20761 | 20671 | 20761 |
| GD 5402 | 29161 | 29131 | 29161 |
| GD 5403 | 25411 | 23431 | 25411 |
| GD 5404 | 22321 | 25531 | 22321 |
| DS 5501 | 25801 | 24271 | 25801 |
| DS 5502 | 19771 | 19411 | 19771 |
| DS 5503 | 20341 | 25111 | 20341 |
| DS 5504 | 19861 | 19861 | 19861 |

All were RWE wins. Four seeds were faster and three slower; the average is a
few hundred ticks slower, so this is neutral in the sample. The classic AI
rarely exposes a commander or uses the air arm, which is why neither feature
binds. Both stay default-on for human play, where the behaviours are aimed.

The AI suite passes (301 cases / 10250 assertions); the full permitted suite
result is in the session report. Raw games are under `D:/tmp/rwe-ai8/`.
