# Maintainability plan

What it costs to work in this tree, measured rather than guessed at, and the
order in which to bring that cost down. Written 2026-09-21 against `revival`.

Each phase names the measurement that says it is finished. Where a phase has no
such number it is not a phase, it is an opinion, and it does not belong here.

## The measurements this plan rests on

Taken 2026-09-21 over the previous thirty days.

| | |
|---|---|
| Non-test lines added to `src/` | 96,830 |
| Test lines added to `src/` | 43,849 |
| Largest files | `UnitBehaviorService.cpp` 5,958 · `BuildManager.cpp` 5,531 · `GameSimulation.cpp` 4,684 |
| Most-committed files | `UnitBehaviorService.cpp` 85 · `GameSimulation.cpp` 79 · `GameSimulation.h` 61 · `BuildManager.cpp` 57 |
| Compiler warnings | 284 lines, **124 unique sites** |
| Worst object, share of the COFF ceiling | 71% (`GameSimulation`, 23,427 of 32,767) |
| Clean Debug rebuild, 8 jobs, warm ccache | 344 s |

The line counts include lines moved by the `save_util` and `GameSimulation`
splits, so they overstate how much was written. The commit counts do not.

### Churn x fan-in: the number that actually decides the cost

A header's cost is not its size. It is how many translation units include it
multiplied by how often it is touched, because that product is how many
recompiles a month of work pays for. `tools/fan-in-report.sh` prints it:

| header | commits | includers | recompiles |
|---|---|---|---|
| `sim/GameSimulation.h` | 61 | 108 | **6,588** |
| `sim/UnitState.h` | 36 | 88 | 3,168 |
| `sim/UnitDefinition.h` | 22 | 88 | 1,936 |
| `ai/AiTuningProfile.h` | 57 | 21 | 1,197 |
| `game/GameScene.h` | 85 | 10 | 850 |

### What the ceiling work actually bought

`GameSimulation.cpp` measured 1,855 lines on 2026-08-01, 3,104 on 09-01 and
4,966 on 09-15 -- about 133 lines a day through September, across 623 commits.
At the file's measured 5.0 sections per line, the 9,340 sections of headroom
left after the visibility extraction is roughly 1,870 lines, or **about two
weeks** at that rate. The extraction itself bought about two days.

That is the argument for Phase 2 and against more of Phase 0's kind of cutting.
Splitting a file moves the total; it does not change the rate. Only giving new
code somewhere else to land changes the rate.

## Phase 0 -- make the limits mechanical  **[DONE 2026-09-21]**

Every limit in this tree used to be enforced by someone noticing. Now they fail
on their own. Verified by a clean Debug rebuild with `-Werror` on: 385 objects,
zero warnings, zero errors, both `rwe` and `rwe_test` linking, and the suite
green at 795 cases.

- [x] **Warnings to zero, then `-Werror`.** 124 unique sites to nil.
      89 of them were `-Wmissing-field-initializers` and fell out of **25 member
      declarations in 9 headers**, not 89 call sites: `GamePlayerInfo::teamId`
      alone accounted for 40. The fix is a default member initializer of `{}`,
      which is exactly what aggregate initialization already does at those call
      sites -- so no behaviour change there -- and which *is* a change, in the
      right direction, wherever the struct is default-constructed instead. That
      is the rule CLAUDE.md already states under "A member the hash reads is
      initialised by the time the object exists", and `nanoPoint` is what it
      cost to learn. `-Werror` sits behind the `RWE_WERROR` CMake option,
      default OFF; CI turns it on for the MinGW64 job only -- see "What is
      deliberately partial".
- [x] **`tools/section-budget.sh`** -- fails over 75% of the COFF ceiling.
      Wired into the MinGW64 Debug job, the one whose linker actually broke over
      it. It counts only objects CMake produced: a build tree collects
      hand-compiled strays, and one left over from a measurement session read
      140% of the ceiling long after it stopped mattering.
- [x] **`tools/fan-in-report.sh`** -- reports, never fails. Produced the table
      above.
- [x] **`tools/hash-oracle.sh`** -- records the per-tick sync hash of four
      fixed headless arena games and compares a later build's against them.
      This is the proof every Phase 2 item is supposed to carry, made into one
      command. It is a stronger check than the suite for a change meant to
      move code without changing behaviour: an extraction can keep 800 tests
      green and still reorder a random draw or a float addition, and the hash
      catches both on the tick they happen.
