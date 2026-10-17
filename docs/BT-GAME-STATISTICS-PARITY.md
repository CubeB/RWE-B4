# Online score tables and reporter lifetime parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GAME-PLAYERS-PARITY.md` and
`BT-GAME-CD-CHECK-PARITY.md`, it allocates no section number in the main register. It covers one
small chapter of the reference, `game-statistics`: the three arrays of report structures that
describe the players of a multiplayer game to the original's online-reporting service, how they
are filled from the player table, and how the reporter library is loaded and unloaded. This is
the finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

The **local** results chart is a different table with a different filter and is audited in
`BT-FRONTEND-ENDGAME-PARITY.md`; the chapter says so itself. The event codes and the transport
(`ReportGameEvent`, `ReportGameChat`) belong to the unit-sync chapter and are not audited here.
The service this module reports to (the 1998 Total Annihilation online ladder) no longer exists,
which settles most of the module as *not applicable*; what stays useful is the score formula,
the definition of the nine fields and two small facts about who counts as a participant.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-statistics.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links
  were followed. Addresses are the ones the chapter names; rules are restated in prose. The
  spec-room discrepancy register was read, read-only; no row concerns this chapter.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No reporter library was loaded and no
  network session was started, by that project or here.
- **The RWE half** is a read-only audit of `src/rwe/game/EndGameStats.*`,
  `game/GameEndedReport.*`, `game/AiArenaReport.cpp`, `sim/GamePlayerInfo.h` and the control
  channel's end-of-game message at the working tree of 2026-10-17. Nothing was built, run or
  changed. Another agent had uncommitted edits in the tree, so every line number is "about".
- **Documents read first:** `TOTALA-EXE.md` (index, sections 88 and 91),
  `TOTALA-EXE-INTERFACE.md` section 104 and `TOTALA-EXE-EXTERNAL.md` ("Settled: the end-of-mission
  score divisor"), which already hold the score formula.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The allocation schema, the loader and the hazards (not applicable)
3. Which players are described, and what each record says
4. The score, and the nine named fields
5. What RWE reports instead
6. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| consumers, reporting paths | none (the online service is not in any register file) | not applicable (rule 2) |
| allocation schema, return codes | none | not applicable (rule 2) |
| filling players, allies, scores | section 104's score table for the *local* chart only | rules 3 and 4 |
| score formula | `TOTALA-EXE-INTERFACE.md` section 104 (corrected 2026-09-24) and `TOTALA-EXE-EXTERNAL.md` | agrees; **RWE's code does not follow the corrected formula** (rule 4) |
| loading the reporter library, export list | none | not applicable (rule 2) |
| enable query, shutdown, stale pointers | none | not applicable (rule 2) |
| persistence | none | not applicable |

## 2. The allocation schema, the loader and the hazards (not applicable)

**Reference.** The module allocates three arrays of ten pointers, and for each of the ten
physical player slots five blocks: a 24-byte player report (name and side pointers borrowed,
an identifier, flags, an ally count and a pointer to the allies), a 40-byte table of up to ten
ally pointers, a 12-byte scoreboard (aggregate score, a count and a pointer), a 36-byte table of
**nine** score pointers (the adjacent source comment about eighteen is inconsistent with the
loop) and a 72-byte table of nine eight-byte records, each a borrowed name and an integer.
Allocation returns zero on success and one on a detected failure; the caller then compares with
two, which the allocator never returns, so its failure branch for that case is unreachable.
Loading the reporter library first tears down any earlier tables (and uninitialises the
multiplayer layer if a library had been loaded), refuses IPX, serial and modem providers with
result four when a certain global is empty, installs two callbacks, loads the library by name and
requires **eight decorated exports** (enable, version, initialise, report, report-chat,
set-callbacks, interval timer, terminate). A missing export unloads and returns two; a version
mismatch or an initialise result of four unloads but **falls through** to a final availability
test that the already-set online flag can still pass, so the overall result can be "available"
after the library refused. Shutdown clears the flag and both callback addresses, uninitialises
the session, terminates and frees the library, then frees every slot's blocks without testing
each slot pointer, and leaves the export pointers stale. A dialog offers the library's choices
only for the multiplayer game type, only when the result is zero and at least one choice came
back.

**RWE: not applicable, and correctly omitted.** There is no reporter library, no online ladder,
no provider GUID and no fixed ten-slot report structure in RWE. The module's faults (partial
allocation, the unreachable return, the stale export pointers, the early fall-through) are
the chapter's own list of defects; none can occur. `TOTALA-EXE.md` section 88 does not need a
line for them.

## 3. Which players are described, and what each record says

**Reference.** `FillScoreTables` scans all ten slots. A slot is **included** when the detailed
active/type/index test passes, **or** when the slot has created at least one unit, **or** when it
is simply active: the chapter says the expression ends in two alternatives that make every active
slot, and every inactive slot that ever created a unit, appear. Entries are stored at their slot
index and are **not compacted**, and a slot excluded on this call is **not cleared**, so values
from an earlier call can remain. The count of included slots is returned. For an included
slot: the name pointer, the network identifier, the flags (bit 1 always set; bit 2 for a local
computer or a remote type-three slot whose player info says computer; bit 4 for an observer;
bit 8 for a player-info flag), the side pointer from a **two-entry** table (Arm, Core) indexed
directly by the player-info side with no bounds check, and the allies: every other slot whose
allied byte is nonzero, **active or not**, counted and null-padded to ten.

**RWE: DIFFERS in shape; no consumer.**

