# Continuation brief: working the RWE issue backlog

Written 2026-10-18 for an agent picking up the open issues on CubeB/RWE-B4. Read
this whole file first, then `CLAUDE.md` and `AGENTS.md` at the repo root. Those
two are the standing project instructions; where this brief and they disagree,
they win.

## 1. What the project is

Robot War Engine (RWE) is a C++20 engine that plays Total Annihilation's data
files. Most current work is **fidelity**: making behaviours the engine already
has match what the original `TotalA.exe` does, down to the arithmetic.

- Repo: `D:\RWE`, working branch `revival` (the default on GitHub).
- Remotes: `b4` is the owner's fork, CubeB/RWE-B4, where pushes go. `origin`
  is upstream and is never pushed to.
- Issues live in GitHub Issues on CubeB/RWE-B4 and are managed with `gh`.
  Every issue carries two labels: one `scope:*` and one `purpose:*`. See
  `docs/agents/labels.md`.

## 2. Hard rules (the owner has set these; do not bend them)

1. **Never `git push` unless the owner asks for it in that message.** Commit
   locally. Merge to `revival` only when asked.
2. **Never stage the owner's uncommitted files.** They are: `CLAUDE.md`,
   `AGENTS.md`, `docs/TOTALA-EXE*.md`, `docs/adr/0004-*`,
   `docs/REVERSE-ENGINEERING-PRIORITIES.md`,
   `docs/NAVAL-CONSTRUCTION-DGUN-FINDINGS.md`, `_mission_export_stage/` and
   `ta-gui-*.md`. Stage with explicit paths. Never use `git add -A` or `git add .`.
3. **Spec-room boundary (ADR-0004).** Implementation work never opens
   `D:\bytetactics`, which holds the decompilation. Rulings on how the
   original behaves come only as prose, from
   `D:\RWE-spec-room\docs\BYTE-TACTICS-SPEC-ROOM-FINDINGS.md` and
   `BYTE-TACTICS-DISCREPANCIES.md` on branch `spec-room-findings`. That branch
   is not merged; read the files from that worktree. Rows BT-1 to BT-253 are
   all resolved. A new question about the original goes to a separate
   docs-only spec-room session, which writes a new BT row. Code never quotes
   decompiled code.
4. **The owner's play-test beats any reading of the binary.** If the owner
   says the original does X, X is the spec, even against a BT ruling. Example:
   BT-185's target ring was removed on 2026-10-18 because the owner saw it is
   not in the original.
5. **Do not run the TaChecksums test** unless explicitly told to.
6. **Never name Zero-K** in commits, issues, PRs or repo docs.
7. **No claude.ai artifact links.** The owner works in a terminal. Deliver
   things as local files or in-engine.
8. **Do not launch the game for screenshots** while other agents are running.
   The owner does the visual checks. Headless probes are fine: `ui_probe`,
   `scenario`, `ai_arena`, `solar_probe`.
9. **Never edit a source file while a `make` is running** in either tree. The
   result is a binary with mixed struct layouts that make believes is current.
10. **Commit messages:** one imperative sentence as the subject, then a short
    body that explains why. Cite the BT row or issue, and use `Fixes #n` to
    auto-close the issue on push. End with the co-author line your harness
    specifies.

## 3. Building and testing (Windows, MSYS2/MinGW64)

Two trees: `build/` (Debug) and `build-release/` (Release, which the owner
play-tests as `build-release\rwe.exe`).

Put a script like this in a scratch directory and run it with
`/d/msys64/usr/bin/bash.exe --login -c "bash <script>"`:

```bash
export USERPROFILE="C:\Users\Baile" HOME=/c/Users/Baile MSYSTEM=MINGW64 CCACHE_DIR=/d/tools/ccache
export PATH=/mingw64/bin:/usr/bin:$PATH
set -e
cd /d/RWE/build
make -j6 rwe          # always build the game itself first, alone
make -j6 rwe_test
make -j6 -k all       # REQUIRED before any push: probes outside rwe/rwe_test once broke every CI job
./rwe_test            # full suite; last count 1799 cases, 1798 pass, 1 skipped (TaChecksums)
cd /d/RWE/build-release && make -j6 rwe   # so the owner can play-test
```

- Name one target per `make` call. Several goals in one parallel make can
  leave `rwe.exe` stale.
