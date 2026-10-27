# RWE development instructions

Read `CONTEXT.md`, `CLAUDE.md`, and the relevant decisions in `docs/adr/`
before working in this repository.

## Original-behaviour verification

Standing user instruction, 2026-10-16: all new RWE code must be checked against
byte-tactics through documented findings. The local reference is
`D:\bytetactics`. Follow ADR-0004's separation throughout:

- An implementation agent must never open the decompilation. It works from
  the prose findings in `docs/`, original-game observations, and the accepted
  clean-room specifications.
- A dedicated spec-room agent may inspect byte-tactics read-only, from a
  separate RWE documentation checkout. It changes `docs/` only and never
  implements RWE code or tests after reading the decompilation.
- Only prose findings cross from the spec room to implementation. Never pass
  source excerpts, decompilation identifiers, patches, or decompiled control
  flow rendered as pseudocode. Addresses, behavioural arithmetic, constants,
  and wire/file layouts are permitted by ADR-0004.
- Every new RWE code change must cite a finding that records its byte-tactics
  comparison, reference revision/build, evidence, and remaining uncertainty.
  If the relevant comparison is missing, obtain spec-room documentation before
  implementing the change. A passing RWE test alone does not establish parity.
- Verify the implemented behaviour against the documented result, using real
  shipped data and original-game observations or demo oracles where available.
  Distinguish a static reference check from an executed original-game check;
  never report the latter when it was not run.
- For functionality with no original counterpart, or a deliberate divergence,
  document that conclusion and its rationale explicitly. Do not claim original
  parity for RWE-specific behaviour. Record deliberate behaviour departures in
  the existing divergence register as required by the project.

Current priority investigations are submarine construction height in shipyards
and D-gun interactions with surface ships and submerged submarines. Treat the
reported expected behaviour as a hypothesis until the reference establishes it.
Documentation work does not authorize implementing these fixes.
