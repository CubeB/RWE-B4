# ADR-0001: RWE writes TA demos from its simulation, as a pure observer

RWE records `.tad` demos so its own output can go through the same corpus
machinery as a real recording: the same extractors, the same reference
oracles, and a scored cell that moves under RWE but not under TA. A demo is a
stream of state and effects and not of orders, so the recorder observes a game
RWE is already simulating and writes, per player per tick, the DirectPlay
traffic that player's TA peer would have sent. **The recorder is a pure
observer: nothing it holds is hashed, saved or dumped, the simulation never
reads it back, and no frame-rate or wall-clock value may reach the simulation
through it** -- recording is opt-in, and a disk stall may cost frames but must
never change a tick.

**Status**: accepted

## Target

**L1** is parseable: `tad_probe` walks the file clean, the status checksums
verify and nothing is unknown. **L2** is mineable: the four reference oracles
score it. **L3** is a faithful capture -- every subpacket a real recording of
that game would contain, including the `0x10` echo, ownership transfers,
speed/pause, chat and the loading handshake, such that a TA client can load it
and watch. This ADR targets L1 and L2, so that the reference scorers can be
pointed at RWE's own arena output; L3 is the natural end state and stays on the
roadmap, its remaining pieces being reverse-engineering tasks of their own. No
L3 item gates L2.

## Decisions

**D1 -- Every player is a sender, and there is no watcher seat.** A real
capture is one peer's traffic, but TA Demo Recorder sees all traffic, including
the settle fan-out (the corpus's `numPlayers - 1` identical `0x28` copies).
The recorder is a wire tap, not a seat: emit one packet per sender per tick.
A seat-filtered variant can be added later if a real use appears.

**D2 -- Live observation, with the replay path for free.** The recorder is
attached to the simulation, the shape TA itself has, because offline synthesis
of every game would double the CPU and buy nothing, and a live recorder also
survives a crash -- which is when a demo is worth most. The offline path
follows at no extra cost: `rwe --replay <file> --record-demo <out>` re-runs
the real simulation, so the writer can be debugged by replaying a game as many
times as it takes.

**D3 -- `maxUnits` is a recorder setting, default 1000, with a per-owner free
list.** RWE has no equivalent of TA's lobby unit limit. The recorder carries a
free-slot allocator per owner block, recycling ids as TA recycles them; it
refuses, with a clear message, to record a game where any player ever exceeds
its block. TA cannot represent that either, and silently dropping units would
corrupt every downstream oracle.

**D4 -- The hooks live in a pure observer.** No recorder member is hashed, saved
or dumped; the simulation never reads it; and no frame-rate or wall-clock
value may reach the simulation through it. In `GameSimulation.h` the recorder
is a forward-declared `std::unique_ptr`, the `AiPlayerController` /
`SimEventLog` precedent, both for the determinism rule and for the COFF section
budget -- `GameSimulation.cpp` is the file to watch.

**D5 -- Packet times are tick-derived.** `Packet::time` is wall clock and the
tools ignore it. Times come from the demo clock scaled by milliseconds-per-tick,
so a given game writes the same bytes; paused stretches are represented by the
`0x19` records, not by a wall-clock gap. If TA playback is ever found to pace
from this field, revisit -- it is one function.

**D6 -- Packet records are uncompressed.** `tadCompress` is the status-message
compressor -- three-byte header, back-references biased by 3 -- and cannot be
reused for packet records, whose header is one byte; using it there shifts
every back-reference. Type byte `0x03` is a legal, reader-handled path and
`tadUnsmartpak` passes plain subpackets through, so uncompressed records come
first. A header-size-parameterised compressor is a closeout item if file size
ever matters.

**D7 -- The `0x1a` unit table is valid but synthetic.** The ids are
content-derived and not reproduced yet (two failed passes; the named way in is
a recursive-descent disassembly of `0x455000`-`0x46e000`, or a live breakpoint
on the restrictions-dialog arrays). Emit a table with the data set's unit
count, the fixed pseudo-entry, and deterministic synthetic ids otherwise. The
tools' filter is a count comparison and `--units` naming does not use the
table, so this does not block L2. The reverse-engineering task stays separate
and unscheduled.

**D8 -- The lobby status message is checksum-valid and zero beyond the
DirectPlay id.** Only offset `0x91` of the decoded body is understood.
Construct a 192-byte message with the id there and the rest zero, using
`tadEncrypt` + `tadCompress`, which already round-trip. That makes the status
record verifiable by `tad_probe` rather than merely present; the fields beyond
the id are closeout work.

**D9 -- `0x10` is omitted from L2.** A real stream is 14% `0x10` by subpacket
count, so the omission is a visible hole in an otherwise faithful capture. The
calls TA echoes are the ones routed through `0x456200` -- weapon aim scripts,
transport `BeginTransport` and friends -- and no complete call-site inventory
exists. No L2 oracle reads the code, so it is omitted and an inventory pass is
scoped for the closeout, rather than guessed at now.

**D10 -- Ownership changes are destroy-and-replace, with the original's own
cause-4 record.** Capture moves a unit between owner blocks and a demo cannot
express "the same id changed hands". The original does not express it either:
`0x488570` implements an owner change as a new record under the captor plus
the old one killed with cause 4, and the corpus reads severity 0 and corpse
level 0 on every cause-4 death (`TOTALA-EXE-WRECKS.md`, "What each death cause
is"). So the recorder emits that death for the old id, sends it from the old
owner, and gives the unit a fresh id in the captor's block, where the ids
recycle exactly as a death's do. There is deliberately no `0x09` -- a capture
is not a nanoframe -- so a receiver learns of the new unit from the captor
block's next `0x2c`. Settled with the ownership hook against the
`capture.test.cpp` scenarios.

**D11 -- Not in scope.** Playing `.tad` in RWE (puppet playback), inferring
orders from demos, SmartPak coalescing in the writer, and reproducing TA's
computer-player handicap. RWE's AI is RWE's, so AI games are valid conformance
subjects for engine arithmetic but must not be compared with TA AI demos on
economy.

## Divergences

D7, D8 and D9 are the three places what RWE writes knowingly differs from what
a real recording contains. They are divergences in the writer's output and not
in the game, and the container is framed by the recorder rather than by
`TotalA.exe`, so they are recorded here and in `docs/TA-DEMOS.md`, "Writing
one", rather than in §88. None is read by an L1 or L2 tool: `tad_probe` checks
the table's size and the status checksum, and no L2 oracle reads `0x10`. All
three are closeout items with their evidence and their way in recorded above,
and none of them holds up L2.
