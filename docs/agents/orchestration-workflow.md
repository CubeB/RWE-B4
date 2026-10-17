# Orchestrated agent workflow

How the 2026-10-16 sessions ran: one orchestrating session that plans, reviews and
reports, and Sonnet subagents that do the reading, coding, building and
measuring. It sits beside `AGENTS.md` and ADR-0004. Neither of those changes;
this is how work is carried out inside them.

## Roles

| Role | Model | Count | Does | Never does |
| --- | --- | --- | --- | --- |
| Orchestrator (main session) | Opus | 1 | Plans, writes briefs, reviews reports, checks claims that look wrong, talks to the user | Bulk reading, coding, building |
| General agent | Sonnet | up to 2 | Coding, tests, builds, arena runs, read-only research, docs-only parity audits | Opens the decompilation |
| Spec-room agent | Sonnet | 1 | Reads byte-tactics `src/`, settles discrepancies, writes prose findings | Writes or edits RWE code or tests, ever |

**Limit:** at most three agents at once, all Sonnet: two general and one spec room.

**One builder at a time:** only one agent may build in `build/` and
`build-release/`. Two agents building in a shared tree compile each other's
half-written edits and report each other's failures. Give the second general
slot to work that does not build: research, audits, docs.

## Every brief starts with its task type

The first line of a brief is `TASK TYPE:`, followed by one of these:

- CODING
- DEBUGGING + CODING
- TESTING / MEASUREMENT
- READ-ONLY RESEARCH
- DOCUMENTATION + READ-ONLY CODE AUDIT
- SPEC-ROOM DOCUMENTATION

Then come four things:

1. **Hard limits:**
   - what it must not touch
   - no pushing
   - no game launch (headless `ai_arena` is fine)
   - never stage `CLAUDE.md`, `AGENTS.md` or the owner's uncommitted docs
   - for implementation agents: never open `D:\bytetactics`
2. **The branch** to work on, and how to start it, for example
   `git switch -c <name> revival`.
3. **The spec**: the finding sections to read, and only those. Give context as
   file:line pointers that the agent must verify, not as conclusions to trust.
4. **The report**: what it must contain and how long it may be. Ask for the
   test-suite pass line verbatim, commit hashes, and anything that disagreed
   with the brief.

Build recipe for MinGW, from the Bash tool: write a script into the scratchpad and
run it with `/d/msys64/usr/bin/bash.exe --login -c "bash <script>"`. The script
exports `USERPROFILE`, `HOME`, `MSYSTEM=MINGW64`, `CCACHE_DIR=/d/tools/ccache`
and `PATH`. Build one target per `make` call, `rwe` first, then `rwe_test` or
`ai_arena`, with `-j6`. Never edit a file while `make` runs. Finish with a full
`./rwe_test` and `tools/section-budget.sh build 75`.

## From byte-tactics prose to RWE code

```
byte-tactics prose chapter/module (no code)
   │  general agent: DOCUMENTATION + READ-ONLY CODE AUDIT
   ▼
docs/BT-<MODULE>-PARITY.md   (+ conflicts appended to docs/BT-NEW-CONFLICTS.md)
   │  spec-room agent
   ▼
D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md   (register, BT-n rows)
D:\RWE-spec-room\docs\BYTE-TACTICS-SPEC-ROOM-FINDINGS.md   (rulings)
   │  general agent: CODING, citing the BT row
   ▼
commits on a conformance branch (e.g. bt-sim-conformance)
```

