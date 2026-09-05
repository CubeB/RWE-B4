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
| 1 | `CTRL+P` all armed aircraft, `CTRL+R` all radar/sonar/jammers, `CTRL+W` all armed mobiles except the commander | **Already implemented**, and data-driven the way the original is — `GameScene` formats `CTRL_%c` and matches the FBI `Category` token, so `CTRL+W`, `F`, `P`, `V`, `B` and `R` all work off the shipped data rather than off six hardcoded predicates. `rev31` carries every one of those tokens. |
| 2 | `+shareenergy` / `+sharemetal` / `+sharemapping` toggles, `+setshareenergy X` / `+setsharemetal X` reserves | **Not implemented.** Needs the chat bar to parse commands. |
| 3 | Hold `SHIFT` over a construction unit to see queued build sites — green for the selected unit, blue for every other builder | **Already implemented.** `GameScene::renderBuildBoxes` under `isShiftDown()`, green `(83, 223, 79)` for the selected builder and blue `(84, 84, 252)` for every other one the player owns. |
| 4 | Hold `SHIFT` over a cloaked unit to see a white circle at its minimum cloaking radius | **Implemented.** `GameScene::renderCloakRadius`, in the same `SHIFT` block as the build boxes, projecting a world-space circle through the same matrix so it lies on the ground. Not yet confirmed by eye. |
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

Both were checked against `src/rwe/ai/`, and **neither reproduces in RWE**:

- The commander is structurally immune to the first. `EconomyManager` sorts it
  into the builder branch, so it is never in `combatUnits` and `ArmyManager`
  never hands it an attack order to interrupt its building with.
- The second is a target-churn mode that any per-tick chooser can fall into,
  and `ArmyManager` looks like a candidate — `nearestKnownEnemy` re-picks from
  scratch every tactical pass, and `isAttackingUnit` only suppresses a reissue
  against the *same* target. But I could not provoke it. A crowd of four
  enemies produced the same three orders over 240 ticks whether or not a
  stickiness guard was in place, and a nearer enemy arriving mid-approach did
  not steal an engaged unit either.

A stickiness guard was written and then reverted, because a fix for a bug that
cannot be demonstrated is a change with no evidence behind it. If a play-test
ever shows an AI army standing still in a melee, the guard is the first thing
to try and this is the note that says so.

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

Three items, in the order I would take them:

1. **Factory squads** (item 5).
2. **The sharing commands** (item 2), if and when the chat bar grows a command
   parser.

The cloak radius ring (item 4) is written and builds; it wants a look with a
cloaked unit under the cursor before it is called done.

Two items came off this list on inspection rather than on implementation. The
selection hotkeys (item 1) were already in, and so was the build-site overlay
(item 3) — both were listed as gaps on a first pass that read the patch readme
and not the tree. The way to find out what RWE does is to grep it.

Nothing on this list is a behavioural correction. RWE is already matching a
patched game.
