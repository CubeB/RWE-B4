# Message HUD ring, chat formatting, filtering and unit jumps parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-DIALOGS-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `network-net-chat` (six services from `0x463ca0` to `0x464290`): the
thirty-record message ring that every on-screen text line passes through (chat, "Under Attack",
victory and defeat lines, timeout notices), how a chat line is formatted for sending, how old
lines expire, how the visible lines are chosen and drawn, and the camera jump to the unit a
line was about. This is the finding that implementation work cites for any of them, per
`AGENTS.md` and ADR-0004.

Not repeated here: the **recipient modes and the one-message prefixes** of the talk dialog
(BT-176, resolved in the spec-room register: modes all, allies, non-allies, custom list;
prefixes digit, `A`, `E` before a comma, colon or semicolon; 64 characters of the formatted
line on the wire) and the plus-command fall-through (BT-177); `BT-INGAME-DIALOGS-PARITY.md`
rule 3 holds the talk dialog itself. The `0x19` pause and speed payload is BT-178. The
Options sliders that set the line count and delay are in `BT-GAME-SETTINGS-PARITY.md` rule 6.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\network-net-chat.md`). The
  Options chapter `frontend-options.md` was read for the slider ranges that feed this ring.
  Nothing else under that tree was opened: no source, no decompilation, and none of the source
  links in the prose were followed. Addresses are the ones the chapter names; rules are
  restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; BT-176 to BT-178
  are not re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** Several findings are about what a
  player sees on screen; the Gaps table says which need a play-test.
- **The RWE half** is a read-only audit of `game/GameScene_audio.cpp` (`printConsole`,
  `renderConsole`), `game/GameScene_chat.cpp`, `game/chat_util.*`, `game/GameScene_input.cpp`
  (F3 and F12), `game/GameScene_commands.cpp` (the "Under Attack" line), the TA-compatible
  path (`game/GameScene_tahost.cpp`, `game/GameScene_tad.cpp`, `puppet/TadPuppetDriver.cpp`,
  `io/tad/tad_events.*`) and `docs/TA-DEMOS.md` near the `0x05` entry, at the working tree of
  2026-10-17. RWE's own lockstep chat is RWE-original (it sends text and formats at the
  receiving end) and is **not** audited against the reference; only what touches a TA peer and
  the shared on-screen list is. Nothing was built, run or changed. Another agent had
  uncommitted edits in the tree, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The ring: record layout and the capacity arithmetic
3. Appending a line
4. Expiry
5. Which lines are drawn, and how
6. Sending a chat line, and what a TA peer receives
7. The jump to the unit a line was about
8. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| ring layout, append | none | rules 2, 3 |
| expiry | `BT-INGAME-DIALOGS-PARITY.md` rule 8 says "the expiry of old messages is `printConsole`'s five seconds (a presentation counterpart)" without a figure from the original | rule 4, **CONFLICT** on the number |
| draw and filter | `TOTALA-EXE-INTERFACE.md` near 547 lists the `MAXLINES` and `TXTSCROL` gadgets only | rule 5 |
| send formatting | BT-176, BT-177 (recipients and prefixes) | rule 6 adds the label format and the TA-peer consequences |
| unit jump | `TOTALA-EXE-KEYBOARD.md` 133 and 394 (F3 "jump to the unit that last reported", marked missing); `BT-INGAME-KEYS-PARITY.md` rule 8 (F3 differs) | rule 7 gives the algorithm |

## 2. The ring: record layout and the capacity arithmetic

**Reference.** The game owns **thirty fixed records of 72 bytes**: 64 text bytes, a
simulation-tick timestamp, a 16-bit unit id, an 8-bit sender-player slot and a flags byte. The
low four flag bits are the line's **category**; bit `0x10` means "visited by the unit jump" and
`0x20` means "highlighted". Read and write indices, the display filter mode, the scroll
duration and the font are separate game fields. There is no allocation per message, no
serialisation of the ring in a save or on the network, and a unit id is a bare number (reuse of
an id can retarget a retained jump). The configurable **line count** is not a resized ring: the
physical storage always cycles through thirty, but the "is it full" test compares the next write
position modulo the line count with the read position, and when it matches the read position
advances (the oldest line is dropped). So with the default of ten lines the list holds **at
most nine** lines, and a user setting above thirty is not guarded.

**RWE: DIFFERS in shape, MATCHES in intent.** RWE has one shared list: `consoleMessages`, a
deque of text, an optional colour and an expiry tick (`GameScene.h` about 955), written by
`printConsole` for chat, the unit notifications, the countdown, defeat lines and the in-game
menu's feedback alike. That is the same "one HUD list that is not a chat-only queue" the chapter
describes. It keeps no category, no unit id, no sender slot and no flag bits, which is why
rules 5 and 7 have nothing to attach to. It caps at **eight** lines (`printConsole` about
152-160). With the original's default line count of ten (`BT-GAME-SETTINGS-PARITY.md` rule 6) the
original shows nine. Kind: presentation, one line.

## 3. Appending a line

**Reference.** An empty string and a line count of zero both add nothing. Text is copied to 64
bytes with byte 63 forced to NUL, so **63 characters** are kept. The timestamp is the current
simulation tick (not wall-clock time). The low four flag bits are replaced from the supplied
category key and the **high four bits of a reused physical record are kept**, so a stale visited
or highlight bit can survive reuse. The "message arrived" sound plays unless the sender slot is
ten (the system or local-echo sentinel); the sender slot of a real player makes the sound. When
the topmost screen is the timeout screen, the call marks the screen for redraw.

**RWE.**

- **Empty text and length: MATCHES in effect for chat.** RWE's chat bar holds 128 code points
  (`MaxChatMessageLength`, `chat_util.h`; `BT-INGAME-DIALOGS-PARITY.md` rule 3) and
  `sanitizeChatText` strips controls; the console draws what it is given with no 63-character
  cut. A long line is wider than the original's would ever be. Presentation only.
- **The "message arrived" sound: MISSING.** Nothing plays when a chat line from another player
  arrives (`printChatLine` and `receiveChatMessages` make no sound call). The chapter does not
  name the sound; the register has no entry for it. Kind: presentation; which sound is
  **UNCLEAR** and wants a spec-room read of the sound key.
- **Timestamp in simulation ticks: UNCLEAR.** RWE stamps with `sceneTime` (the scene's own
  tick counter); whether that advances while a lockstep game is paused by the menu was not
  traced. The original's lines do not age while the simulation is paused.
- **Stale high flag bits, the timeout redraw: not applicable.** RWE has no flag byte and no
  timeout screen.

## 4. Expiry

**Reference.** One line is retired per call, and only the **oldest** is tested: it goes when its
timestamp plus `(textScroll + 1) * 30` ticks is strictly less than the current tick (a line
exactly at the deadline stays). `textScroll` is the Options "text delay" value; the Options
page shows the bare value in seconds, so the effective life is **the setting plus one second**.
Its default is ten (`BT-GAME-SETTINGS-PARITY.md` rule 6: "`textlines`, `textscroll` and
`mousespeed` default to ten"), its slider maximum is twenty, and `frontend-options` confirms the
slider shows plain seconds "without adding the extra second used by HUD expiration".

**RWE: DIFFERS, and the number is a CONFLICT.** Every console line lives **five seconds**
(`printConsole`: `5 * 30` ticks; `renderConsole` pops expired lines). The code comment and
`BT-INGAME-DIALOGS-PARITY.md` rule 8 both call this the original's behaviour, but nothing in the
register or the reference gives five for the message list: the only five in the register is the
self-destruct countdown (`TOTALA-EXE-ECONOMY.md` near 311, a different setting). Read together,
the reference's arithmetic and the settings default give **eleven seconds** for an untouched
install. The chapter also says only the front record is tested per call, so a long-lived
front line holds the ones behind it; with a constant delay that costs nothing, and with RWE's
independent expiry nothing is lost either. Kind: register versus reference (the five is an
inference, not a recorded finding). Settled by a play-test (print one line in TA and time it) or
a spec-room read of where the delay default is stored.

## 5. Which lines are drawn, and how

**Reference.** The draw pass returns when the line count is zero. Starting at the write index it
walks **backwards at most line-count minus one records** or until the read index, then draws
forward. Filtering happens **after** that selection: a hidden line is skipped and **not
replaced** by an older visible one. The filter mode (Options "screen chat") is:

| Mode | Shown categories |
| --- | --- |
| 1 | undefined: for category other than two the show flag is set to zero; for category two it is never assigned, so the result reads leftover state |
| 2 | everything except category eight |
| 3, screen-chat off | categories one, four and eight only |
| 3, screen-chat on | everything |
| any other value | nothing |

Lines draw in the Comix font from y 52 downward, advancing one font height per shown line, in
GUI palette colour fifteen (ten when highlighted) with key colour `0xFE`. A sender slot other
than ten draws that player's logo first: a square about eight tenths of a font height at x 138,
and the text moves right by about one and a half icon widths. There is no range check on the
sender slot and no check that the player is active.

**RWE.**

- **Category filtering: MISSING.** There are no categories, so mode 2's "hide category eight"
  and mode 3's three-category set cannot exist. `BT-GAME-SETTINGS-PARITY.md` rule 3 already
  records `screenchat` as absent from `rwe.cfg` and the options page. What the categories are
  (the chapter says only that callers define them) is **UNCLEAR**.
- **Player logo: MISSING.** RWE colours the whole line with the speaker's colour instead
  (`printChatLine`, `playerColorToRgb`). Same information, different drawing; presentation.
- **Placement: DIFFERS.** RWE draws at the top left under the resource bar, newest at the
  bottom, in the speech font with a 14-pixel step (`renderConsole` about 164-182). The
  original's list starts at y 52 and its icon at x 138; whether those are on the same screen
  rows depends on the HUD layout and was not compared. Presentation.
- **Line count: DIFFERS** (rule 2): eight against nine at the defaults, and not adjustable
  (`MAXLINES` is a disabled gadget, `GameScene_menu.cpp` about 743).

## 6. Sending a chat line, and what a TA peer receives

**Reference.** `SendChatMessage` builds a 200-byte local string with an unbounded formatted
write: the sender's full name in angle brackets, optionally an arrow and a recipient label
inside the brackets, a space, then the text. It sends that string through the packet layer,
optionally reports it for online mission type three, and adds a **local copy** to the ring with
unit id zero and sender sentinel ten. The recipient parameter only changes the **label**; the
destination list is chosen elsewhere (BT-176). The send and report results are ignored, so the
local echo does not prove delivery. On the wire the line is the `0x05` subpacket: 65 bytes, a
64-byte NUL-terminated body (`docs/TA-DEMOS.md` near 451); the body therefore carries the
`<Name>` label **inside the text**.

**RWE.**

- **Inbound label: DIFFERS (a probable code fault).** The corpus fixture is
  `<Albert-the_8th> wtf isw this` (`tad_events.test.cpp` about 595-606): the label is part of
  the wire text. Both TA paths hand that text to `printChatLine`, which prefixes
  `playerDisplayName(sender) + ": "` (`GameScene_tahost.cpp` about 304-307,
  `GameScene_tad.cpp` about 56-59, `chat_util.cpp` `formatChatLine`). A line from a TA peer
  therefore reads `Name: <Name> text`, with the name twice. Traced by reading, not run. The
  fix is to print TA text as received (or strip the leading `<...>`), colour by the sender.
- **Outbound to a TA peer: MISSING, silently.** There is no `0x05` encoder anywhere under
  `io/tad/` or `net/ta/` (only decoders, `tadDecodeChat` and `tadDecodeAllyChat`). In an
  own-clock TA-hosted game the chat bar still opens and accepts a line; `sendChatMessage` calls
  `gameNetworkService->submitChatMessage`, which posts to the lockstep endpoints (none in this
  mode), returns success, and RWE prints the line locally ("it never comes back"). **The TA
  player never sees it and the RWE player is told nothing.** To be received by a real TA the
  line must be `<Name> text` cut to the 64-byte body of a 65-byte `0x05` record (BT-176 gives 64 characters of the formatted line; whether the 64th is the terminator was not settled), queued on the
  UDP or TCP path like any other subpacket. Recipient modes (BT-176) then decide who gets it;
  in a two-player game nothing more is needed. This is the single most visible gap in the
  TA-compatible path and has no mention in `TA-NETWORK.md`.
- **Ally chat on a live peer: UNCLEAR.** RWE decodes `0xF9` (73 bytes: two DirectPlay ids and a
  64-byte body) and prints `(ally) ` before it. The reference's receive splitter scans only
  command kinds two to forty-four and **stops at any other kind**
  (`BT-NETWORK-PACKETS-PARITY.md` rule 6), so a live TA's own receiver would not walk a `0xF9`
  record at all; the code looks like a recorder construct (a demo file is a recording, not a
  live stream). How a live TA sends allied chat (presumably a `0x05` addressed to the allies
  only) is the open point behind BT-176; until then the `(ally)` path may never fire on a live
  peer. Settled by a capture of an allied chat line between two real TAs.
- **Overflow, ignored results, mission-type-three reporting: not applicable.**
- **RWE lockstep chat: RWE-original, not audited.** The one place it touches the reference is
  the local echo (the original also echoes locally with the sentinel sender), which RWE does.

## 7. The jump to the unit a line was about

**Reference.** Records with a nonzero unit id are jump targets. `ScrollToNextMessageUnit`
walks from the read index to the write index (wrapping at thirty), picks the first record whose
unit id is nonzero and whose **visited bit is clear**, looks the unit up and requires the
**live** flag; it then sets visited and highlight (`0x30`), **centres the camera on the unit's
whole X and Z** and returns success. A dead or missing unit is skipped and **not** marked
visited. `CycleMessageUnits` (`0x464000`, the F3 key per `TOTALA-EXE-KEYBOARD.md` 133) first
clears the highlight bit on all thirty records and tries the scan; if nothing is found it
clears the visited bit on all thirty and scans once more, so repeated presses **cycle through
the reporting units** and wrap. It touches no indices, categories or text. The lookup has no
ownership or line-of-sight test.

**RWE: DIFFERS** (also `BT-INGAME-KEYS-PARITY.md` rule 8). F3 jumps to the **position of the
last hit** on an own unit (`lastAttackPosition`, `GameScene_input.cpp` about 216-223;
`GameScene_commands.cpp` about 1700). It stores no unit and no list: one press repeated does
not advance to an earlier report, a unit that has since died still draws the camera to where it
was, and non-attack reports (unit complete, self-destruct) are never jump targets because they
carry no unit id. The refreshed point of the register: `TOTALA-EXE-KEYBOARD.md` 394 still says
F3 is "missing"; it is wired, with the different meaning. Kind: input, not hash-visible.

---

## 8. Gaps to fix

Ranked by what a player would notice. None touches the simulation (the ring is presentation
and, in a lockstep game, local).

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Chat typed in a TA-hosted game never reaches the TA player, and the typist is told nothing.** No `0x05` encoder exists. | MISSING | code: encode `<Name> text` (64-byte body, 65-byte record) and queue it; refuse with a console line until then |
| 2 | **A TA player's chat prints with the name twice** (`Name: <Name> text`). | DIFFERS (probable fault) | code: print the received text, or strip the leading label |
| 3 | **Message life is five seconds in RWE; the reference and the setting default give eleven.** The five is an unsourced inference in a comment and in `BT-INGAME-DIALOGS-PARITY.md`. | CONFLICT (register) | play-test with TA, or spec-room read of where the delay is stored; then make the life the setting |
| 4 | **F3 jumps to a point, once.** The original walks the reporting units, skips dead ones, wraps, and centres on the unit. | DIFFERS | code: keep a short list of (unit id, tick) and a visited set; spec text above |
| 5 | **No sound when a chat line arrives**, and the sound is unnamed. | MISSING | spec-room read of the sound key; then one call in `printChatLine` for another player's line |
| 6 | **Nine lines in the original at the default setting, eight in RWE**, not adjustable. | DIFFERS | decision with the `MAXLINES` gadget (settings audit rule 6) |
| 7 | **Live ally chat as `0xF9` may not exist on a live peer.** | UNCLEAR | a capture of an allied line between two TAs (also settles BT-176's wire half) |
| 8 | **Category filter and sender logo.** | MISSING, presentation | decision; categories need their meaning read first |
| 9 | **Whether lines age during a menu pause.** | UNCLEAR | trace `sceneTime` against `setMenuPause` |

No source file was changed.
