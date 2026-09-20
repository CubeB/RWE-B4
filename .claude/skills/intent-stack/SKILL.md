---
name: intent-stack
description: Generate an intent stack — a review aid that organizes a proposed code change into a forest of progressively more concrete intent, with provenance on every node and links from intent to the exact PR diff lines that implement it. Use when preparing a branch for review, or when asked to explain how a change was decided.
disable-model-invocation: true
---

# Intent Stack

Generate an **intent stack** for a proposed code change.

An intent stack is a post-hoc review artifact. It compresses the development session into a forest that lets a reviewer move from broad intent to progressively more concrete decisions and finally to the exact changed code implementing them.

The artifact is descriptive, not prescriptive. Do not change the implementation to make the tree cleaner. Analyze the implementation that exists.

## Output contract

Return the intent forest as Markdown. If a PR is open for the branch, the forest lives in the **PR description**: render it there (`gh pr view --json body` to read the current body, then `gh pr edit` with the forest first and any existing content below it — preserve what was there). If no PR is open yet, return the forest in the conversation and say nothing beyond it. Never check the log or the forest in as repo files. Do not add:

- an introduction
- a summary
- methodology
- confidence scores
- coverage statistics
- review advice

Every intent node is one short line and includes its provenance tag. Every diff reference is its own child line.

Example shape:

```markdown
- Add password-reset support; do not add another dependency [USER]
  - Use 15-minute, single-use reset tokens [[DESIGN](https://github.com/.../blob/main/docs/design.md)]
    - Reuse the existing signed-token module [AGENT]
      - src/reset_token.ex — issue/1 — +12L
    - Consume tokens using the existing verify-and-consume pattern [CODEBASE]
      - src/reset_token.ex — consume/1 — +7L
    - Cover expiry and token reuse [AGENT]
      - test/reset_token_test.exs — "expired token"
      - test/reset_token_test.exs — "token cannot be reused"
  - Extract reset request parsing [UNEXPLAINED]
    - src/reset_controller.ex — parse_request/1 — +14L
    - src/reset_controller.ex — request_reset/2 — -6L/+2L
```

The forest may have one root or many roots. Multiple roots are normal.

## Inputs

Use these sources, in this conceptual order:

1. The session's **decision log** (`.intent-log.md`, written by the autonomous-execution skills in this repo) — each entry is surviving evidence of a decision made at the time it was made, which is stronger than anything reconstructed afterwards.
2. The proposed diff.
3. The surviving conversation context, including compacted context.
4. Explicitly supplied handoff context.
5. Explicitly linked feature-specific design documents.
6. Repository contents and conventions.

A decision-log entry is trusted for its provenance tag (its `[AGENT]`/`[CODEBASE]` claims are real agent reasoning, not code-inferred) but is still checked against the diff: an entry whose decision did not survive into the change is dropped, per the negative-information rules below.

Do **not** inspect or use the current PR description unless the user explicitly asks you to use it.

### Diff scope

If the user explicitly supplies a diff, commit range, base ref, or PR, use that scope.

Otherwise, default to the PR diff for the current branch (`gh pr diff`), falling back to all current working changes that would be included by `git add .`, without modifying the index.

Do not run `git add` merely to compute the scope.

If there is nothing to scope, say so and stop.

## Core invariant: cover the diff

Every added and deleted line in scope must be attributable to at least one intent path.

Coverage is by changed line, not by hunk. A diff reference may cover an arbitrary contiguous subset of a hunk. A changed range may appear in more than one intent chain when it genuinely implements more than one independent intent — count the underlying changed lines as covered once.

Do not print a successful coverage report. Coverage is an internal invariant.

## Tree semantics

A parent-child edge means **more abstract/general intent -> less abstract/more concrete intent**.

Continue until the implementation is concrete enough to link directly to changed code.

Do not create synthetic grouping nodes merely to make the forest prettier. Every intent node must represent an actual intent or decision supported by provenance or reconstructed from the diff.

Do not require a fixed depth. These are both valid:

```markdown
- Rename UserToken to SessionToken [USER]
  - src/user.ex — UserToken -> SessionToken — +2L/-2L
  - src/auth.ex — authenticate/2 — +1L/-1L
```

```markdown
- Add authentication [USER]
  - Use JWT [USER_APPROVED]
    - Use RSA signing [USER]
      - src/token.ex — sign/1 — +9L
```

An intent node may have both diff-reference children and more-specific intent children. Do not put a diff reference on the same line as an intent node.

## Provenance

Use exactly these provenance categories for intent nodes:

