# RWE AI adaptability design

Working design note, 2026-10-20. It synthesises the RWE AI audit, the
Nanolathe `aikit` study (MIT), the Open Annihilation AI study (GPL-3.0) and
the outside-the-box tactics review. It is RWE-original policy: the classic
engine is untouched, and nothing here is claimed as original-game parity.

## Principles

1. **Observe, do not cheat.** Enemy positions come only from
   `knownEnemies`/`PerceptionManager`; omniscience stays a Brutal-only
   exception. No extra resources or free units.
2. **Deterministic.** Sorted `UnitId`/map iteration, `GameTime` timers,
   integer or `SimScalar` arithmetic, no wall clock and no
   implementation-defined RNG. The AI emits ordinary `PlayerCommand`s.
3. **One army, several intentions.** The full platoon system is not required.
   A small fixed-slot group record (wave, raid, reserve, feint) on the
   blackboard is enough for two-axis play and survives no worse than the
   existing blackboard does on save/load.
4. **Every change is a knob**, with the "off" value restoring today's
   command stream, so a paired arena can measure it.
5. **Judge on decided games and metal, not only unit count.** Unit margin can
   reward cheap-unit trading; use `armyMetal`, `decided` rate, enemy
   buildings lost, and our losses together.

## The adaptation loop

    observe -> estimate -> choose posture -> allocate economy/production
            -> form groups -> act -> measure outcome -> adjust weights

### 1. Observation: enemy model

Add a decayed, role-keyed enemy histogram on the blackboard, built in
`PerceptionManager` from `knownEnemies`:

- roles: raider, rocket/artillery, tank, air, static defence, builder,
  commander;
- counts decay by halving every N passes (integer), so the model follows the
  current enemy army rather than a lifetime total;
- store recent enemy attacks by direction/zone for the threat memory.

This is the substrate for composition counters, odds estimates and
threat-direction defence. It is AI-side state, not hashed, and re-learns after
a load (already true of `knownEnemies`).

### 2. Composition adaptation

`BuildManager` already has `counterShares` for the lab only. Generalise it:

- derive category weights from the histogram (air share, static-defence share,
  long-range share, raider share);
- apply those weights to both lab and vehicle shares;
- use an epoch/commit timer so the AI builds a coherent batch before switching,
  rather than oscillating every pass;
- keep the measured faction defaults as the baseline and the counters as a
  bounded lean.

Open Annihilation's useful contribution is the idea of a **weight/limit
substrate**: preferences as data with limits, not as an if-chain. RWE can keep
the C++ profile but make the weights a function of the observed histogram.

### 3. Posture: odds, not headcount

Replace the pure `armySize >= attackArmySize` gate with an attack posture:

- estimate our committed value and the enemy's known/estimated value;
- use `EngagementPredictor` (already present) for a hp x dps margin, or a
  metal-value ratio where the predictor would be blind (air, artillery,
  defences);
- launch when ahead by a margin, hold when behind, and use hysteresis plus a
  patience fallback so it cannot wait forever;
- during contact, re-evaluate: withdraw when the ratio collapses, reinforce
  when it improves.

Nanolathe's `aikit` does this with odds-gated squads and repulse memory; RWE
has the inputs but never uses them.

### 4. Threat memory and proportional defence

Today any armed enemy within 900 of the anchor puts the AI in Defend and sends
the reserve. Replace the binary trigger with:

- a decaying threat level per direction/zone from observed attacks;
- a response sized against the intruders (count and value), capped;
- tower/rebuild siting that leans toward the directions attacks actually come
  from;
- a minimum home guard even while attacking.

### 5. Groups and multi-axis play

Add a small fixed-slot group record:

- **wave** - main objective, attack-move rather than pure move;
- **reserve** - held at rally, released on contact, enemy commitment or wave
  losses;
- **raid** - fast units, expansion frames/builders preferred, withdrawn under
  tower cover;
- **feint** - optional, small and cheap; timed to pull the enemy before the
  main arrives.

This enables: reserve behind the wave, two-axis synchronized attack, expansion
frame raids, threaten-and-flip, fake retreat, decoy outpost. The outside-box
review ranks reserve and counter-push highest; both need only this record.

### 6. Build order and opening templates

The replay-mining work is extracting real player openings, expansion/factory
timing, composition and first-attack timing from demo recordings. Encode the
robust patterns as a small opening book:

- opening: solar/mex counts and constructor count by map character;
- first factory and expansion timing bands;
- first attack size/timing bands;
- map-character branches (land, water, high wind, choked).

