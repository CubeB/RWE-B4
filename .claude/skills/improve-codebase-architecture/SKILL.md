---
name: improve-codebase-architecture
description: Scan a codebase for deepening opportunities, present them as a visual HTML report, then execute whichever one you pick autonomously — recording decisions as you go so an intent stack can be generated at review time.
disable-model-invocation: true
---

# Improve Codebase Architecture

Surface architectural friction and propose **deepening opportunities**: refactors that turn shallow modules into deep ones. The aim is testability and AI-navigability.

This command is _informed_ by the project's domain model and built on a shared design vocabulary:

- Call the Skill tool with "codebase-design" for the architecture vocabulary (**module**, **interface**, **depth**, **seam**, **adapter**, **leverage**, **locality**) and its principles (the deletion test, "the interface is the test surface", "one adapter = hypothetical seam, two = real"). Use these terms exactly in every suggestion, and don't drift into "component," "service," "API," or "boundary."
- The domain language in `CONTEXT.md` gives names to good seams; ADRs in `docs/adr/` record decisions this command should not re-litigate.

## Process

### 1. Explore

**Scope before you scan: YAGNI.** Deepening a module pays off by making future changes to it easier, so put extra weight on the parts of the codebase that have recently changed. Decide *where* to look before you look:

- If the user named a direction (a module, a subsystem, a pain point), take it, and skip the inference below.
- Otherwise, walk back a good stretch of the commit history (`git log --oneline`) to find the codebase's hot spots, the files and areas that keep coming up, and let those paths pull your attention first. If the changes are scattered with no clear hot spot, widen the net.

Read the project's domain glossary (`CONTEXT.md`) and any ADRs in the area you're touching first.

Then spawn a sub-agent to walk the codebase. Don't follow rigid heuristics; explore organically and note where you experience friction:

- Where does understanding one concept require bouncing between many small modules?
- Where are modules **shallow**, with an interface nearly as complex as the implementation?
- Where have pure functions been extracted just for testability, but the real bugs hide in how they're called (no **locality**)?
- Where do tightly-coupled modules leak across their seams?
- Which parts of the codebase are untested, or hard to test through their current interface?

Apply the **deletion test** to anything you suspect is shallow: would deleting it concentrate complexity, or just move it? A "yes, concentrates" is the signal you want.

### 2. Present candidates as an HTML report

Write a self-contained HTML file to the OS temp directory so nothing lands in the repo. Resolve the temp dir from `$TMPDIR`, falling back to `/tmp` (or `%TEMP%` on Windows), and write to `<tmpdir>/architecture-review-<timestamp>.html` so each run gets a fresh file. Open it for the user (`xdg-open <path>` on Linux, `open <path>` on macOS, `start <path>` on Windows) and tell them the absolute path.

The report uses **Tailwind via CDN** for layout and styling, and **Mermaid via CDN** for diagrams where a graph/flow/sequence reliably communicates the structure. Each candidate gets a **before/after visualisation**. See [HTML-REPORT.md](HTML-REPORT.md) for the full scaffold, diagram patterns, and styling guidance.

For each candidate, render a card with: **Files**, **Problem**, **Solution**, **Benefits** (in locality/leverage terms), **Before/After diagram**, and **Recommendation strength** (`Strong`, `Worth exploring`, `Speculative`). End with a **Top recommendation** section.

**Use CONTEXT.md vocabulary for the domain, and the `/codebase-design` vocabulary for the architecture.**

**ADR conflicts**: if a candidate contradicts an existing ADR, only surface it when the friction is real enough to warrant revisiting the ADR. Mark it clearly in the card. Don't list every theoretical refactor an ADR forbids.

After the file is written, ask the user: "Which of these would you like to execute?" Also offer **"execute all Strong recommendations"** as a choice — this skill is designed for hands-off execution.

### 3. Autonomous execution

This repo is not code-reviewed by humans reading every line: the process here is **generate autonomously, review the results**. Once a candidate is picked, do not grill the user through implementation decisions. Go with your instinct.

Run the work on a branch (`git checkout -b <descriptive-name>` if not already on one). Then:

