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
- [x] **`tools/syntax-check.sh`** -- compiles one translation unit for
      diagnostics only, out of `compile_commands.json`, writing nothing. It
      exists because `make` is not safe to run while anything is editing
      sources, so this is how a fix gets checked without waiting for a build.

`-Werror` justified itself before it was even committed: it caught two
`unused-but-set-variable` sites in `AirTactics.test.cpp` that the warning
inventory had miscounted and nobody had looked at.

## Phase 1 -- stop the rebuild storms

**Most of this is already in flight as PR #124** ("Break GameSimulation.h's
transitive grip"), which moves `GamePlayerInfo`, `UnitInfo`/`ConstUnitInfo` and
`ImpactType` to leaf headers and forward-declares the rest. It reports
transitive parsers down from 137 to 120. It is **CONFLICTING against `revival`**
and needs a rebase before it can land.

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
- [ ] **`UnitState.h`** -- 36 commits, 88 includers, 3,168 recompiles a month,
      and no PR touches it. The best remaining target once #124 lands.
- [ ] **`UnitDefinition.h`** -- 22 commits, 88 includers. Same treatment.

### Corrected: `AiTuningProfile.h` is not a target

An earlier draft of this plan said its profile *values* should move to a `.cpp`
so that tuning a knob rebuilt one object. That was wrong, and the measurement
says so: the file is 242 member declarations and **1,635 comment lines**, 84%
documentation. There are no values to move -- the defaults are the members --
and the comments record the measurements behind each knob, which is exactly
where they belong. Its fan-in is 21, the lowest of the four. Left alone.

## Phase 2 -- give new code somewhere else to land

Two of these are already open and **both merge cleanly**:

- [ ] **PR #123** -- draws an `AirMovement` module around the aircraft state
      machine, out of `UnitBehaviorService.cpp`. This is issue #118. Clean.
- [ ] **PR #122** -- carves the resource settle into a `ResourceSettler`
      module, out of `GameSimulation`. Clean.
- [ ] **`BuildManager::update` by phase** -- 1,612 lines in one function,
      following the pattern #121 set for the walks. No PR.
- [ ] **#117 PieceController** -- ~30 methods off `GameSimulation`'s interface.
- [ ] **#119** -- a perception interface for the AI read side.
- [ ] **#116** -- close the non-const escape hatch into `UnitState`.

Each is one pull request, behaviour unchanged, proved by diffing a fixed
replay's `RWE_HASH_LOG` before and after.

## Phase 3 -- finish the single source of truth

- [ ] **#115, the compile-time half.** `UnitStateFieldTable` covers
      `UnitState`. Extend the same one-row-per-field treatment to `MapFeature`,
      `GamePlayerInfo`, projectiles and simulation-level state, so the dump
      guard test added 2026-09-21 covers all four rather than one.

This is the class of bug that has cost the most debugging time in this project.
Both heap-dependent desyncs of 2026-09-18 were in it.

## Phase 4 -- the single-file tests and tools

- [ ] **`AiBehaviour.test.cpp`** -- 6,912 lines, 53 commits, 18,541 sections
      (56% of the ceiling). Split per manager.
- [ ] **`tad_episodes.cpp`** -- 6,533 lines. One file per mode over a shared
      reader. Low urgency: the tool is stable. Do it if it is touched again.

## Phase 5 -- what the documentation costs

- [ ] **`CLAUDE.md` to ~200 lines.** It is 556 now and is loaded into every
      session and every agent brief. It should be rules, hazards and an index.
      The `tad_episodes` mode reference is a third of it and belongs in
      `docs/TA-DEMOS.md`, which already paraphrases it.
- [ ] **Split `TOTALA-EXE.md` by subject**, keeping the section numbers stable
      and adding a numbered index. 14,057 lines cannot be read whole.

## Phase 6 -- tracker hygiene

- [ ] 34 issues open. Eleven lack `purpose:`, twelve lack `scope:`; #55, #58 and
      #75 carry neither. Apply the pairs.
- [ ] Put Phase 2 on a milestone, so the order lives on GitHub and not in a chat
      log.

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
