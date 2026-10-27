# ADR-0004: A spec room stands between the decompilation and the code

`TotalA.exe` now exists as a full matching decompilation, byte-tactics. That
is readable, named C++ whose `src/` is licensed to nobody, and copying from it
takes no effort. Disassembly was never that easy to copy from. **So every
session, human or agent, now works in one of two rooms. The _spec room_ may
read the decompilation and writes only prose findings into the registers. The
_implementation room_ writes RWE's code and tests from the registers,
Nanolathe's clean-room spec and black-box observation, and never opens the
decompilation.** The register is the only thing that passes between the two
rooms, and the code is written from what the register says.

**Status**: accepted

## Standing verification instruction, 2026-10-16

The user requires new RWE code to be checked against byte-tactics, available
locally at `D:\bytetactics`, with documentation as the only handoff. This
extends the citation requirement below: each new code change needs a prose
finding recording the relevant byte-tactics comparison, reference revision or
build, evidence and unresolved questions before implementation proceeds.
An implementation agent never opens the decompilation to perform that check;
the dedicated spec room performs it and supplies the finding.

Verification must distinguish static reference analysis, RWE tests, and checks
actually executed against the original game. Where a code change has no
original counterpart, document that conclusion; where it intentionally differs,
record the divergence. Neither case is to be presented as verified parity.
This instruction applies prospectively and does not assert that the existing
codebase has already undergone a complete audit. Root `AGENTS.md` carries the
instruction for subsequent development sessions.

## Considered Options

**Keep the existing practice.** Until now, one person read the disassembly,
wrote a finding and then ported it. A register sat between the reading and
the code, but nothing separated the people. That was defensible because a
disassembly listing has no names or structure worth copying. A decompilation
has both, so the same practice is no longer defensible.

**Do without the decompilation, and work only from Nanolathe and black-box
observation.** This would be the cleanest option. It would also leave
networking without any source: Nanolathe treats retail networking as out of
scope. Every open item in `TA-NETWORK.md` (the unit CRC, the status `0x20`
fields, the `0x19` speed scale and the `0x0f` quit burst) is a question the
decompilation answers directly. It would also leave each Nanolathe *Unknown*
waiting on a static trace that has in fact already been done.

## Consequences

- **What a spec-room finding may carry is the same as what a finding has always
  carried.** It may state behaviour, arithmetic, constants, wire and file
  layouts, and routine addresses. It may not carry byte-tactics' identifiers
  (function, variable, type or field names), quoted code, or the decompiled
  control flow rendered as pseudocode. It states what the original does and
  leaves out how byte-tactics wrote that down. A finding that drew on the
  decompilation says so in its own evidence, and its review checks the
  wording as well as the facts.
- **The implementation room is to be closed by tooling as well as by
  convention.** Project settings are to deny agents access to byte-tactics,
  and a CI check is to reject any byte-tactics identifier that turns up in
  `src/`. Both are tracked in #476. Spec-room agents run from a separate
  checkout and change `docs/` only.
- **A finding still graduates by being checked**, as in `TOTALA-EXE-EXTERNAL.md`.
  Where a black-box check exists, it is the acceptance test. For networking
  that means a real `TotalA.exe` under `tools/ta-net/`. For simulation it means
  the demo oracles. The decompilation supplies the hypothesis, and the original
  running is the evidence.
- **A code change cites what it implements**, either a register section
  (`TOTALA-EXE §87`) or a Nanolathe anchor (`[NL 04 R-PATH-01 §10]`). That
  citation is the record that the change came from the implementation room's
  sources.
- **Nothing is re-derived retroactively.** The existing corpus and code stay
  as they are. This decision applies from here on.