1. **Read before writing.** Read the findings in `CLAUDE.md` ("Matching Total Annihilation", "Determinism", "Other hazards") before touching anything TA-facing, sim, or hashed state. A plausible refactor of the sim can be a desync; a tidy move of hashed state out of the hash is a bug nothing will catch at compile time. If a candidate would violate one of those rules, stop and report rather than working around them.
2. **Work in one direction.** Pick the design the exploration argued for and carry it through. Don't stall on alternatives, don't design-it-twice, don't come back with questions. The reviewer's job is to disagree; your job is to have decided.
3. **Record every decision as you make it.** This is the contract that makes the whole thing reviewable — see "The decision log" below.
4. **Verify.** Run the test suite for whatever you touched (`./build/rwe_test` and the relevant launcher/npm tests; rebuild the `rwe` target too — a green `rwe_test` says nothing about whether the game still links). Run `npm run tsc && npm run lint` in `launcher/` for launcher changes. Fix what fails; do not weaken a test to make it pass — if a test encodes a behaviour the deepening genuinely changes, that goes in the decision log and the reviewer adjudicates.
5. **When done, stop and report.** Summarise what changed in a few lines and tell the user the intent stack is ready to generate (`/intent-stack`) whenever they want it, or that it can be generated straight onto the PR once one is open.

**Mid-execution discoveries.** If the refactor uncovers something that genuinely cannot proceed without a human (a contradiction between two ADRs, a finding that the candidate's premise was wrong, a determinism hazard with no safe reading), stop there, record the blocker in the decision log, and report. Everything short of that is yours to decide.

**Domain model side effects.** If execution names a new module after a concept not in `CONTEXT.md`, or sharpens a fuzzy term, update `CONTEXT.md` inline as you go (the domain-modeling discipline). Don't batch it.

### The decision log

While executing, append to `.intent-log.md` in the repo root the moment a decision crystallises — not at the end, when the reasoning has already evaporated. One line per decision:

```markdown
- <what was decided, one short line> [<provenance>]
```

Provenance tags are the intent-stack categories:

- `[USER]` — the user asked for this directly, in this session or the one that picked the candidate.
- `[[DESIGN](URL)]` — a design document the user supplied or linked.
- `[USER_APPROVED]` — you proposed this narrow choice and the user approved exactly it.
- `[CODEBASE]` — follows an established repository pattern; you checked at least two comparable precedents or an explicit repo instruction (CLAUDE.md, CONTEXT.md, an ADR).
- `[AGENT]` — you decided it. This is the default tag for autonomous execution, and it is not a defect: the point of this process is that your instinct is trusted. Tag it honestly and move on.
- `[UNEXPLAINED]` — never used while executing. It exists for post-hoc reconstruction; if you made the decision, it is `[AGENT]`.

Rules:

- One line per **decision**, not per edit. "Move capture progress onto the order queue" is one line covering forty edited lines.
- Preserve negative constraints when they materially characterise the intent: "keep `simulation.rng` untouched by presentation code", "do not reorder the hash fields".
- Log the *reason* when it is not readable from the change itself: `Split GameScene._menu out before it needs bigobj — 13105 sections against a 32767 ceiling [AGENT]`.
- Do not log abandoned paths that never survived into the change.
- Do not log routine consequences (every call-site import update for a rename). Log the decision, not its echo.
- If the user is in the conversation and approves something specific mid-run, record it as `[USER_APPROVED]` at that moment — post-hoc approval is not this tag.

The log file is a working artifact, not a deliverable: it is an input to the intent-stack skill, which renders it into the forest a reviewer actually reads. Commit it with the branch so provenance survives if the session is compacted or handed off. Delete it (or leave it — reviewer's choice) when the intent stack has been generated.

### 4. At review time

Once a PR is open, call the Skill tool with "intent-stack". It reads `.intent-log.md`, the diff and the surviving context, and produces the forest a reviewer walks from broad intent down to exact diff lines — flagging anything `[UNEXPLAINED]`, which is the signal for "the log lost a decision; reconstruct and scrutinise".
