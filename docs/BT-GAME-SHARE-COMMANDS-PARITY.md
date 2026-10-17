# Sharing preferences, thresholds and the diagnostic command toggles parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-PLAYERS-PARITY.md` and `TA-SYSTEMS-PARITY.md`, it
allocates no section number in the main register. It covers one chapter of the reference,
`game-share-commands` (twelve services from `0x418CD0` to `0x419560`): the five player-info sharing
flags and `ShareAll`, the two resource thresholds, the outgoing compression switch and the three
local display and effect toggles (`ShowRanges`, `BPS`, `SFX`). This is the finding that
implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

`TOTALA-EXE-COMMANDS.md` section 124 already decodes the same commands from the executable, and
RWE implements them (`src/rwe/sim/ResourceSharing.*`, `src/rwe/game/chat_command.*`,
`GameScene_chat.cpp`). So this audit is mainly a cross-check, and it records where the register's
later resolutions (BT-27, BT-57, BT-139) have left RWE's two placeholder commands saying something
that is no longer true. The distribution pass that consumes the thresholds is BT-27's
(`BYTE-TACTICS-SPEC-ROOM-FINDINGS.md`); the share dialog, the other path to a transfer, is
`BT-INGAME-DIALOGS-PARITY.md` rule 2.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-share-commands.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source links were
  followed. Addresses are the ones the chapter names; rules are restated in prose. The spec-room
  discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read,
  read-only; BT-27, BT-57 and BT-139 are the rows this audit touches and none is re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no command was run, no preference changed and no packet sent. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `game/chat_command.*`, `game/GameScene_chat.cpp`
  (`runChatCommand`), `sim/ResourceSharing.*`, `sim/GamePlayerInfo.h` and the visibility code
  (`sim/GameSimulation_visibility.cpp`) at the working tree of 2026-10-17. Nothing was built, run
  or changed; another agent had uncommitted edits, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. Registration and routing
3. The five sharing flags and `ShareAll`
4. The two thresholds
5. Outgoing compression
6. The local toggles: ShowRanges, BPS, SFX
7. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| routing, the registered table | `TOTALA-EXE-COMMANDS.md` section 124 (the table, mask one, `+ShareLOS` unregistered) | **MATCHES**; rule 2 |
| the five flags, `ShareAll` | section 124 (bits `0x02`, `0x04`, `0x20`, `0x40`, and `0x08` unregistered); BT-57, BT-139 | the flags **MATCH**; the two placeholders have gone stale; rule 3 |
| the thresholds | section 124; BT-27 | **MATCHES**; rule 4 |
| compression | none | not applicable; rule 5 |
| ShowRanges, BPS, SFX | section 124 ("other registered commands", unbuilt) | **MISSING**; rule 6 |

## 2. Registration and routing

**Reference.** The main command table registers ShareMetal, ShareEnergy, ShareMapping, ShareRadar,
ShareAll, ShowRanges, SetShareMetal, SetShareEnergy, Compression, BPS and SFX with permission mask
one. The line-of-sight toggle (`ToggleShareLos`) is a service of this group but is **not**
registered under any name in that table slice, so a chat command with that spelling is not known.
The handlers that take no argument ignore it; the thresholds read argument one.

**RWE: MATCHES.** `parseChatCommand` (`chat_command.cpp` about 100-170) recognises the case-folded
words `sharemetal`, `shareenergy`, `shareall`, `sharemapping`, `shareradar`, `setsharemetal`,
`setshareenergy` and RWE's own `give`; `chat_command.h` (about 20) records that `+ShareLOS` is
"not registered by the original either" and answers "Unknown command". `ShowRanges`, `BPS`,
`SFX`, `Compression` and every other registered command also answer "Unknown command: <word>"
(`GameScene_chat.cpp` about 109-113), which `TOTALA-EXE-COMMANDS.md` section 124 says in so many
words.

## 3. The five sharing flags and `ShareAll`

**Reference.** Each of the five handlers (metal bit `0x02`, energy `0x04`, mapping `0x20`,
line of sight `0x08`, radar `0x40` of the flag byte at `+0x97` of the player-info record) tests the
network-game flag first and, when it is clear, **returns without a message or a broadcast**. When
set it flips its one bit, adds a **confirmation message** (fixed English text plus ON or OFF,
three formatting arguments), and **broadcasts the updated player-info block**. The broadcast
carries a preference, not an amount or a sight bitmap; turning a flag on transfers nothing at once.
`ShareAll` runs four inlined toggles in the order metal, energy, mapping, radar, each with its own
message and broadcast; it **does not include line of sight**, and because each is a toggle, a mixed
starting state stays mixed with every bit inverted. (The register's open note: the byte's bit
assignments collide with two other readings in `TOTALA-EXE-DATA.md` for `0x04` and `0x08`.)

