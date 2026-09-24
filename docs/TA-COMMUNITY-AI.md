# What the original's AI modders learned, and what of it RWE can use

Between 1998 and 2005 a community of people spent years doing one thing: making
Total Annihilation's computer player less bad, from the outside, without being
able to change a line of its code. The only lever they had was the AI profile —
a flat text file of `weight` and `limit` directives naming units. They could not
change *how* it thought, only *what it was allowed to think about*.

That constraint is what makes their work worth reading. Every conclusion they
reached is a conclusion about build composition and economy that survived years
of adversarial testing against human players, with all the AI's behavioural
faults held fixed. RWE's computer player is a different machine with different
faults, so none of their numbers transfer directly — but their *agreements*
are hypotheses with unusually good provenance, and RWE has an arena that can
test each one in a day.

Two sources, absorbed 2026-09-24:

- **Switeck's "AI Design Guide"**, the community's standard tutorial — the
  profile grammar, a catalogue of the engine AI's behavioural bugs, the
  economic doctrine, and a set of benchmark timings for judging whether an AI
  is any good.
- **Nine shipped community AI packs** (`D:\taais`): Banzai 3.0 and its 4.0
  bugfix, CounterStrike, Moon AI v15a, Mostly Harmless, Queller TA:CC v16,
  BAI 4, Flea Bowl 3, Siege 2. Five carry a full `Default.txt` that can be
  parsed and compared.

The mining script is `$CLAUDE_JOB_DIR/tmp/aimine.py` in the session that did
this; the method is simple enough to redo — parse `weight`/`limit` directives
under the `hard` plan, first occurrence wins, and tabulate across authors.

## 1. The profile grammar, and what it confirms

`LIMIT <unit> <n>` caps how many completed instances the AI keeps. `WEIGHT
<unit> <n>` sets its share of a weighted draw, 0.05 to about 255, where 1 is
the implicit default and a weight below 0.01 rounds to zero and is never built.
`PLAN easy|medium|hard` splits the file by difficulty; directives before the
first `plan` apply to all three. `//` is a comment. **The first occurrence of a
directive wins**, so a later duplicate is dead — which is why every profile
puts its weights at the top.

There are group keys as well as unit keys, and the corpus uses them: `Limit
PLANT` (5, 15 and 25 in three different profiles), `Limit level2 20`, `Limit
level3 4`, `Weight ARM`/`Weight CORE` (0.1–0.2), `Weight METAL`, `Weight
ENERGY`, `Weight PLANT`.

This is the same grammar [`TOTALA-EXE-AI.md`](TOTALA-EXE-AI.md) §7 describes
from the binary — three directives, `plan`, `weight`, `limit` — reached
independently. Note the one place the two do **not** collide: that section
records `ai_limit`, the *FBI definition key*, as parsed and never read. The
profile's `limit` directive is a different thing and plainly works; the whole
community practice rests on it.

## 2. The behavioural faults, from the outside

Switeck catalogues the AI's faults from thousands of hours of play. Set beside
the decoded account in [`TOTALA-EXE-AI.md`](TOTALA-EXE-AI.md), most of them
land on the same mechanism — which is a strong mutual check, because one source
is a disassembler and the other is a player in 1999 who never saw the code.

| Observed, by a player | Decoded, from the binary |
|---|---|
| "The AI's aircraft wander alone randomly around the map"; rarely three together; never a threat | The classifier tests `canfly` **before** the armed test, so every aircraft is sorted into the explore slot and **no aircraft ever receives an attack order** (§2, §3) |
| "It looks for the nearest enemy unit to its attack force, that's almost all it knows" — the *Solitaire* bug | The wave attacks **the single nearest hostile unit to its centroid**, every 300 ticks, regardless of what it is (§5) |
| "After many attacks the ground force gets smaller and smaller ... the AI is at or near the unit max" | Waves refill only by merge from the paired regroup slot; nothing re-prioritises army over buildings (§2) |
| "Its base ends up built around whatever features are on the map, with no consideration for where its enemy is" | The placement origin walks toward a **strategic centre that is a weighted centroid of that player's buildings only** (§6) |
| "If the AI's commander is damaged, it stops where it is for up to 15 seconds" | — not covered by the decode; a genuinely new observation |
| "Fail-safe routines ... while they are in use the AI ignores the profile weights" | The hard gates before any scoring: reject if energy < 50 or metal < 25 (§6) |
| "The AI doesn't count a unit towards its LIMIT unless that unit is completed" — the *Overbuild* bug | "reject if the **completed** count has reached the profile's limit" (§6) |
| "Never would a nuke be fired nor would antinukes shoot down nukes" | No such order is ever issued (§3) |
| The *Coffee Break* bug: units stop moving for seconds after killing everything nearby | — not covered; the decode would put it in the wave's 300-tick re-target period |

The two accounts agree wherever they overlap, and each covers ground the other
does not. The commander freeze and the coffee break are worth looking for in
the binary; the transport, guard and capture negatives in §3 are worth
believing more than they were, since a community that spent years poking at
this AI never reported it doing any of those things either.

## 3. What five expert profiles agree on

Tabulated across Banzai 3.0, CounterStrike, Moon AI, Mostly Harmless and
Queller — five authors, different years, no coordination. Only the agreements
are interesting; the disagreements are mostly taste.