1. `[USER]`
2. `[[DESIGN](URL)]`
3. `[USER_APPROVED]`
4. `[CODEBASE]`
5. `[AGENT]`
6. `[UNEXPLAINED]`

Their authority precedence is:

`USER > DESIGN > USER_APPROVED > CODEBASE > AGENT > UNEXPLAINED`

Precedence determines which label to use when the **same decision** is supported by multiple sources. It does not determine tree position. A more specific `[USER]` node can appear beneath an `[AGENT]` node.

Provenance is local to each node. Never inherit provenance from a parent merely because the child is necessary to implement it.

When provenance is ambiguous, choose the weaker category. Never upgrade provenance on uncertainty.

### USER

Use `[USER]` when the intent was directly expressed by the human — in conversation, or in the decision log as a user instruction. Compress rambling user language into a short faithful statement. Preserve material constraints, especially explicit negative choices:

```markdown
- Use opaque session cookies with immediate revocation, not JWT [USER]
```

Do not split one coherent instruction into multiple nodes merely because it contains several clauses.

### DESIGN

Use `[[DESIGN](URL)]` for intent originating in a **feature-specific design document explicitly supplied or linked in the session**. Design documents are less authoritative than direct USER intent because they may cover multiple changes or be authored by someone else.

Do not discover arbitrary repository documents and label their contents DESIGN. If a document was discovered independently in the repository, its influence is CODEBASE if it establishes a repository convention, otherwise use the appropriate weaker provenance.

Preserve useful negative design constraints just as for USER provenance.

### USER_APPROVED

Use `[USER_APPROVED]` only when:

1. an agent originated the specific decision, and
2. the user specifically approved that decision.

Agent proposes one narrow choice, implements only that, user gives unambiguous approval in that narrow context ("looks good") — that qualifies. "Looks good" on twenty decisions, broad delegation ("use your judgment"), or mere non-objection do not.

When approval scope is ambiguous, use `[AGENT]`.

### CODEBASE

Use `[CODEBASE]` when the decision follows an established repository pattern or concrete repository constraint. This requires evidence: the same pattern in two or more comparable implementations, explicit repository instructions (CLAUDE.md, CONTEXT.md, ADRs), or a concrete framework/type constraint. One vaguely similar example is not enough.

If the agent explicitly chose something and repository evidence is weak, prefer AGENT. If the choice exists only in the diff and repository evidence is weak, prefer UNEXPLAINED.

Express the concrete implementation decision, not a grouping constraint:

```markdown
- Return Result<User> from the auth handler [CODEBASE]
```

### AGENT

Use `[AGENT]` for decisions made by an agent when surviving evidence shows the agent actually made that decision — a decision-log entry, an assistant message explaining the choice, a plan, a subagent result, a trusted handoff.

Merely seeing code that an agent wrote is **not** AGENT provenance. If rationale must be inferred backwards from the code, use UNEXPLAINED.

Broad user delegation does not promote agent decisions: under "use your judgment for sensible defaults," the defaults chosen are still `[AGENT]`. Routine subordinate decisions (which representative test cases to add, import ordering) can be AGENT.

### UNEXPLAINED

Use `[UNEXPLAINED]` when no surviving provenance establishes the decision and the rationale must be inferred backwards from the resulting diff.

UNEXPLAINED does **not** mean the change is wrong, accidental, or suspicious. It means the reason is reconstructed rather than traced.

Infer unexplained intent as narrowly and conservatively as possible. Do not reverse-engineer an elaborate speculative architecture from a simple implementation.

If the diff contains unrelated unexplained work, make it an independent root:

```markdown
- Fix typo in authentication error [UNEXPLAINED]
  - src/errors.ex — invalid_credentials/0 — +1L/-1L
```

## Negative information

Retain explicit negative information when it materially characterizes USER or DESIGN intent ("X, not JWT"). Do not retain agent-generated rejected alternatives merely because the agent considered them. Abandoned implementation paths that never survived into the change do not belong in the intent stack.

If the change itself replaces a previously committed approach, that replacement is part of the present intent and is represented normally.

A material USER or DESIGN constraint that governs the whole change but has no changed-line descendants may exceptionally appear as a top-level zero-diff node. Use this sparingly.

## Compression rules

The intent stack is a review aid, not a transcript.

Add an intent child only when at least one of these is true:

1. it distinguishes provenance from its parent;
2. it groups multiple diff ranges that are useful to review together;
3. it expresses a materially narrower decision needed to understand the implementation.

Otherwise link the diff directly beneath the causal intent. Do not manufacture levels to make every path the same depth.

## Shared implementation