- [x] **`tools/syntax-check.sh`** -- compiles one translation unit for
      diagnostics only, out of `compile_commands.json`, writing nothing. It
      exists because `make` is not safe to run while anything is editing
      sources, so this is how a fix gets checked without waiting for a build.

`-Werror` justified itself before it was even committed: it caught two
`unused-but-set-variable` sites in `AirTactics.test.cpp` that the warning
inventory had miscounted and nobody had looked at.

## Phase 1 -- stop the rebuild storms

Most of this arrived as PR #124 ("Break GameSimulation.h's transitive grip"),
which moves `GamePlayerInfo`, `UnitInfo`/`ConstUnitInfo` and `ImpactType` to
leaf headers and forward-declares the rest. It is merged; what it bought is
measured below.

- [x] **#124 landed** 2026-09-21. Two conflicts, both against the same day's
      work: `save_util.h`, where the PR had independently made the same
      narrowing, and `GameSimulation.h`, where its side wins because the type
      moves out wholesale. The `teamId{}` initialiser had to be carried by
      hand into the new `sim/GamePlayerInfo.h`; without it forty warnings come
      back and CI now fails on them. **A header move is where that class of
      regression hides**, and the Phase 0 switch is what catches it.
- [x] **Baseline timed.** Touching `src/rwe/sim/GameSimulation.h` and
      rebuilding `rwe` and `rwe_test`, ccache disabled:

      | | objects | seconds |
      |---|---|---|
      | before #124 | 139 | 195 |
      | after #124 | 123 | 186 |

      Sixteen fewer translation units, 11.5%, corroborating the PR's own
      137-to-120 figure. **Read the object count, not the seconds**: a third
      run of the same command came back at 324 s, so wall clock on this
      machine is noise at this scale. Direct includers are unchanged at 108;
      what went away is transitive, which is what forward-declaring in eleven
      headers should do.

      The honest reading is that #124 is worth having and is not a
      breakthrough. Eleven percent of a 195-second rebuild is twenty seconds.
      The 108 direct includers are untouched and are where the rest of the
      cost is.
- [x] **`UnitState.h` -- measured, and not worth doing.** It looked like the
      best remaining target in the tree: 36 commits a month against 88
      includers. It is not, and the reason is structural rather than
      accidental.

      Touching it rebuilds **143 objects in 209 s**. Of those 143, **125 also
      pull `GameSimulation.h`, `GameScene.h` or `UnitStateFieldTable.h`**, and
      all three need the complete type because they *contain* units --
      `GameSimulation` holds a map of them and `GameScene` holds a
      `GameSimulation`. Forward-declaring in every leaf header that could take
      one leaves a ceiling of **18 objects, 12%**, and most of those genuinely
      use `UnitState` too.

      The second idea failed for a better reason. 52% of the header's edits
      land in the air-movement state machine, which is 23% of its lines, so
      moving that block to its own header should have taken half the churn off
      the other 143. It would not: `UnitState` holds `UnitPhysicsInfo physics`
      **by value**, and that variant contains `UnitPhysicsInfoAir`, so
      `UnitState.h` must include wherever the air states live and everything
      that knows a unit's size still knows its air state. Making it indirect
      means a hashed member behind a pointer, which is the determinism
      discipline and a redesign of the sim's central type, not a refactor.

      **Nothing short of changing what `UnitState` is will move that 143.**
      Recorded as a negative result rather than left as a standing
      recommendation, which is what it was until it was measured.
- [x] **`UnitDefinition.h` -- measured, and worse.** 146 objects depend on it
      and only **3** reach it without also carrying `UnitState.h`,
      `GameSimulation.h`, `GameScene.h` or `UnitStateFieldTable.h`: a **2%**
      ceiling against `UnitState.h`'s 12%. Not worth doing.

### What the two dead ends say together

Header fan-in in this tree is not an accident to be tidied away. A unit is the
thing the simulation is about; `GameSimulation` holds a map of units,
`GameScene` holds a `GameSimulation`, and four fifths of the tree reaches one
of those. Phase 1's remaining candidates are all downstream of that, so the
phase is finished -- #124 took the transitive slack out and there is no more
to take without changing what a unit *is*.

**Stop looking for rebuild-cost wins in headers.** The next real work is
Phase 3, which is about a class of bug rather than a class of cost.