**Economy sits at the top of every single file.** Not close. Queller weights
fusion 8.2, geothermal 8, solar 7.91 against 0.25 for a Guardian; Banzai
weights solar 18.35 and wind 18.2; Moon AI weights geothermal 9. Every author
independently concluded that the way to make this AI better is to make it
build power and metal before anything else, and to let defences be whatever is
built when there is nothing else left to build.

**Extractor and solar caps are large.** Solar 12/36/40, extractor 6/32/40 in
the three profiles that cap them at all. Queller and Mostly Harmless — the two
most recent and most tested — both sit at 32–40 of each.

**Radar is banned.** `limit 0` in four of five. Switeck says why: "The AI will
build multiple radars right next to each other."

**Dragon's teeth are banned, everywhere, without exception.** The AI walls in
its own factory exits.

**Nuke and antinuke silos are banned in four of five**, because the AI builds
them and then never fires them. Queller is the exception and pairs its
allowance with weight 10, i.e. build it properly or not at all.

**Scouts are banned**: Peeper, Flea, Fido, both Jeffys. The AI's scouting does
nothing with what it sees.

**Anti-air is the single largest "build more of this" signal after economy.**
Defender (`ARMRL`) is limited to 98–100 in three profiles and weighted as high
as 10; Samson is limited 20–80; Flakker 5–16. Against a human who has noticed
that the AI has no air defence, this is the difference between a game and a
formality.

**Static defence is kept small and built last.** LLT banned or ≤1. HLT 4–7,
weighted 0.05 in two profiles. Guardian limited 3–12 but weighted 0.2–0.65 —
present, cheap to reach, never a priority.

**Factories are few and specific.** Kbot lab 3–4, vehicle plant 3–5, aircraft
plant 1–3, advanced lab 1–5, advanced vehicle 1–2. Shipyards zero on a land
profile. Nobody builds one of everything.

**Construction units are surprisingly few.** Basic vehicle 2–4, basic kbot 2–3,
advanced vehicle 2–3, advanced kbot 1–2 — seven to twelve in total across all
four types, and Switeck argues even eight is more than the economy can feed.

## 4. Benchmarks, and where RWE stands against them

Switeck's §14 gives the numbers he judged an AI by. They are for the original
on Greenhaven with 1k starting resources, so they are not a specification —
but they are the only external yardstick we have, and RWE's arena measures
every one of them already.

| What | His figure for a good AI |
|---|---|
| First attack **launched** | 5 minutes, 7 still acceptable; 3–5 on a metal map with 10k |
| Energy produced in 10 minutes | 60k–100k |
| Metal produced in 10 minutes | 4k–7k (nonmetal map) |
| Level 2 units in the attack force | by 10–20 minutes |
| Level 2/3 base buildings started | 10–30 minutes |
| Unit cap of 250 reached | as little as 25 minutes for the winner |
| Mature level-3 economy | under 30 minutes at best, 1 hour acceptable |
| Army resupply rate once fighting | about one replacement every 15 seconds |

Two of his observations are testable claims about *openings*, and ours can test
them directly: **"whenever the AI builds too many solars and metal extractors
before its first factory it usually loses to one that didn't"**, and **"if its
first factory is an aircraft plant it usually loses as well."**

Set against `AiTuningProfile`'s defaults, four gaps stand out. None of these is
a conclusion — the community tuned a different machine with different faults —
but each is a one-knob paired arena run, which is cheap.

| Knob | RWE default | Community consensus | Note |
|---|---|---|---|
| `targetMetalExtractorCount` | **8** | 32–40 in the two best-tested profiles | The largest single gap. Also the likeliest reading of the parked CORE-deficit finding: an AI holding only eight extractors has no margin when a raid takes three |
| `targetSolarCount` | **10** | 12–40 | Softened by `solarOnDemand`, so measure rather than assume |
| anti-air: `baseAntiAirTowerCount` 1, `reactiveAntiAirTowerCount` 3, `antiAirMobileCount` 2 | ~6 total | Defender alone limited 98–100 | Their strongest agreement after economy, and our smallest number |
| `targetVehiclePlantCount` 1, `targetAirPlantCount` 1, `surplusLabCount` 1 | 1 of each | 3–5 vehicle, 3–4 kbot | Factory count is what sets the resupply rate Switeck says decides fights |

## 5. What not to take from this

The profile system exists because the original's AI could not be changed. Its
whole method is *subtraction* — Switeck's own summary is "almost every time I
eliminate units so the AI no longer builds them, the AI generally gets better."
That works because the original picks from its whole catalogue by weighted
draw with no notion of what a unit is for. RWE does not: it has managers that
ask for a thing because something wants it. Banning a unit in RWE would remove
a capability rather than remove a distraction.

So the transferable part is the *economic doctrine* and the *benchmarks*, not
the lists. Specifically:

- **One finished Guardian beats four unfinished ones.** Switeck's sharpest
  point, and it is about concurrency, not composition: an AI that starts
  several expensive things at once stalls on all of them. Worth checking that
  our build manager cannot do this.
- **Spend metal down to just above zero, and keep an energy surplus.** "In the
  long term the AI should use about as much metal as it's making and use LESS
  energy than it's making" — the surplus covers d-gun, laser fire and metal
  makers switching on.
- **Cap base buildings or the army shrinks.** As the unit cap approaches, an
  uncapped base eats the slots the army needed. Ours has a unit cap too.
- **Core needs more energy than Arm** for the same plan, because Core leans on
  energy-hungry lasers. We have never distinguished the two sides' economies,
  and the parked CORE-deficit investigation never considered this.
- **More than about thirty of one unit get in each other's way.** A composition
  cap per type, not just a total.