- A make that was killed mid-compile can leave a truncated `.obj` behind. The
  sign is masses of undefined references from one file. Delete that object
  and rebuild.
- **Section budget:** `tools/section-budget.sh build 75` has a limit of 24575.
  `GameScene_commands.cpp` (~24319) and `GameScene.cpp` (~24280) are at the
  ceiling. Put new code in new or other translation units, and do not add
  members to `GameScene.h` casually. A `BehaviorSubject` declared there costs
  sections in every file that includes it.
- **Determinism:** new simulation state goes in the struct and also in
  `save_util.cpp`, `GameHash_util.cpp` and `dump_util.cpp`, plus
  `UnitStateFieldTable` for unit state. Check by running `ai_arena` seed 7
  twice with `RWE_HASH_LOG` and diffing the two logs. No
  `std::uniform_int_distribution` and no libm trig in the simulation (use
  `det_math`). Presentation code never touches `simulation.rng`.
- **Test helpers** are in `src/rwe/sim/sim_test_util.h`. A unit's
  `CobEnvironment` holds a raw pointer to its script. `addUnitOfType` now
  keeps every script alive, but a test that builds a CobEnvironment itself
  must keep its script alive for at least as long as the units that use it.
  Temporaries crashed MSVC Debug twice.
- **CI** (GitHub Actions) runs Linux gcc and clang, MSVC and MinGW, in Debug
  and Release. A docs-only push is skipped. The nightly pre-release is
  published from `revival` and does not wait for MSVC Debug. After a push,
  run `gh run watch <id> --repo CubeB/RWE-B4 --exit-status`, then check that
  `gh release list` shows `Nightly <sha>`.

## 4. How a fidelity issue gets done

1. Read the issue. If it cites a BT row, read that row in the spec-room files.
   The ruling is the spec.
2. Read the relevant `docs/TOTALA-EXE*.md` section through the index in
   `docs/TOTALA-EXE.md`. Section numbers never move. §88 lists deliberate
   divergences; §91 lists what is decoded but not ported.
3. Branch from `revival` as `<issue>-<slug>`, or batch related issues on one
   branch with one commit per issue.
4. Implement, with a Catch2 test next to the source
   (`src/rwe/<subsystem>/<X>.test.cpp`). Use shipped unit data in fixtures,
   never invented numbers. `tools/hpi.py <archive> units/x.fbi --cat` reads a
   shipped FBI.
5. Build all, run the full suite, and rebuild the release `rwe`.
6. If the change is hash-visible, say so in the commit body. Old replays will
   diverge.
7. If the change departs from the original on purpose, label it
   `purpose:divergence` and add an entry to §88 and `docs/compatibility.md`.
   Those docs belong to the owner, so draft the text for approval.
8. Report to the owner what changed, what to check in play, and the test
   counts. Wait for "push" before pushing.

## 5. The backlog, in priority order

State as of 2026-10-18, `revival` at `13a83124`, nightly green. There are about
140 open issues; `gh issue list --repo CubeB/RWE-B4 --state open --limit 200`
lists them.

### A. Owner decisions already made: code these first
- **#706 BT-232:** remove a dropped player's units, as the original does.
  This is hash-visible. Then update §88, which currently lists it as a
  divergence, as a drafted edit for the owner.
- **#701 BT-103:** grey out mission-excluded build entries, as the original
  does. This reverses #381.
- **#655:** RWE hides `camps/EXAMPLE.TDF` from the campaign list.

### B. Rows the owner approved for scheduling
- #681 (BT-44, BT-49): burning features
- #682 (BT-47): blast reach on a multi-cell feature, measured from its centre
- #683 (BT-48): cap on live stateful features
- #684 (BT-51): yard letters mapped to the placement mask
- #685 (BT-52): clearance strips
- #687 (BT-64): floater taken from its key alone

#686 was closed as not planned.

### C. Presentation batch B
- #574: explosion smoke
- #578: lava explosions
- #583: positional sound gated by fog
- #584: unknown sound category
- #585: message cooldowns
- #606: animated model textures
- #607: radar picture header
- #616, #617: score panel
- #618: music evaluator order
- #635: label font
- #636: briefing blink
- #637: message box recentre
- #639: dialog fade
- #723: laser weapon effects, a new owner issue. Ask the owner what is wrong
  with them.