A template sets targets and timing windows, not a rigid script; the managers
still react to what they see.

### 7. Difficulty as two axes

Separate **ambition** (how early/large it attacks, how much it risks) from
**skill** (micro, scouting frequency, adaptation speed). The current ladder
mostly changes counts and cadence and flattens CORE. A harder tier should make
better decisions, not only more units; Brutal may keep its existing
omniscience/income cheat as a separate, clearly labelled option.

## Batch plan

| Batch | Contents | State |
| --- | --- | --- |
| 1 | CORE composition, tier aggression, weapon-range acquisition | merged (#727) |
| 2 | Factory queue depth, stalled-head drop, mobile AA escort | built and measured; depth default 1 after a regression, escort neutral vs classic, kept for human air |
| 3 | Decayed composition histogram + lab/vehicle counter weights; attack posture/odds gate with hysteresis | built and measured; neutral close-out vs classic on Great Divide/Dark Side, all decided RWE wins |
| 4 | Reserve slot, attack-move, two-axis/feint, expansion-frame raids | design ready |
| 5 | Replay-derived opening/expansion templates; map-character branches | waiting on the demo mining |

## Measurement

- Maps: **Great Divide** and **Dark Side**. Coast To Coast is a ferry test,
  not a general strength map.
- `--ai-engine rwe`, dealt seats, random starts, paired control with the knob
  off, 900 s screening then 1800 s confirmation on fresh seeds.
- Metrics: decided rate, `armyMetal`, units/buildings lost, metal/energy excess,
  first-building-kill time, phase duty cycle, and tactic-specific events.
- A tactic that only wins against the classic AI is not proven against a
  human; record demos and play-test the human-facing tactics (feint, reserve,
  bait, fake retreat) separately.

## Replay evidence (2026-10-20)

The official tademos.xyz archive closed on the day of this study; twelve
OTA-family demos were recovered from its archive.org mirror (captured
2026-09-24) and all parse with `tad_probe`. Eleven are decided 1v1s. Role
labels are inferred from the build graph for the 283/294/296-type data sets;
only the exactly-named 3113 game gives certain unit identities, so treat
per-role splits as indicative.

Observed patterns:

- winners' first real army attack: median **452 s with 7 combat units**;
  losers **1207 s with 4**, and five of eleven losers never produced a
  qualifying three-unit attack;
- winners' first hostile shot lands a median **761 world units** from the
  enemy base; losers' **3812** — winners raid economy/base, losers fight at
  the middle;
- winners hold **10** economy sites outside the opening base against 6, and
  first expansion at **213 s** against 252 s;
- first constructor at **145 s** against 182 s;
- first factory time is **not** predictive (57.7 s against 62.4 s);
- two losers opened with commander-only contact at 19 s and 41 s;
- winners field a high-count light/medium raider core plus a few heavies;
  losers over-invest in low-count heavy/artillery. The one exactly-named game
  (3113) is 25 Flash, 6 FAV, 5 SAM, 5 Thunder, 2 CV, 2 Vehicle Plant against a
  loser with one combat unit.

These are hypotheses for RWE, not causal rules: winners may attack earlier
because they are already ahead. The replay-derived opening/timing batch keeps
them behind a `humanlikeOpening` knob until measured. The full report and
extraction scripts are under `D:/tmp/replay-decisions/` (not checked in).

## Cross-project sources

Two open-source Total Annihilation engines were studied read-only:

- **Nanolathe** (`nanolathe-gg/nanolathe`, MIT). Its retail-faithful `classic`
  AI is not the useful part; the `aikit` research layer is. It separates
  Strategy, Economy, Army and Production, scores options by utility, and runs
  odds-gated squads with threat maps, routing and repulse memory. The ideas
  taken into this design are the odds-gated wave/retreat, attack posture,
  decaying threat memory and proportional defence, live composition counters,
  zone-level target scoring, role squads and stuck-order tracking.
- **Open Annihilation** (`open-annihilation/open-annihilation`, GPL-3.0). Its
  AI is a compact classic reimplementation, not an adaptive player: fixed
  squads, periodic tasks, a static utility score and one weighted draw. Its
  useful contribution is the **data-driven weight/limit substrate**, plans and
  opening books, novelty bonuses for untried types, cohesion-first waves and
  value-weighted area targeting.

Both are read for behaviour and architecture only; the RWE code is written
independently. Nanolathe's MIT notice and Open Annihilation's GPL-3.0 licence
mean any literal reuse would need attribution, and none is intended. Neither
project is an original-game oracle; a change that claims parity still needs a
byte-tactics finding under AGENTS.md.