Prefer a tree, not a DAG. When a concrete decision could be justified by several parents, choose the more specific/stronger causal intent. If one changed range genuinely implements two independent intents, duplicate the diff reference beneath both chains — do not invent an artificial common parent to avoid duplication.

## Tests

Treat tests as ordinary changed code with potentially different provenance. A user-requested regression case is USER intent; agent-chosen representative cases group under an AGENT node. For test diff references, prefer the test name/function as the label; line counts are usually less useful and may be omitted.

```markdown
- Fix login for Unicode usernames [USER]
  - Cover representative Unicode cases [AGENT]
    - test/login_test.exs — "precomposed accent"
    - test/login_test.exs — "combining characters"
```

## Diff-reference labels

Every leaf is a Markdown link when a reliable PR diff link is available; otherwise plain text — never a fabricated URL.

For production code prefer `path — symbol — changed-size`:

```markdown
- src/session.ex — authenticate/2 — +3L
- src/AuthService.ts — validateSession — +11L
```

For tests prefer `path — "test name"`. For config/data/docs, use the most useful semantic section/key. If no reliable symbol exists, use path and changed-size.

### GitHub links

`gh pr diff` and `gh pr view` are the tools; do not make the skill depend on `gh` if equivalent context is already available.

GitHub PR Files-view line anchors have the form `#diff-<sha>-R<line>` (per-file, `-L<old>-R<new>` when both sides are wanted). Build them only when you can compute the per-file sha correctly; if you cannot, or are unsure, emit plain text. A plain-text label loses nothing that a wrong link would not destroy.

## Generated files, lockfiles, formatting

Do not create a special provenance category for generated code. If one generated file contains many hunks caused by one intent, collapse it to one file-level leaf. Routine consequences (lockfiles beside the dependency that caused them) need no intent node of their own.

Formatting naturally part of a changed range stays under that intent's chain; unrelated formatting is its own root (AGENT if the action survives in context, UNEXPLAINED if inferred). Never omit it — changed lines still require coverage.

## Building the forest

1. **Inventory the diff.** Every added/deleted line, grouped by file and hunk but addressable at line-range granularity. Note renames, containing symbols, test names, generated files.
2. **Extract provenance-bearing intents** — from the decision log first, then surviving conversation, handoffs, design docs. The forest describes the intent embodied in the final change, not abandoned decisions.
3. **Map known intents to changed ranges**, attaching each range to the strongest applicable intent. An intent that produced no surviving change is omitted (except material global USER/DESIGN constraints).
4. **Identify CODEBASE decisions** for what stronger provenance does not already explain.
5. **Add surviving AGENT decisions.** Do not infer AGENT intent merely because the agent authored the patch.
6. **Reconstruct all remaining intent** as narrowly as possible, labelled UNEXPLAINED, under the known intent it concretely implements when that is clear; as separate roots when unrelated.
7. **Compress.** For every intent node ask the three questions above; remove nodes for which none apply. Do not create synthetic grouping parents.
8. **Verify coverage** internally. If anything is uncovered, keep reconstructing. Never claim a line is covered by an intent that plainly does not explain it.
9. **Create reviewer links** per the GitHub rules above.

## Root ordering

Order roots for reviewer usefulness:

1. primary USER/DESIGN feature work
2. other directly relevant work
3. additional AGENT work
4. unrelated UNEXPLAINED work

Preserve the natural implementation story inside each tree.

## Do not do these things

Do not:

- generate a prose PR summary
- rewrite the code
- invent missing user intent
- convert agent reasoning into USER provenance
- convert broad delegation into USER_APPROVED
- infer AGENT rationale solely from code
- label discovered repository docs DESIGN unless they were explicitly supplied as the design input
- use CODEBASE for a weak or one-off resemblance
- create grouping-only nodes
- force a single root
- force equal tree depth
- omit deletions, docs, config, generated files, lockfiles, or formatting from coverage
- preserve abandoned reasoning that did not survive into the change
- add confidence percentages
- print coverage diagnostics when coverage succeeds
- fabricate diff URLs

## Final quality check

Before returning the forest, confirm internally:

- every changed line is covered
- every intent node has provenance
- every child intent is materially more concrete than its parent
- no grouping-only nodes exist
- provenance uses the strongest justified category, and no stronger
- USER and DESIGN negative constraints were preserved when materially useful
- USER_APPROVED has narrow enough approval evidence
- CODEBASE has real repository evidence
- AGENT has surviving reasoning evidence (decision log or context)
- code-inferred reasoning is UNEXPLAINED
- independent changes are separate roots
- diff labels use useful symbols/test names where available
- no fabricated links