A note on **#595** (projectiles on the minimap): unfinished work sits on
branch `wip-595-minimap-projectiles` (7b0f3489). It is untested and touches
`GameScene.h`, so watch the section budget.

### D. GUI fidelity and the front end
- #670: label alignment
- #671: gadgets with active=0
- #672: quick keys and default buttons
- #673: list frame
- #674: message box wrap and Return
- #675: text-box editing
- #676: labels that swallow clicks
- #677: radio buttons that start pressed
- #644: the options page cannot set the interface style
- #652: the loading bars never move
- #653, #654: camera and selection keys
- #659, #660, #661: save name, damaged save and false "Game saved"
- #669: scroll-bar divide by zero
- #678: `+Give` counts players from 0, as the original does

### E. Data robustness
- #580, #581, #582: TDF parsing rules
- #608: lenient side data
- #646: negative model height
- #651: duplicate feature files
- #647, #648: launcher archive hashing and order. Keep `common/archives.ts`
  in step with `CompositeVirtualFileSystem.cpp`.

### F. Game modes
- #699: Deathmatch and commander respawn
- #700: continue watching after elimination
- #38: campaign
- #383: mission AI profiles

### G. Older gameplay bugs
- #406, #407: fighter strafing pass and maneuver leash. See
  `docs/TOTALA-EXE-MISSIONS.md`.
- #408: Standby units never chase
- #409: self-destruct ladder
- #443, #444, #455: carriers
- #193: a passenger wades out to meet its transport
- #346: stop noise at path corners
- #348: auto-target scan

### H. AI (RWE's own AI and the classic AI)
- Transports and water maps: #26, #195, #202, #227
- Holding ground and army composition: #446 to #450
- Follow-ups: #679 (bombers for the patrol), #680 (+1 metal scoring)

Arena runs that mean RWE's AI must pass `--ai-engine rwe`. Use
`tools/arena-paired.py` to compare.

### I. TA-host (real TotalA.exe joining RWE)
- #422 to #431 are waiting on the owner's real-TA tests: start-up pause, the
  21-byte/0x50 admission, 0x1e/0x1f sync, the 0x20 flag, and UDP and TCP
  arriving from different addresses.
- #471: map checksum
- #474: stop-and-go movement
- #630 was deliberately skipped.
- #704, #709, #713 are optional.

TA peer identity uses `sameCarrierBlock` (/16), **never the exact IP**.

### J. Docs only
- #707, #710: record two divergences in §88 and `compatibility.md`. Draft the
  edits for the owner to approve.
- Leftover doc fixes to draft:
  - the KEYBOARD §78 note on dead keys d and v
  - TA-DEMOS 0x19 saying "RWE has no pause"
  - §88 listing Give as unmatched
  - INTERFACE saying "RWE has no resurrect order"

### K. Owner-only checks (do not attempt these)
- #688 to #692: checks in the original game
- Play-test items, waiting on the owner:
  - enemy buildings appearing in the fog (fixed by making the renderer
    stricter; the cause was never reproduced)
  - the building hit area looking bigger than the original's (BT-253 says the
    regions are identical)
  - the underwater tint colours (yellow parts come out mauve)

### L. Optional and tooling
- Optional BT rows: #567, #568, #576, #577, #596, #598, #601, #602, #634,
  #638, #645
- Demo scorers: #146 to #151
- CI arena runs: #254
- Section headroom: #396, #403

## 6. Known open oddities noticed in play

- Faint "Repeat Off" ghost text appears in the interface. It predates this
  work.
- F10 did not open the debug window during the last visual test.
- The game ignores posted mouse messages, so the off-screen hover and click
  phases of `tools/visual-test.ps1` cannot work.

## 7. Where things are recorded

- Findings index: `docs/TOTALA-EXE.md`. Plain-language differences:
  `docs/compatibility.md`.
- Parity audits, one per byte-tactics module: `docs/BT-*-PARITY.md` (75),
  plus `NAVAL-SYSTEMS-PARITY.md` and `TA-SYSTEMS-PARITY.md`.
- Agent workflow notes: `docs/agents/orchestration-workflow.md`.
- Usage guard: if you run under usage limits, pause all work at 80% of the
  5-hour window or 72% weekly. Between 75–80% (5-hour) or 68–72% (weekly),
  finish what is running and start nothing new.
- Concurrency: at most 3 agents at once (2 coding, 1 spec room), and only one
  of them building at a time.