**RWE: MATCHES for metal, energy and `ShareAll`'s two; DIFFERS in wording and in the outside-a-
network-game case; the other two are stale placeholders.**

- **Metal and energy: MATCHES.** `ToggleMetal` and `ToggleEnergy` flip `shareMetal` and
  `shareEnergy` (`ResourceSharing.cpp` about 141-148), run by every peer from the command stream,
  hashed, saved and dumped (`GamePlayerInfo.h` about 87-103, a deliberate difference from the
  original, which saves none of it). The pass they gate is BT-27's.
- **The refusal outside a network game: DIFFERS (presentation).** The original returns silently;
  RWE prints "That command only works in a network game." (`GameScene_chat.cpp` about 118-124),
  and also refuses a computer player, a recording and an own-clock game (a TA peer is a different
  program). Nothing the sim sees.
- **The confirmation wording: DIFFERS, UNCLEAR.** RWE prints `ShareMetal: on` and `ShareEnergy:
  off`, lower case, with the state *predicted* from the local flag before the toggle lands. The
  chapter says "fixed English text plus ON/OFF"; BT-139's evidence gives the radar line as
  "Toggled ShareRadar to: ON". The other four lines' text is not quoted anywhere. RWE's lowercase
  is its own.
- **`ShareAll`: DIFFERS by two toggles.** RWE submits metal and energy and prints their lines;
  the original's four calls include mapping and radar. RWE's comment ("two independent toggles, as
  in the original: with mixed starting states it is not 'all on'") is right about the mixed-state
  rule.
- **`ShareMapping` and `ShareRadar`: stale, a probable fault in the message.** RWE answers
  "ShareMapping: allies already share their map in RWE." and "ShareRadar: allies already share
  their radar in RWE." and holds no state for either (`GameScene_chat.cpp` about 157-164). Two
  things have moved since those lines were written:
  - **Radar is no longer shared at all.** Commit `ba7000e8` ("Stop allies sharing radar and sonar
    detectors", BT-139) removed the unconditional sharing, and `GameSimulation_visibility.cpp`
    (about 640-645) says "detectors are not" shared. So the printed sentence is false. In the
    original the radar bit does one thing: **your own units appear as contacts on an ally's
    picture** when it is set (BT-139). RWE has no such flag, so a player cannot make their units
    show as radar blips for an ally, and the toggle prints a lie.
  - **Map sharing is continuous in RWE** (sight is shared between allies, about 600-640 of the same
    file), where BT-57 found the original does a one-shot copy: on the share dialog's button, on a
    received packet, or **every 450 ticks (fifteen seconds) when the sender's mapping bit is on**,
    only toward remote human allies, and never a live merge. The sentence "allies already share
    their map" is true of RWE and false of the original, which is BT-57's DIVERGENCE row and not
    new; what is new is that the toggle, which would drive the push, has no state.
  - Adding either flag is a **four-place change** (`CLAUDE.md`, "Keep sim state in step"): the
    struct, `save_util.cpp`, `GameHash_util.cpp` and `dump_util.cpp`, with the hash version in
    step.
- **`ToggleShareLos`: MATCHES.** Not registered in RWE either.
- **Broadcast: DIFFERS in mechanism.** The original sends the player-info block. RWE queues a
  `PlayerShareCommand` that every peer applies at the same tick, so there is no player-info block
  to send. Equivalent for what the sim does.

## 4. The two thresholds

**Reference.** `SetShareMetal` and `SetShareEnergy` act only with the network flag set. Each reads
integer argument one (zero when absent or not a number, by the interpreter's conversion), converts
it to a float and stores the **smaller of that and the matching current storage capacity** in the
local player's threshold. There is **no lower clamp**: a negative argument is stored. The capacity
used is the current one; a later change of capacity does not rerun the handler. The confirmation
prints the **requested integer, not the clamped float**, so a request above capacity shows a
larger number than was stored. The min expression may call the argument getter twice and the
message a third time, relying on stability. The handlers neither broadcast player info nor start a
transfer; the threshold lives on the local runtime record, not in the five-flag byte. The consumer
reads it on its schedule, with its recipient and role tests. The share dialog is a separate
explicit-amount path.

**RWE: MATCHES.** `SetShareMetal`/`SetShareEnergy` carry the typed integer (`chatAtoi`, which
saturates, `chat_command.cpp` about 60-100), print `SetShareMetal: <typed>`, and the sim stores
`min(capacity, amount)` with a negative left as typed (`ResourceSharing.cpp` about 149-168), as
`TOTALA-EXE-COMMANDS.md` section 124 describes. Differences of detail:

- **The capacity is read when the command lands**, a few ticks after the typing (the command
  travels through the lockstep stream), where the original reads it at the keystroke. A storage
  building completed in that window would change the stored value by an amount the original would
  not. Not observable in practice.
- **The reserve is hashed, saved and dumped**; the original keeps it on the runtime record and does
  not save it (section 124).
- **The refusal text outside a network game** is the same difference as rule 3.
- **No case of a missing argument is shown separately**: `chatAtoi("")` gives zero, as the
  interpreter does.

## 5. Outgoing compression

**Reference.** `Compression` flips whether outgoing packets may be sent packed: it assigns the
off flag from whether its previous value equalled zero (normalising to zero or one) and prints
outgoing compression OFF when the flag is nonzero. It is local; it does not change what the
receiver decodes. The sender's consumer uses the packed form only when the packed length plus
three bytes is smaller than the plain one and the flag is clear.

**RWE: not applicable.** RWE's lockstep protocol is its own protobuf over UDP
(`proto/network.proto`) and has no packed-or-plain choice, no per-packet size comparison and no
such switch. The command answers "Unknown command". Nothing for a port.

## 6. The local toggles: ShowRanges, BPS, SFX

**Reference.** Three handlers **do not** test the network flag. `ShowRanges` XORs the low bit of
the game's `showRanges` field, `BPS` the low bit of `showBps`, and `SFX` the low bit of the global
byte that **blocks the effect-event pool**. They add no message, broadcast or persistence. The XOR
keeps the higher bits (unlike the compression flag's normalising). `showRanges` is read by the
unit range-ring drawing (`BT-INGAME-MARKERS-PARITY.md` rule 4) and nothing else; `showBps` by a
statistics display; the blocked byte by the effect-event allocator, which returns null while it is
set, so **new effect events stop** (not every particle, not any sound, not an existing effect).

**RWE: MISSING (all three), by choice of surface.**

- **`ShowRanges`: MISSING.** No diagnostic ranges. RWE draws the cloak radius of the hovered unit
  and the minimap detection rings, and not the sight, radar, sonar, jammer, build, manoeuvre,
  kamikaze or weapon rings the original draws in this mode. See the markers audit.
- **`BPS`: not applicable.** RWE's network overlay on F9 (`docs/NETWORK-OVERLAY.md`) is the
  equivalent, and richer.
- **`SFX`: not applicable.** A development switch for new effect events; RWE's debug window has its
  own particle switches.
- The three would be harmless to add as client-side commands: none feeds the simulation, so they
  need no command, hash or save, only a branch in `runChatCommand`. They are not network-only in
  the original, so RWE's "only works in a network game" refusal must not apply to them.

---

## 7. Gaps to fix

Ranked by what a player would notice. None changes a sim outcome except the ShareMapping and
ShareRadar flags, which are new hashed state if built.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **`+ShareRadar` prints "allies already share their radar in RWE"**, which has been false since `ba7000e8`; the toggle has no state and the original's meaning (your units become contacts on an ally's picture) is absent. | DIFFERS (stale message; the feature is BT-139) | code: either build the flag (four-place change) or change the message to say the command is not built |
| 2 | **`+ShareMapping` has no state**; the original pushes the map to remote human allies every 450 ticks when it is on, and RWE shares sight continuously. | DIVERGENCE (BT-57), toggle missing | decision |
| 3 | **`+ShowRanges` is unknown**, and with it the whole diagnostic range-ring display. | MISSING | code, with `BT-INGAME-MARKERS-PARITY.md` rule 4 |
| 4 | **`+ShareAll` toggles two flags, not four.** | DIFFERS (follows rows 1 and 2) | follows rows 1 and 2 |
| 5 | **The confirmation wording** (lower-case `on`/`off`, a predicted state) is not the original's ON/OFF lines. | DIFFERS (presentation, UNCLEAR) | spec-room read of the message strings |
| 6 | **Outside a network game the original is silent; RWE prints a refusal.** | DIFFERS (presentation) | none |

No source file was changed.
