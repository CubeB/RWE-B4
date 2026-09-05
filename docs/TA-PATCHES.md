# What the patches changed, and what of it RWE needs

Two different things get called "the TA patch", and they have almost nothing
in common. This is what each one actually did and what, if anything, is left
for RWE to do about it.

## The short version

**The GOG install is already v3.1.** Every engine fix the official patch made
is therefore in the binary every finding in `TOTALA-EXE.md` was read out of,
and RWE has been matching post-3.1 behaviour all along without anyone
deciding to. What is left of the official patch is a handful of *interface
features*, listed below.

**The unofficial patch changes the simulation in exactly one place.** It is
otherwise a wrapper — a DirectDraw replacement, a megamap, a replay recorder,
raised unit limits. The one behavioural change to `TotalA.exe` is the
pathfinding budget, `1333 → 66650`, and RWE has already gone past it.

## Establishing that the GOG build is 3.1

The v3.1 readme's headline feature is the multiplayer resource-sharing chat
commands, which did not exist before it. Both executables carry the strings:

```
$ grep -aoiE "sharemetal|shareenergy|sharemapping" TotalA.exe | sort | uniq -c
      3 ShareEnergy
      2 ShareMapping
      3 ShareMetal
```

— the same counts in the GOG `TotalA.exe` (MD5 `8e74a1dffa1f5988624c52048f5b20cd`)
and in the unofficial patch's (`df08c41ea3e508a0debb6bfb16af1e5e`). So the
question "should RWE implement the official patch's fixes?" has already been
answered by which file the decoding was done against.

The one official fix that is *data* rather than code lands the same way:
readme item 8, "the reload time for the Arm PHALANX has been properly set", is
`rev31.gp3`'s `Weapons/ARMYORK_weapon.tdf` overriding the flak cannon to
`reloadtime=.55`. RWE loads `rev31`, so it inherits it.

## The official v3.1 patch

Read from `readme31.txt` (Cavedog, 27 July 1998), checked item by item against
what RWE does.

| # | Item | Status in RWE |
|---|---|---|
| 1 | `CTRL+P` all armed aircraft, `CTRL+R` all radar/sonar/jammers, `CTRL+W` all armed mobiles except the commander | **Not implemented.** Three selection predicates over the unit list; the smallest real gap on this list. |
| 2 | `+shareenergy` / `+sharemetal` / `+sharemapping` toggles, `+setshareenergy X` / `+setsharemetal X` reserves | **Not implemented.** Needs the chat bar to parse commands. |
| 3 | Hold `SHIFT` over a construction unit to see queued build sites — green for the selected unit, blue for every other builder | **Not implemented.** RWE already draws the placement box and the waypoint trail, so the drawing half exists. |
| 4 | Hold `SHIFT` over a cloaked unit to see a white circle at its minimum cloaking radius | **Not implemented.** RWE has `MinCloakDistance` in the sim (the ninety-tick hold-off), so this is purely a ring to draw. |
| 5 | Assign a factory to a squad with `CTRL+1`–`9`; everything it builds joins that squad | **Not implemented.** |
| 6 | Any campaign mission selectable from New Campaign | **Not applicable** — RWE has no campaign. |
| 7 | Features vanishing from user maps across a save/load | **Not applicable** — RWE's save format is its own. Its known gaps are recorded in `CLAUDE.md`. |
| 8 | Arm Phalanx reload time | **Already correct**, through the `rev31` data override above. |
| 9 | Pathfinding filters that degrade as demand rises | **See below.** |

Item 9 is the interesting one, and it is worth quoting because it describes a
design rather than a fix:

> We have instituted different pathfinding filters that will be run as the
> demand for pathfinding increases. The more units that need paths, the less
> complex the filter. What this really means is that the more units you try to
> move the more varied the path will be that they try to take.

That is a load-shedding scheme, and it is the same problem RWE's
`PathFindingService` solves with `expansionBudgetPerTick` — a fixed budget
divided among whoever is asking, so a crowd of units each get a coarser
answer. The readme is describing the shape of what `0x40EAD3` sets. RWE
arrived at the same structure independently; what it does not do is vary the
*filter*, only the budget.

The readme is also candid that it does not always work, and names Sector 410b
and Steel Jungle as maps where it does not. Worth remembering before treating
original pathfinding behaviour as a target to match exactly.

## The unofficial patch (v3.9.02, 2013)

Installed at `D:\ta patch`. Its own readme describes it as "a patch, NOT a
mod", which is accurate: it replaces the renderer and the network layer and
leaves the game alone.

What it supersedes, from its own list: the v3.1 patch itself, Cavedog's
downloadable units and maps, TA Demo and its various third-party fixes, the
interface upgrade `ddraw.dll` shims, every feature pack, every NoCD patch, the
expanded battleroom, every unit-limit patch (500/1500/5000), the sound
mixing-buffer fix, **the TA Pathfinding Fix**, the LOS tables fix, the
`+atm 10000` patcher, and the multicore patch.

Its features are a megamap (`F4`, fully interactive, with range rings and
patrol routes drawn on it), TA Demo folded in as the replay recorder, a
DirectDraw replacement (`tdraw.dll`) that fixes fullscreen on Kepler and
later, gamma-correct antialiasing and shading, customisable player colours,
`INSERT` to repeat commands, and `+unitname` to spawn units with cheats on.

Two of its fixes are about behaviour rather than presentation, and both are
AI-side:

- AI commanders no longer stop building when attacked.
- AI commanders no longer thrash between targets, and jam, when several
  enemies attack at once.

Both are worth checking against `src/rwe/ai/` — RWE's commander is driven by
`BuildManager` and `ArmyManager`, and the second one describes a target-churn
failure mode that any per-tick chooser can fall into.

### The one simulation change

A byte diff of the two executables isolates it:

```
0x40EAD6:  35 05 00  ->  5a 04 01
```

`0x1535` = 1333 becomes `0x1045A` = 66650. That is the pathfinding work
budget at `0x40EAD3` — see §71 of `TOTALA-EXE.md` — raised fifty-fold. It is
the whole of the "TA Pathfinding Fix" and it is the **only** change either
patch makes to the simulation's behaviour.

It independently confirms the decode: the constant was read out of the
instruction stream before the diff was taken, and the diff found it at the
address the decode named.

RWE does not need it. `expansionBudgetPerTick` is 4000 against the original's
1333, and the relaxed-goal first pass did more for arrival rates than any
budget increase did — exhausted searches fell 443 → 6 and expansions 149,199 →
32,292 while arrivals rose 96 → 116. Raising the budget was the original's
only lever; RWE has a better one.

## What is worth doing

In the order I would take them, all of them from the official patch's feature
list rather than its fixes:

1. **The three selection hotkeys** (item 1). Self-contained, and the kind of
   thing whose absence is felt every game.
2. **The cloak radius ring** (item 4). The sim state already exists; this is a
   ring and a `SHIFT` test.
3. **The build-site overlay** (item 3). Green for the selected builder, blue
   for the others.
4. **Factory squads** (item 5).
5. **The sharing commands** (item 2), if and when the chat bar grows a command
   parser.

Nothing on this list is a behavioural correction. RWE is already matching a
patched game.