### Corrected: `AiTuningProfile.h` is not a target

An earlier draft of this plan said its profile *values* should move to a `.cpp`
so that tuning a knob rebuilt one object. That was wrong, and the measurement
says so: the file is 242 member declarations and **1,635 comment lines**, 84%
documentation. There are no values to move -- the defaults are the members --
and the comments record the measurements behind each knob, which is exactly
where they belong. Its fan-in is 21, the lowest of the four. Left alone.

## Phase 2 -- give new code somewhere else to land

- [x] **PR #123 landed** 2026-09-21 -- an `AirMovement` module around the
      aircraft state machine, out of `UnitBehaviorService.cpp`, which comes
      down from 5,958 lines to 5,790. This is issue #118.
- [x] **PR #122 landed** 2026-09-21 -- the resource settle becomes a
      `ResourceSettler` module; `GameSimulation.cpp` 4,684 to 4,654.

Both were held to the sync hash rather than to a reading: **17,999 ticks x 4
scenarios, every hash identical**, against baselines taken before any of the
three merges. So #124, #123 and #122 together changed nothing the simulation
can observe. Tests went 795 -> 807 cases as the two PRs brought their own.

One thing to watch in #122: `settleResourcePool` opens by clamping a negative
supply to zero where the original divides straight through, on the argument
that the original never arrives with one. The oracle never saw it fire, which
is evidence and not proof. If a negative supply turns out to be reachable,
that clamp is a divergence and wants a line in `TOTALA-EXE.md` section 88.

- [x] **`BuildManager::update`** 2026-09-21, two cuts. The 477-line
      site-search chain inside its priorities loop is `choosePrioritySite`,
      and the battlefield-reclaim rule is `tryBattlefieldReclaim`.
      **`update()` goes 1,613 -> 975 lines, down 40%.** The file is 51 lines
      *longer*, which is the honest shape of an extraction: a struct, two
      signatures and two call sites are not free. Both hash-identical,
      17,999 ticks x 4.

      What made each cut safe was measuring the block's free variables first
      rather than starting and finding out. The site search read twenty outer
      names and needed a context struct; the reclaim rule read eight and
      needed nothing. The two blocks are similar sizes and that number is
      what decided how much work each was.
- [x] **#116, the half that is a leak** 2026-09-21. `setMoveOrders` and
      `setCloakRequested` join `setBuildStance`. See the correction below.
- [ ] **#119** -- a perception interface for the AI read side. The largest
      remaining item, and the one aimed at the actual churn hot spot. Its own
      figures are a little over: measured 2026-09-21, **91 call sites across
      11 files** in production (196 across 23 with tests; the issue's 167/15
      sits between the two) and **38 declarations** taking
      `const GameSimulation&`, not ~50. The three fog-of-war checks appear
      only 9 times, which makes centralising them the cheap half. None of that
      changes the ordering: the AI is where the churn is.
- [ ] **#117 PieceController** -- measured smaller than it claims; see below.

### Corrections both issues need before anyone starts them

**#116 says "two direct writes" and "a two-line change".** Counted, presentation
mutates simulation state in **twelve** places: the two named, **two more raw
field writes in `GameScene_debug.cpp`** (`fireOrders` and `hitPoints`, both from
the debug spawner, and `hitPoints` is hashed), and eight through `UnitState`'s
own methods (`addOrder`, `clearOrders`, `setFireOrders`, `modifyBuildQueue`).

The two named are fixed. The debug pair cannot route through the simulation
until `spawnCompletedUnit` returns a `UnitId` rather than a reference, because
`UnitState` carries no id of its own. The eight method calls are not a leak at
all -- issuing an order is what a scene does when the player clicks -- but
`getUnit` cannot become const until someone decides whether `GameSimulation`
should own that path, which is what the issue actually asks for.

**#117 claims ~30 COB methods can move behind a `PieceController`.** `cob.cpp`
does reach 31 distinct `GameSimulation` members, but **18 of them have
consumers outside `src/rwe/sim/cob.cpp`** -- `unitDefinitions` in 28 other
files, `units` in 20, `terrain` in 19, `gameTime` in 18, `getUnitState` in 15,
`tryGetUnitState` in 14. Those are general sim access the VM needs and
everything else uses too. Only **13 are COB-exclusive** and can actually move.
`moveObject`, `turnObject`, `spinObject` and `getUnitPiecePosition` read like
COB plumbing and are not.