1. **Parity audit, by a general agent.** It reads only the prose docs under
   `D:\bytetactics\.worktrees\naval-reference\docs\`, never `src/`, and does
   not follow source links.
   - It writes one `docs/BT-<MODULE>-PARITY.md`, with this structure:
     - a header giving the reference commit and the evidence class
     - one section per rule, giving the reference rule in prose and RWE's
       status (MATCHES, DIFFERS, MISSING, UNCLEAR or a documented DIVERGENCE)
       with file:line
     - a ranked "Gaps to fix" table at the end
   - Every place the reference disagrees with an existing RWE finding is
     appended to `docs/BT-NEW-CONFLICTS.md` under a dated batch heading.
   - Audits record conflicts and never resolve them.
2. **Register, by the spec room.** Each conflict, and each gap that needs a
   reading, becomes a row with a stable ID (`BT-n`, never renumbered). The row
   gives:
   - its kind: CONFLICT, GAP or DIVERGENCE
   - what settles it: a spec-room read, an original-game check, or an owner
     observation
   - its status, and a link to the resolution
3. **Ruling, by the spec room.** It reads the routines at the addresses the
   prose gives and writes a prose finding:
   - the answer, with addresses, constants and arithmetic
   - the evidence
   - its confidence
   - what remains uncertain
   - whether an original-game check is still worth doing
   - whether a code change is implied, and whether that change is hash-visible

   **As soon as a row's reading is complete, mark it resolved with a link.**
   This is a standing instruction, and rows are never batched up first. The
   spec room works in its own docs checkout, `D:\RWE-spec-room`, on branch
   `spec-room-findings`. It commits only `docs/BYTE-TACTICS-*.md`, every two or
   three rows.
4. **Implementation, by a general agent.** It cites the BT row in the commit
   message ("static byte-tactics reading at <commit>, not executed against the
   original"). It reads the finding text and never the decompilation.
   - Hash-visible changes follow CLAUDE.md "Determinism": new state goes into
     save, hash and dump.
   - Every change needs a same-seed `RWE_HASH_LOG` determinism run.
   - It runs the section budget, because `GameScene_commands.cpp` is close to
     its limit.
5. **Changes that break replays need the owner's decision first**, for example
   arrival radius, retrying an unreachable Move, or the aim point. For each, ask
   whether to match the original or record a divergence in §88.

What the owner observes in the original game outranks every static reading. A
row that needs an original-game check stays open, and the question goes to the
owner in plain terms.

## Checking agents' claims

- **Verify a surprising result before building on it.** "Identical hashes
  across two commits" turned out to compare two runs that already contained the
  change. The cure was a three-way rebuild: before, after, and HEAD.
- **Prove the fix in play, not only in a unit test.** The ferry fix passed its
  tests. Arena runs showed ships still never unloading. The real cause was
  queued unload orders cancelling each other in pairs, and only a state dump
  around the stall showed it.
- **Commit one agent's work before the next agent touches the same files.**
  This keeps the work of separate agents in separate commits.
- **Judge a test result only from a quiet tree.**

## Scheduled jobs (session-only, recreate after a restart)

- **Usage guard, every 10 minutes.**
  - The status line script `~/.claude/usage-statusline.ps1` writes the
    `rate_limits` figures to `~/.claude/usage.json`. It is configured in
    `~/.claude/settings.json`. The path must use forward slashes, because Git
    Bash strips backslashes from the command.
  - At 5-hour usage of 80% or more, or weekly usage of 72% or more, against a
    75% weekly ceiling:
    - stop every agent with TaskStop
    - kill any leftover `make` or `cc1plus`
    - schedule a one-off resume at the matching reset time
  - At 5-hour 75-80% or weekly 68-72%: start nothing new.
- **Roadmap check, hourly.**
  - It compares byte-tactics' `doc-writeup-roadmap.md` and the `systems/`
    listing against snapshots in the scratchpad.
  - Newly finished modules go out as parity audits, within the agent limit.
  - New conflicts go to the spec room.

After a restart, check with CronList for duplicate jobs left by the old session.

## Restart hand-off

Before a planned restart:
1. Stop all agents.
2. Check that no build is running.
3. Write a memory note covering:
   - the branches and their commits
   - any half-done uncommitted work
   - the queue
   - the cron jobs to recreate

To resume, read that note, recreate the jobs, and restart the agents with briefs
that review the half-done work before continuing it.

## Git

Commit locally, one commit per logical change. Never push, and never merge into
`revival`, without asking the owner. Use one branch per stream of work: an AI
fix branch, a conformance branch, and the spec-room docs branch.
