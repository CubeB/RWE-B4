---
name: playtest-scan
description: Scan and triage RWE playtest findings from the arena harness — read findings.jsonl and the raw artifacts, confirm/reject/escalate each finding, dedupe against open issues, and file bugs with inline evidence. Use when asked to playtest, scan a run, triage arena findings, or file a bug from an ai_arena run.
---

# Playtest Scan

Turn completed `ai_arena` playtest runs into GitHub issues. The runner and its checkers are deterministic code; you are the judgement layer above them. You never edit engine code or fix anything in a scanning session — findings only. Fixes go through the normal issue → branch → PR flow.

## The loop

1. **Get the findings.** For a fresh run root:
   `python3 tools/playtest/run.py --check-only <runroot>`
   Or read an existing root directly: `~/.rwe/playtest/runs/<date>-<matrix-hash>/findings.jsonl` (one finding per line) and `index.jsonl` (one run per line). Exit code 2 just means findings exist.
2. **Triage each finding in severity order** (`likely-bug` → `suspicious` → `info`): confirm, reject, or escalate.
3. **File** each confirmed finding as an issue (dedupe first).
4. **Write `scan-report.md`** in the run root.

## Triage

For each finding, read the evidence the checker attached, then read the raw artifacts around it — never triage from `summary` alone:

- the `ai-arena.csv` window around the flagged tick (economy/army samples every 10 s);
- the `ai-arena-events.csv` rows near the flagged death or event;
- `game.log` lines before and after (and `path-profile.log` / `ai-profile.log` where the finding is pathfinding or perf);
- the **control twin's same window**: `~/.rwe/playtest/runs/<root>/<scenario>/seed<N>-control/` — same seed, no tune. Compare against it before believing a spike is real: much of what looks wrong is one arm of the matrix behaving differently for a legitimate reason. `arena-analyse.py` and `arena-deaths.py` summarise a run dir faster than reading CSVs by eye.

Then classify, per design §3.4 step 2:

- **Reject** — explainable without a bug (e.g. an economy stall that is the consequence of an all-in rush losing its army). Record the reason; it goes in the report.
- **Confirm** — wrong by the rubric below, with evidence and a plausible engine-level cause. File it.
- **Escalate** — looks wrong but the rubric is silent. Write a hypothesis and surface it to a human maintainer. **Never file an escalation as an issue.**

## The rubric

The known-wrong signatures. Seeded from design §6; every confirmed issue appends its own signature here **with its issue number**, so this list is the scanner's living memory — append it here when you file the issue.

- **resources** — stalled while stores are near zero and builders are idle; waste spikes not explained by a phase transition.
- **production** — factories idle with income positive; builder count collapsing to zero without matching losses.
- **deaths** — extractors dying with no enemies near; army dying detached from its own tower cover.
- **pathfinding** — sustained exhausted searches; `bugwalk`/`walkstuck` counters nonzero and growing.
- **determinism** — any hash divergence between same-seed runs of the same arm.
- **perf** — any AI pass beyond its baseline p95.

## Judgement

A **confirmed** finding has all three: a rubric rule it violates, evidence that it is real (and not the control twin's normal behaviour), and a plausible engine-level cause. An **escalated** finding has evidence but no rubric rule. When in doubt, escalate — a wrong issue costs more than a missed one. Confirmed findings are the rubric's output; escalations are how it grows.

## Dedupe

Before filing, search open issues by **engine area + symptom signature**, not wording — two reports of the same bug rarely share a title. `gh search issues -R CubeB/RWE-B4 --state open "<area> <signature>"`, then read the candidates. If one matches, comment the new run ids and evidence there instead of opening a duplicate. Never reopen a closed issue without new evidence; a closed issue with new evidence is a fresh issue that references it.

## Filing

File with the repo-scoped form (auto-allowed in this environment):

```
gh issue -R CubeB/RWE-B4 create --title "<area>: <symptom>" --body-file <path> \
  --label scope:sim --label purpose:rwe-original --label ai --label bug --label needs-triage
```

- **Title** — `<area>: <symptom>`, e.g. `AI economy: builder count collapses to zero after factory loss on water maps`.
- **Body** — what is wrong; the rubric rule violated; the run ids and seeds; the exact `ai_arena` command line that reproduces it (from `run.json`); the evidence rows and log lines **verbatim**; the hypothesis; and the matrix scenario that reproduces it — proposing it as an addition if it is new.
- **Labels** — one scope label and exactly one purpose label. AI behaviour is `scope:sim` + `purpose:rwe-original`. Add `ai`, `bug`, `needs-triage` on top. See `docs/agents/labels.md`.

Never close an issue you did not file.

## Evidence hygiene

An issue that cannot be reproduced from its own body is not done. Artifacts under `~/.rwe` are ephemeral and pruned after triage, so the issue must carry its evidence **inline**: run ids, seeds, the exact CSV rows and log lines, and the control twin's contrast. Do not link to files under the data root as if they will persist.

## Close the loop

Write `scan-report.md` in the run root: one line each for every confirmed, rejected and escalated finding, with its run id, verdict, and the issue number or escalation hypothesis. A human audits the stream from this file.