(Re-measured 2026-09-21 before posting the correction. An earlier pass of this
plan said 15 shared and 16 movable, and quoted consumer counts -- 76, 92, 42 --
that do not reproduce; they were whole-tree greps including tests. The
conclusion is unchanged and slightly stronger: about **13** methods can move,
not the ~30 advertised.)

So the achievable interface reduction is about half what is advertised, against
a seam the issue itself calls hypothetical, and it moves no rebuild cost: taking
method *declarations* out of `GameSimulation.h` does not change how many files
include it. On this plan's own rule -- do the work whose value survives
measurement -- #119 should go first.

Each is one pull request, behaviour unchanged, proved by diffing a fixed
replay's `RWE_HASH_LOG` before and after.

## Phase 3 -- finish the single source of truth

- [x] **Audited all four types, and found two real faults** 2026-09-21.
      The audit was the work; the fixes were four lines.

      | type | hash | save | dump |
      |---|---|---|---|
      | `MapFeature` | 8/8 | 8/8 | **0/8 -- no `dumpJson` existed** |
      | `Projectile` | 14 fields | 24/24 | **13 -- `edgeEffectiveness` missing** |
      | `GamePlayerInfo` | 28/32 | 32/32 | 28/32, the same 28 |
      | simulation-level | 8 members | -- | **7 -- `features` missing** |

      Hunting a desync is `RWE_HASH_LOG` for the tick then `RWE_STATE_DUMP`
      for the field, so the second step can only show what the dump walks.
      Feature state -- a burning tree's clock, a wreck one peer has reclaimed
      further, a corpse that sank on one side -- was hashed, saved, and
      entirely absent from the dump, so that whole class of desync arrived
      with an empty diff. `GamePlayerInfo` is clean: its hash and dump agree
      field for field and the four they both skip are deliberately saved
      rather than hashed.

      Three guards added, each checked by undoing the fix and watching it
      fail, because a guard that cannot fail is worth nothing. Tests 807 ->
      810.
- [ ] **#115's larger half is still open, and is now worth less.** The issue
      asks for one field list per type with the three walks derived from it,
      the way `UnitStateFieldTable` does for `UnitState`. What exists now is
      tests asserting the lists agree, not a structure making disagreement
      impossible. They would have caught both of today's faults, which no
      check did. Worth doing when a fifth hashed type appears; not worth it
      for the four that are now covered and guarded.

This is the class of bug that has cost the most debugging time in this project.
Both heap-dependent desyncs of 2026-09-18 were in it.

## Phase 4 -- the single-file tests and tools

- [x] **`AiBehaviour.test.cpp`** -- was 6,906 lines, 58 commits, 18,565
      sections. Done 2026-09-21, split seven ways by subject: economy stayed
      in the base file, and army, commander, defences, naval, perception and
      siting went beside it, 367 to 1,442 lines each. The four anonymous
      namespaces it carried are one `ai_test_util.h`; 17 of their 20 helpers
      were used by two or more subjects, and the three that were not stayed
      with their one user.

      **The section argument for this split was wrong, and that is the useful
      result.** Measured either side:

      | | before | after |
      |---|---|---|
      | longest file | 6,906 lines | 1,442 |
      | sections, worst object | 18,565 (56%) | 14,324 (44%) |
      | sections, all of them | 18,565 | **91,514** |
      | compile, serial | 13.8 s | **54.7 s** |
      | compile, critical path at `-j8` | 13.8 s | 8.5 s |

      An AI test translation unit has a floor of about 11,000 sections and
      6.5 s whatever is in it -- `perception`, eleven cases and 367 lines,
      costs 11,188 and 6.5 s -- so seven files pay that floor seven times.
      Sections went up **4.9x** and compiler time **4x** to take the peak down
      twelve points on a file that was never within reach of the ceiling. The
      whole-build budget did not move at all: it reports the worst object, and
      that is `GameSimulation.cpp` at 71%, untouched by any of this.

      What is actually bought is the 58 commits: one 6,906-line file that
      everything AI-shaped had to be edited into, now seven that can be edited
      independently, and editing one subject costs 8 s rather than 14. Take
      that as the reason next time, and do not quote sections at a test file.