- **No equivalent record.** The nearest things are `EndGameStatRow` (name, colour, seven values,
  `EndGameStats.h`) for the local chart, `GameEndedReport` (outcome, winner, tick, time, build,
  artifact paths; `GameEndedReport.h`) for the launcher's control channel, and the arena CSV row
  per player (`AiArenaReport.cpp`). None carries flags, a network identifier or an ally list.
- **The two-side table: DIVERGENCE (documented elsewhere).** RWE reads side names from the side
  data (`BT-GAME-SIDE-DATA-PARITY.md`) and has no fixed two-entry table. A mod with a third side
  would index past the original's table; RWE has no such limit.
- **Allies as "any nonzero byte, active or not": DIVERGENCE (documented).** RWE's alliance is the
  lobby's symmetric team id (`BT-GAME-PLAYERS-PARITY.md`, closing list of documented
  divergences). The report would list the same set for a game started from the lobby.

## 4. The score, and the nine named fields

**Reference.** The aggregate is the integer conversion of the game tick divided by **sixty**
(an integer quotient, truncated before the multiplication) times the mission time multiplier,
plus, separately converted, the kill count times the mission kill multiplier. Neither term is
guarded against overflow or a negative multiplier. The nine named values are: kills, losses,
total energy produced, total metal produced, energy wasted, metal wasted (the four resources
converted to integers), **commander kills**, **commander losses**, and a **winner** field whose
configured label is "I am Winner". The winner field is the global winner bit (bit four of the
result flags) for the **local index or any active type-two computer**, and zero for others. The
chapter notes the consequence: the local human and each computer in the game share the **same**
bit, so a local victory marks the computers as winners too. Nothing is computed per player.

**Register.** `TOTALA-EXE-INTERFACE.md` section 104 (corrected) gives the same formula with the
divisor of sixty, and `TOTALA-EXE-EXTERNAL.md` shows how the divisor was found (the compiler's
unsigned divide-by-constant idiom whose constant is 59.99999998777639). The chapter independently
agrees. The *local* chart additionally clamps a negative total to zero, which the chapter says
the **online** table does not ("negative-score and winner-field policies are not identical").

**RWE.**

- **The divisor: DIFFERS (a code fault against the corrected register).**
  `computeEndGameStats` (`EndGameStats.cpp` about 30) divides by `SimTicksPerSecond` (30). The
  chart is the only place RWE computes a score. See `BT-FRONTEND-ENDGAME-PARITY.md` rule 5 and
  its Gaps rank 1; `EndGameStats.test.cpp` encodes the thirty. Shipped maps set `timemul` to
  zero, so retail data is unaffected.
- **Truncation order: MATCHES.** Each term is converted to an integer on its own and the total
  is floored at zero (`static_cast<int>` of each float product, about 44-48). Both terms
  start from an integer quotient taken before the multiplication, as the chapter requires; only
  the divisor is wrong.
- **Commander kills and commander losses: MISSING.** `GamePlayerInfo` has `unitsKilled` and
  `unitsLost` (about 120) but no commander-specific counters. The original's two extra fields
  are online-reporting only; the local chart has seven columns and none of them is a commander
  count. Nothing in RWE needs them. If a "commander kills" statistic is ever wanted (the
  Deathmatch rule of BT-83 would make it interesting), the counter belongs next to
  `unitsKilled` and in `save_util.cpp`, `GameHash_util.cpp` and `dump_util.cpp` only if the
  simulation reads it, which a statistic does not (`CLAUDE.md` "Keep sim state in step across
  four places").
- **The winner field: DIFFERS, deliberately.** `GameEndedReport` names exactly one winner (the
  lowest id of the winning team; see `BT-GAME-VICTORY-PARITY.md` rule 7 for the reading problem
  that raises). The original's field marks the local player and every computer when the **local
  machine** won. That is a reference quirk and not a rule to copy.
- **Aggregate score for a computer or remote player: the chart shows it; nothing reports it.**
  RWE's chart computes a score for every row, as the original's local chart does.

## 5. What RWE reports instead

**Reference.** Reports carry a session and provider identity, the mission name, the local index,
the participant count and the arrays, and chat uses the local report-player pointer. A periodic
callback calls the library's timer only while its handle is nonzero.

**RWE.** There is no online report. What leaves the process is:

- `GameEndedReport` (`GameEndedReport.cpp`): outcome (`decided`, `draw`, `abandoned`), winner,
  tick, game seconds, desync tick, build, and the replay, hash-log, desync and log paths, sent
  once on the control channel to a launcher (`GameScene_endgame.cpp` about 118-175; ROADMAP
  about 554). Not hashed, saved or read back.
- The arena report (`AiArenaReport.cpp`), written only for `--ai-arena` runs.

Neither is a scoreboard and neither takes anything from this chapter. The launcher's lobby
records no scores. **Not applicable.**

---

## 6. Gaps to fix

Ranked by what could be noticed. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **The score's clock term divides by 30; the corrected register and this chapter say 60.** Invisible on shipped maps (`timemul` 0). | DIFFERS (a code fault) | code: `EndGameStats.cpp` with its test (the same item as `BT-FRONTEND-ENDGAME-PARITY.md` rank 1) |
| 2 | **No commander kill or loss counters.** Online reporting only in the original. | MISSING, no consumer | none; add only if a statistic is ever asked for |
| 3 | **The reporter library, the ladder and the provider rules** have no counterpart. | not applicable | none |
| 4 | **The "winner" field marks every computer when the local player wins.** | reference quirk | none; do not copy |

No source file was changed.
