# Architecture Decision Records

`docs/adr/` holds this repo's ADRs: one file per design decision, recording
what was decided and why. They pair with `CONTEXT.md` at the repo root, which
is the glossary and nothing else — it carries the project's terms, never the
reasoning behind a choice. A term goes in the glossary; the decision that
shaped it goes here.

Read the ones touching the area you are about to work in. If your work
contradicts an existing ADR, surface it — cite the number — rather than
silently overriding.

## When to write one

When a design decision's rationale cannot live anywhere else — the glossary
has nowhere to put it, and the code alone would leave a future reader
guessing. All three of these must hold:

1. **Hard to reverse**: changing your mind later costs something meaningful.
2. **Surprising without context**: a reader will wonder why it was done this
   way, and be tempted to "fix" it.
3. **The result of a real trade-off**: genuine alternatives existed and one
   was picked for stated reasons.

If any is missing, skip it: the code and the glossary carry the rest.

ADRs are written when a decision is actually resolved, not speculatively.
The `.claude/skills/domain-modeling` skill maintains this directory and
`CONTEXT.md` as a session works, and creates files lazily. An empty
`docs/adr/` is not a gap and takes no placeholders.

## Format

A file is named `NNNN-kebab-case-title.md`: a sequential four-digit number,
then a short hyphenated slug. Its content is a title and one to three
sentences carrying the context, the decision and the reason. That paragraph
is the whole template; an ADR that fits in one sentence is fine.

Three optional sections, only when they earn their place:

- **Status** frontmatter, `proposed | accepted | deprecated | superseded by
  ADR-NNNN`, when a decision is revisited.
- **Considered Options**, when the rejected alternatives are worth
  remembering.
- **Consequences**, when the downstream effects are not obvious.

Numbering takes the highest number in the directory and adds one. Numbers are
never reused or renumbered: an ADR is cited as "ADR-0007" from issues,
reviews and skills, and a superseded decision stays in place, marked in its
status.

RWE is single-context: one glossary, one `docs/adr/`. There are no
per-context ADR directories.

## What does not belong here

Findings about the original, and deliberate divergences from it, live in the
registers under `docs/` (`TOTALA-EXE.md` and its siblings; divergences
specifically in §88) — they have their own numbering and their own labels.
ADRs record RWE's own design decisions.