- [x] **`makeFlatTerrain` was defined separately in 40 test files.** Done
      2026-09-21. Nine distinct bodies behind one name, and a bare
      `makeFlatTerrain()` meant six different map sizes -- 16, 32, 64, 128,
      256 and 1024 -- depending on the file. One `inline` definition in
      `sim_test_util.h`, without default arguments so the size cannot go
      back into hiding; 191 bare calls now name their map. Two helpers stayed
      local under names that say what is different (`makeDryTerrain` in
      TransportManager, the control for its channel map; `addFiringUnitOfType`
      in patrol), and the clash predicted above was real but small: three
      files, fixed by renaming their `addPlayer` and dropping two helpers
      identical to the shared ones. 255 insertions against 543 deletions
      across 41 files; 810 tests and 111,701 assertions before and after.
- [x] **`addWellStockedPlayer` was three copies, and the name was taken.**
      Done 2026-09-21. `aircraftbuild`, `nanoframedecay` and `repair` defined
      bodies identical apart from the player's name and side string (CORE in
      the first, ARM in the other two); one shared definition in
      `sim_test_util.h` and 21 call sites. The side is an argument rather than
      a default, because the three disagreed on it and the simulation reads it
      -- the AI's `EconomyManager` resolves its build tree through
      `player.side`. The name did not survive: nothing outside a log reads
      one. `saveload`'s helper of the same name held the shared 1,000 of each
      resource and differed only in storage, so it is
      `addPlayerWithLargeStores` now; renaming it was the fix, not sharing it.

- [ ] **`tad_episodes.cpp`** (#139) -- 6,533 lines. One file per mode over a shared
      reader. Low urgency: the tool is stable. Do it if it is touched again --
      and weigh it on its commit rate, not its section count, for the reason
      the entry above measures.

## Phase 5 -- what the documentation costs

- [x] **`CLAUDE.md` cut down.** Done 2026-09-21: 558 lines and 52,645 bytes
      to 287 and 32,082, a **39% cut by size**. What moved, and where:

      | out of CLAUDE.md | to | was |
      |---|---|---|
      | `tad_episodes` and the three `tad-*.py` scorers | `docs/TA-DEMOS.md`, "The tools, mode by mode" | 180 lines |
      | the annotated `TOTALA-EXE.md` section list | `docs/TOTALA-EXE.md`, as a real index | 58 lines |
      | the scope/purpose/other label lists | `docs/agents/labels.md` | 40 lines |
      | the two profiler references | `docs/PROFILING.md` | 3 paragraphs |

      Everything moved, nothing was deleted, and each left a pointer behind.
      What stayed is what the plan asked for: rules, hazards and an index --
      determinism, the COFF ceiling, the subscription traps, the conventions,
      and one line per tool.

      **The ~200-line target was the wrong measure and is not met.** This file
      has no column limit, so a "line" is a whole paragraph: the 133-line
      Matching-TA section was 12 KB and the 40-line label taxonomy 2.6 KB.
      Bytes are what gets loaded into a session, and bytes are down 39%.
      Cutting to 200 lines from here would mean deleting hazards, which is the
      opposite of the point.
- [x] **`TOTALA-EXE.md` is split by subject** (#140). Done 2026-09-21.
      14,174 lines into ten subject files of 432 to 2,396 lines, with the
      parent keeping the full index, §88 and §91. **The section numbers did
      not move** -- 109 findings numbered to 111, no §83, no §84 -- so every
      `§n` in the tree, the commit log and the other documents still names the
      same finding; the index says which file holds it. 216 links checked,
      none broken; the section bodies are 13,914 lines before and after.

      **It was justified on churn, which is the rule the AiBehaviour split
      left behind.** `TOTALA-EXE.md` takes **153 commits in thirty days**, the
      most of any file in this tree -- ahead of `CLAUDE.md` at 98 and
      `BuildManager.cpp` at 62. Size alone would not have been a reason.

      The measuring turned up something worth more than the split: **§53, §54
      and §58 carried a finding that `TOTALA-EXE-SHADING.md` had overturned,
      and said nothing about it.** All three are marked now, and the two worst
      are flagged in the index.

## Phase 6 -- tracker hygiene

- [x] **Every open issue carries a purpose.** Done 2026-09-21. The figures
      above were wrong: counted on the day, of 34 open issues **20 lacked a
      scope, 13 lacked a purpose and 12 lacked both** -- not 12, 11 and 3.
      With the ten opened for this batch, 44 are open and all 44 now have
      exactly one `purpose:`.

      **Three deliberately have no scope**, because none of the nine fits:
      #75 (Security Audit, spans everything), #44 (multiplayer reconnect --
      engine networking) and #8 (map-pack robustness -- the file parsers).
      CLAUDE.md already allows this: purpose takes exactly one, scope "one
      where one fits". Those last two point at a real gap in the axis, which
      names no scope for the network layer or for `io/`, though `io` exists as
      a plain label. Worth adding `scope:io` and something for networking if a
      third case turns up.
- [x] **Phase 2 is a milestone**, "Maintainability: Phase 2, the churn hot
      spots", carrying #116, #117, #118 and #119. Done 2026-09-21.
- [x] **The corrections are posted** on #116, #117 and #119, each re-measured
      against the tree first. Two of the three figures this plan recorded did
      not survive that check; see below.

## Found along the way, now tracked

None of these were on the roadmap; each turned up while doing something else
on it, was measured, and is now an issue rather than a note in this file.

- **#135** `GameSimulation.h` is the only file of 733 in `src/` stored CRLF in
  the index. It has already cost one 3,006-line phantom diff on a four-line
  change. Wants a `.gitattributes`.
- **#136** `makeChannelTerrain` is two copies with different channel widths,
  and the comment explaining the duplication describes a constraint that no
  longer holds. May well want to stay two, under names that say what differs.
- **#137** `docs/adr/` is named by `CLAUDE.md` and five times by
  `docs/agents/domain.md`, and does not exist. It was the only path in
  `CLAUDE.md` that did not resolve.
- **#138** The scope axis names nothing for `src/rwe/io/` or the network layer,
  which is why #8 and #44 carry no scope. Two is not yet a pattern; filed so
  the third has somewhere to land.

The ten issues for the work in this batch are #125 to #134.

## What is deliberately partial

**`-Werror` is on for MinGW64 only.** The tree reached zero warnings on MinGW64
gcc 16.2, which is the toolchain this machine builds with and the one whose
linker broke over the section ceiling. gcc-14, clang-18 and MSVC each warn about
their own things and have not had the same pass. Turning `-Werror` on for all of
them at once would break CI over warnings nobody has read yet, so the option
exists and the other jobs opt in when someone has done that reading. An
unconditional `-Werror` also breaks a contributor's local build the day their
compiler updates, which is why the default is OFF.

## Two hazards this work turned up

**MSYS2 `sed -i` flattens CRLF to LF on any in-place write**, even for a
line-addressed substitution that never anchors to `$`. It silently rewrites
every line ending in the file. Use a Python read/modify/write in binary mode,
detecting and restoring the ending, or an editor that preserves it. Checking the
result with `grep -c $'\r'` does not work either: in some shells the escape does
not expand, the pattern becomes empty, and every line matches, so a broken file
reports itself clean.

**`src/rwe/sim/GameSimulation.h` is the only file in the repository stored with
CRLF in git** -- 768 of 769 tracked files under `src/`, `tools/`, `docs/` and
the workflows are LF (`git ls-files --eol`). That one file therefore shows a
3,006-line whole-file diff the moment anything normalises it, which is what
makes the `sed` hazard above expensive rather than merely untidy. It should be
normalised to LF, but **not until #124 lands**, because #124 rewrites that
header and the two changes would conflict over every line of it.

## Noticed while fixing warnings, not acted on

`GameSimulation::doProjectileImpact` takes an `ImpactType` and never reads it,
while its callers take care to pass `ImpactType::Water` at one site and `Normal`
at eight others. The sim therefore ignores a distinction its callers bother to
make -- including running `tryIgniteFeaturesInRadius` for a round that landed in
water. Whether that matches the original is a question for `TOTALA-EXE.md`, not
for a warnings pass, so the parameter is commented out rather than removed: 21
call sites name it, and deleting it would be a behaviour question wearing a
refactor's clothes.

## Deliberately not doing

- **More mechanical cuts to `GameSimulation.cpp`.** Measured above: about two
  days of headroom for a day's work. Phase 2 addresses the rate instead.
- **Unity builds.** They would hit the section ceiling on contact.
- **Cleaning anything that is not churning.** `GraphicsContext.cpp` is 954 lines
  and nobody has touched it. Leave it alone.
