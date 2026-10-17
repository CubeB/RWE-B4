# In-game dialogs, the score panel, music activity and simulation scheduling parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-INFO-PANEL-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `ingame-dialogs` (23 services from `0x493340` to `0x495860`): the share
dialog and the unit gift, the talk dialog and its recipients, the unit-information dialog, the tab
menu and the HUD layer callbacks, the sliding score panel, the combat-activity sampler that picks
background music, the frame pacer, the simulation step loop and the debug hotkey dispatcher. This
is the finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

Two halves of the chapter are covered elsewhere and only cross-referred. **Pacing and the step
order** are chapter 02 of the reference, which `TA-SYSTEMS-PARITY.md` rules 1 to 6 audit, with
BT-1 and BT-23 to BT-25 open in the register. **The music side** is `TOTALA-EXE-MUSIC.md` sections
45 to 49, which already decodes the evaluator this chapter describes. The score panel is
`TOTALA-EXE-INTERFACE.md` section 108, decoded from the executable and measured against the
original in play. So most of what follows is a cross-check; the new material is the share and
talk dialog rules, the tab menu, and a few places where this chapter and section 108 disagree on
detail.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ingame-dialogs.md`). Nothing else
  under that tree was opened: no source, no decompilation, and none of its source links were
  followed. Addresses are the ones the chapter names; rules are restated in prose. The spec-room
  discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read,
  read-only. The rows this audit touches are BT-1 and BT-23 to BT-25 (the step order and pacing),
  BT-27 (automatic sharing), BT-55 (the property text formulas), BT-57 (allied map sharing) and
  BT-88 (the ownership-transfer callers); none is re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no game, network session, GUI interaction or audio was run. Weaker than
  `TOTALA-EXE*.md`, and weaker than section 108's play-test. Disagreements are marked **CONFLICT**
  with no winner picked.
- **The RWE half** is a read-only audit of `game/GameScene_chat.cpp`, `game/chat_command.*`,
  `game/chat_util.*`, `sim/ResourceSharing.*`, `game/GameScene.cpp` (`updatePanelSlide`,
  `updateStatsBarSlide`), `game/panel_slide.*`, `game/GameScene_render.cpp` (`renderSpaceTabs`),
  `game/GameScene_audio.cpp` (`updateMusic`, `addBattlePoints`), `game/GameScene_commands.cpp` (the
  battle-point call sites) and `game/GameScene_input.cpp` at the working tree of 2026-10-17.
  Nothing was built, run or changed; another agent had uncommitted edits, so every line number is
  "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. The share dialog and the unit gift
3. The talk dialog: recipients, drafts and the plus path
4. The unit-information dialog
5. The tab menu and the HUD layer callbacks
6. The sliding score panel
7. Combat activity and the music category
8. Frame pacing, the step loop and the debug dispatcher
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| share dialog, unit gift | `TOTALA-EXE-COMMANDS.md` section 124 ("the share dialog uses the same transfer"); BT-57, BT-88 | the dialog is **MISSING**; rule 2 |
| talk dialog | `TOTALA-EXE-KEYBOARD.md` section 73 (Enter), `TOTALA-EXE-COMMANDS.md` section 124 (the plus path) | recipients and the plus path are new; rule 3 |
| unit-information dialog | `TOTALA-EXE-KEYBOARD.md` section 80 (F1 "different"), BT-55 | **MISSING**; rule 4 |
| tab menu | `TOTALA-EXE-KEYBOARD.md` section 80 (Tab "different") | **MISSING**; rule 5 |
| score panel | `TOTALA-EXE-INTERFACE.md` section 108 | agrees in outline, differs on four details; rule 6 |
| combat activity, music category | `TOTALA-EXE-MUSIC.md` sections 46, 47 | agrees; one ordering question; rule 7 |
| pacing, step loop | `TA-SYSTEMS-PARITY.md` rules 3, 4, 6; BT-1, BT-23 to BT-25 | covered; rule 8 |
| debug dispatch | `TOTALA-EXE-KEYBOARD.md` section 77 | not applicable; rule 8 |

## 2. The share dialog and the unit gift

**Reference.** The share dialog (`SHARE.GUI`) is refused to a local player whose info bit 6 (a
watcher) is set. Two sliders, METAL and ENERGY, run from zero to the player's **current stored
balance truncated to an integer**, with no live refresh while the dialog stays open. Its player
list holds every player who is active, of controller type one, two or three, whose slot is not ten,
who either still owns units or has created none, who is **not** controller type one (the local
seat) and not a watcher; the list stores network ids, not slot numbers, so a row is not a slot.
With nobody eligible the screen closes before the list is configured (and the name buffer
leaks). OK rechecks that the chosen player is still participating, still alive or not yet used and
not a watcher, then transfers. The METAL slider calls the routine whose source name says *energy*
and the ENERGY slider the one that says *metal*; the fields they move are the matching ones, so
the names are reversed and the behaviour is right (`TOTALA-EXE-COMMANDS.md` section 124 says the
same). The callee clamps to the source's current balance, so the slider's old maximum is not the
final amount. A second button gifts the **selected units**: those whose low two state bits are not
two, with no carrier or cargo link, and of a type outside the **Commander** category; a gift does
not carry the cargo of a transport. A third sends the player's map to the recipient through both
the local and the network share, and **neither the gift nor the map requires an alliance** in this
handler. Cancel plays the previous sound; neither button calls a close itself.

**RWE: MISSING (the dialog), MATCHES (the transfer).** There is no `SHARE.GUI`, no `h` key and
no SHARE button handler (`TOTALA-EXE-KEYBOARD.md` section 80). What exists is the typed
`+Give player amount metal|energy` (`GameScene_chat.cpp` about 175-210,
`ResourceSharing.cpp` about 175-205), which does not require an ally (as in the original) and
clamps to the donor's stock. Against the chapter:

- **No eligibility test on the recipient.** The dialog refuses an eliminated player (one that
  created units and has none) and a watcher; `+Give` accepts any slot but the giver's own, living
  or not (`applyPlayerShareCommand` tests the *issuer* is alive, not the target). A gift to an
  eliminated player is harmless (their stock is ignored) but it is not the original's refusal.
- **No unit gift.** Already BT-88: the register records three callers of the ownership transfer
  (capture, a network packet, the share dialog) and RWE has no gift command. The commander
  exclusion, the carrier and cargo exclusion and the state-bit test are the rules a port needs.
- **No manual map share.** BT-57: the dialog's third button, the periodic push and the received
  packet are the three callers of the visibility copy; RWE shares sight between allies
  continuously instead (`GameSimulation_visibility.cpp` about 600-640). A resolved row.
- **The amount.** The slider is an integer to the stored balance; `+Give` takes any integer,
  negative ones refused (a hardening the register records).

## 3. The talk dialog: recipients, drafts and the plus path

**Reference.** The talk dialog is refused to a watcher and while the unit-information modal is up.
A 129-byte **draft** is initialised once and survives closing: the expanded layout (`TALK2.GUI`,
selected when lobby flag `0x100` is set and the game is a network game) can be switched with the
SENDTO button, which copies the draft, closes the layer and reopens the other, so the text is kept.
A SENDTYPE button cycles a **chat mode**: zero everybody, one allies, two non-allies, three a
custom recipient list built from the LIVEPLYR buttons (one byte per player; the suffix is not
range-checked). Submitting copies up to 255 characters, skips leading spaces and then:

1. A leading `+` hands the rest to the command interpreter with permission mask one (mask seven
   with the cheat UI option, and the cheat bit added in non-campaign play); **if the command's
   result contains the cheat bit the send mode becomes "all"**. The chapter notes that
   "command execution does not automatically consume the text from the later chat path".
2. A text that begins with one digit, `A` or `E` followed by a comma, colon or semicolon is a
   one-message override: a digit selects that player (when its network id is nonzero) and
   temporarily clears all eleven recipient bytes, `A` selects allies, `E` selects enemies; the
   prefix is stripped.
3. The message is sent with a label (the player's name, "Allies" or "Enemies"), then the previous
   mode and recipients are restored; the draft and the expanded flag are cleared. An invalid
   numbered target reaches the clear step without sending. The saved recipient structure is ten
   bytes and the temporary clear is eleven, so the eleventh byte is not restored.

**RWE: DIFFERS (a one-line bar with no recipients), MATCHES on length and the plus path's local
half.**

- **Length: MATCHES.** The bar holds 128 code points (`MaxChatMessageLength`, `chat_util.h`), the
  original's 128 characters plus the terminator.
- **Recipients: MISSING.** Every line goes to every peer (`submitChatMessage`,
  `GameScene_chat.cpp` about 68-96). There is no allies mode, no enemies mode, no custom list, and
  neither the `A,` / `E,` nor the `3:` prefix; a line that starts with one of them is chat to
  everyone, prefix included. In a two-player game nothing is lost. In a team game, a player cannot
  speak to allies alone, and **a prefixed line meant privately goes to the enemy**. Kind: gap with
  a player-visible privacy consequence.
- **Draft lifetime: DIFFERS.** Escape clears the bar (`handleChatBarKey` about 53-57); the
  original keeps the draft across closing and across the SENDTO layout switch. RWE has no second
  layout, so only the Escape case matters.
- **The plus path: UNCLEAR, a CONFLICT with the code comment.** RWE treats any line that parses as
  a command as **never chat** (`sendChatMessage` about 78-83: "the original's talk dialog hands it
  to the command interpreter locally, so it must not reach the other players' screens either").
  The chapter says execution "does not automatically consume" the text and that a cheat result
  forces the mode to *all*, which only makes sense if the text goes on to be sent; so in the
  original a typed `+ShareMetal` may also appear in other players' chat. `TOTALA-EXE-COMMANDS.md`
  section 124 says only that the rest "is handed to the command interpreter locally". Neither
  source decides. Settled by a two-machine play-test (type `+ShareMetal` and look at the other
  screen) or a spec-room read of the dialog's submit path. Kind: design against the chapter.
- **Permission masks, the cheat bit and the cheat table: not applicable.** RWE registers none of
  the cheats (`chat_command.h` says so).
- **Draft/transport, `ResetPlayerGadgets`, the unchecked suffix: not applicable.**

## 4. The unit-information dialog

**Reference.** F1 without Shift opens `UNITINFOx.GUI` for the unit under the hovered gadget (the
first sixteen bytes of its name resolve a type) or, failing that, for the hovered unit **if it is
visible to the viewing player**; no fallback from the gadget route to the world route. It loads the
unit's picture, builds a property list (Cost, Energy, Metal, Build Time, Statistics, Max Velocity,
Acceleration, Turn Rate, with labels from Y 32 in steps of fifteen and values from X 240) and sets
the display name. The values are the definition's, not a simulated build time. The property text
formulas are BT-55.

**RWE: MISSING.** F1 is RWE's help overlay (`GameScene_input.cpp` about 161-164). The bottom
footer for a hovered or selected unit (`BT-INGAME-INFO-PANEL-PARITY.md` rule 7) is the nearest
equivalent and shows other fields. No picture dialog, no property table. BT-55 already records the
formulas; the gap is the dialog.

## 5. The tab menu and the HUD layer callbacks

**Reference.** Tab in a network game opens `TABMENU.GUI` (the sliding menu bar) when the
orders-panel bit two is clear, hides the cursor, loads the layer with flag `0x800` and enables key
commands; with any of the lobby bits `0xE0` set it clears them and closes the top screen only if it
is `TABMENU.GUI`. The buttons ALLIES, SHARE and CONTROL are enabled by a **candidate count** that
skips active controller-type-one players and any player with the watcher bit, and then **counts
every remaining slot, inactive ones included**; the chapter notes the count is not "live
non-allied opponents" as the overview claims. ALLIES and SHARE need a positive count and a
non-watcher local player; CONTROL needs lock flag bit zero clear and the local player to be the
host, with no count test. OPTIONS opens the in-game options and sets the panel flag; the other
buttons open the share, control and allies dialogs. `HandleMain2LayoutEvent` only clears a
non-negative current gadget.

**RWE: MISSING.** Tab opens the options panel in every game type (`GameScene_input.cpp` about
68-72, the comment at `GameScene_menu.cpp` about 327 says so: "the sliding TABMENU bar it shares a
key with is multiplayer-only"). RWE has no in-game ALLIES dialog (alliances are fixed at the
start), no CONTROL (host controls), and no SHARE. The candidate-count quirk, counting inactive
slots, is worth keeping if the bar is ever ported: with one human and eight empty slots ALLIES is
still enabled. Kind: gap, multiplayer only.

## 6. The sliding score panel

**Reference.** `FlashScorePanelKillLoss` sets the indexed kill and loss **flash bytes to thirty**
(no index check); the draw routine subtracts two from positive flash bytes each time its decay
deadline passes (an odd byte value would underflow). The panel opens when visual flag `0x80` (the
F4 latch) is set **or** the space key is down without a type-three text-entry focus, and closes
otherwise. The slide position runs zero to 125; **each draw moves one quarter of the remaining
distance, at least one**, and plays the `Panel` and `Options` sounds at the boundaries; it is not
scaled by elapsed time. The panel's left edge is the **screen width minus the slide position** at
Y 32, 125 wide, with a **faded background sized by the number of players times forty plus
fourteen**. It draws translated "Kills" and "Losses" headings and then searches **ranks in order**;
a row qualifies when the player is active (controller types one, two, three), the slot is not ten,
the player is **not eliminated after having created units**, and the watcher bit is clear. The
local player gets extra fades; the coloured `32xlogos` frame is mapped across each row, then the
name and the counters, the flashes passed to the text draw. When the commander-death mode is
**two** (BT-83's Deathmatch) it shows *commander* kills and losses, otherwise the ordinary
counters. If no eligible player holds the expected rank, **drawing decrements the ranks of the
higher eligible players to close the gap** (the draw mutates ranking state).

**Register.** `TOTALA-EXE-INTERFACE.md` section 108: the list is drawn by `0x4948E0` on the side
panel's slide (`0x51F2D8`, 0 to `0x7D`) and gate (the F4 latch, or Space with the cursor off the
panel); the box is `40 x rows + 46` tall at Y 32 with x equal to the screen width less the slide
plus 125; order is a stored rank byte closed up when a player drops; a row is skipped for an
unused record, a state outside one to three, a record whose `+0x146` is ten, a record that **has
and has had no units**, or bit `0x40` of the colour sub-record; no alliance or visibility test; the
mode-two counters were "not found".

**RWE: MATCHES the register and the measurements, DIFFERS from the chapter on four details.**
`renderSpaceTabs` (`GameScene_render.cpp` about 560-690) draws a 125-pixel list at the top right on
`playerListSlide`, gated by `playerListWantsOut` (F4, or Space with the cursor off the panel,
`panel_slide.cpp` about 7-16), with the header 16 pixels deep, a plate every 40 of the player's
`32xlogos` frame stretched to 112 by 37, the name above the counters, and no fill behind it.

- **CONFLICT: which rows drop out.** The chapter drops a player *eliminated after having created
  units*; section 108 drops a record that *has and has had no units*, which is the opposite end
  (never played versus played and lost). The register's own "closed up when a player drops"
  and the chapter's rank repair both fit the chapter's reading (a defeated player's row goes).
  RWE keeps a player who has lost, and its comment says that is "how the losses show" (about
  667-669); it lists **every** player in the simulation (which presumably holds only seated players, so
  the eliminated case is the whole difference) and the watcher bit has no counterpart. Kind: register, design. Settled by a play-test in a game with a defeated
  player.
- **CONFLICT: the background.** The chapter sizes a **faded background** at players times forty
  plus fourteen (the total player count, not the row count); section 108 decoded forty times the
  *rows* plus **forty-six** and noted it is thirty pixels taller than the plates need; the screenshot
  showed no fill. A "faded" fill that is nearly transparent could be invisible on a dark map, which
  would explain the screenshot; the 14 against 46 and rows against players are two different
  readings of one box. RWE draws none. Presentation. Settled by a bright-map screenshot.
- **The slide: DIFFERS in curve (presentation).** RWE moves a constant pixels per second
  (`advancePanelSlide`); the original moves a quarter of the remainder per draw, so it decelerates
  and its speed depends on the frame rate. The two sounds are played at the ends in both, with
  which belongs to which unconfirmed (about GameScene.cpp 486-498).
- **Kill and loss flashes: MISSING.** No flash on a change of either counter.
- **Commander mode: MISSING.** RWE has two commander-death modes (`GameEnds`, `GameContinues`,
  `GameParameters.h` about 124-130) and no Deathmatch (BT-83's mode two), so the panel always shows
  unit counters. `BT-GAME-PLAYERS-PARITY.md` rule 7 holds the respawn rule that mode two needs.
- **Ranks.** RWE lists in slot order; the original in a stored rank byte that starts as the slot
  and is closed up as players drop. Identical until the first drop.

**On the F4 latch.** The chapter's slide position is used *only* to place the score panel; nothing
in the chapter makes the side panel move with it. `TOTALA-EXE.md` section 76 inferred that the
side panel is what slides on the F4 latch and `TOTALA-EXE-INTERFACE.md` section 108 says that was
inference, "what F4 does to the side panel in the original has not been checked in play". RWE still
moves the side panel and the minimap with F4 (`GameScene.cpp` about 455-510, `panelSlide`).
**CONFLICT, weak:** the chapter is absence of evidence rather than a finding, but it is the second
source after section 108's caveat, and the F4 side-panel slide is RWE's whole F4 behaviour besides
the list. Kind: register (section 76). Settled by one play-test of F4 in the original.

## 7. Combat activity and the music category

**Reference.** The activity ring has thirty one-second buckets. A hit adds one when an identified
attacker or victim belongs to the local player; a kill attributed to the local player adds five.
The sampler runs only in a network-flagged game, only once the timer has passed the last sample
plus thirty, and **once per call** (a long gap produces one sample, not a refill of the missed
buckets). It counts samples since the last change; once the count is **more than ten** it sums
all thirty buckets *starting just before the current write index* and, separately, the first
five encountered (the recent activity). In category zero (building), a total above fifty or a
recent sum above thirty, **and** a local unit count above thirty, selects category one (battle); in
category one, a total below ten, a recent sum of zero **and a count above sixty** selects category
zero. Other categories do not transition. A change updates the sound category and resets the
count. Only **after** the sums does it advance and wrap the write index, clear the new bucket and
record the sample time.

**Register.** `TOTALA-EXE-MUSIC.md` section 47: the same numbers and gates, "at least 10 s since the
last change (`cmp eax,0xa; jle skip`)" and "at least 60 s since entering Battle"; "each run advances
the ring and zeroes the new slot" with the order not stated.

**RWE: MATCHES the gates and numbers; two details differ or are unsettled.**

- **The ring order: UNCLEAR, a possible DIFFERS of one second.** RWE advances the cursor and clears
  the new bucket **first** and sums afterwards (`updateMusic`, `GameScene_audio.cpp` about
  365-381), so its thirty-second total is 29 completed seconds plus an empty bucket, and its
  five-second total is the empty bucket plus the **four** most recent completed seconds. The
  chapter sums first and starts "just before" the write index: its five-second figure is the five
  seconds *before* the one that just ended (it does not include the newest bucket at all), and its
  thirty-second figure includes all thirty. Read as written, the original reacts to a burst of
  fighting one second later than RWE's four-bucket window does and holds a thirty-second window
  where RWE holds 29. Section 47 does not say which side of the advance the sums sit. Effect: a
  threshold crossed by a margin of one bucket. Settled by a spec-room read of the sampler's order.
- **The boundaries: UNCLEAR, within a second.** "More than ten samples" and "above sixty" are
  strict, so the original needs eleven and sixty-one one-second samples; section 47 says "at
  least". RWE waits a ten-second lockout and a sixty-second stay measured from the **end of the
  fade** (`musicLockoutUntil` and `battleEnteredTime`, about 427-433), where the original resets
  the count when the state changes, before the fade. So RWE's first possible switch back comes
  between zero and two seconds later than the original's.
- **The gap behaviour: MATCHES.** RWE evaluates when the game second changes and, after a long
  pause or a stall, takes one step, not one per missed second.
- **Where the points come from: MATCHES.** `addBattlePoints(1)` on a weapon hit touching the local
  player (`GameScene_commands.cpp` about 1694), `(5)` on a kill credited to it (about 1758).
- **The unit-count gate: MATCHES** (own, alive units, strictly more than thirty).
- **Categories two to four and the network flag: not applicable.** Victory, Defeat and silence
  are never entered in play (`TOTALA-EXE-MUSIC.md` section 46), and the register's gate bit is set
  at mission start, so the sampler runs in single-player too, as RWE's does.

## 8. Frame pacing, the step loop and the debug dispatcher

**Reference.** The pacer converts elapsed timer ticks to whole simulation steps with a fractional
carry (rate = effective speed x 0.1; a lag of at least 900 behind the slowest remote player
multiplies it by `(3600 - min(lag, 3600)) / 2700`, floor 0.01), zeroes the steps when paused,
caps at **five** and runs a hysteresis counter (above ten capped observations lower the effective
speed, below -100 raise it); the discarded steps are not returned to the carry. The step loop copies
the pending count, does not clear the original, and for each step advances the tick and runs:
packets (only when its argument is nonzero), units, projectiles, explosions and debris, players,
features with the global GAF sequences, wind, meteors and the camera follow, particles, blink, and
local resource sharing with the queued-packet flush (only with the argument and a packet manager).
The argument named "show stats" **controls the actual networking**. After all steps three
post-step hooks and the oldest-message and eyeball-marker expiries run **once per call, even with
a zero count**. The debug dispatcher has six events (a bit toggle, a mode counter wrapping at
five, two page-flip switches, a hovered-unit poke and a fill-both-stores).

**RWE: covered.** The accumulator, the cap, the lag policy and the tick order are
`TA-SYSTEMS-PARITY.md` rules 3, 4 and 6 and BT-1, BT-23 to BT-25; nothing here adds a rule. The
chapter's table puts packets first and sharing last in each step; RWE's lockstep gate and its
sharing pass are BT-27's. The expiry of old messages is `printConsole`'s five seconds (a
presentation counterpart). The debug dispatcher has RWE's F10 and F11 windows in its place and is
**not applicable**; its fill-both-stores event is the only one that changes a balance and RWE's
debug window has its own cheat for that.

---

## 9. Gaps to fix

Ranked by what a player would notice. None of the presentation rows touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **No recipient control on chat**: no allies-only, enemies-only or custom list, and the `A,` / `E,` / `3:` prefixes go to everyone, prefix included. A private line in a team game reaches the enemy. | MISSING (player-visible) | code: a recipient mode in the bar and in `submitChatMessage` |
| 2 | **No share dialog, no unit gift, no map share, no `h`.** Typed `+Give` is the only manual transfer. | MISSING | `PlayerShareCommand` has the resource half; the unit gift is BT-88 |
| 3 | **The score panel's rows**: RWE keeps defeated players and lists every slot; the chapter drops a player eliminated after creating units; section 108 reads the opposite end. | CONFLICT, register and design | play-test with a defeated player |
| 4 | **No unit-information dialog, no tab menu.** F1 is a help overlay; Tab is the options menu. | MISSING | decision |
| 5 | **Does the original send a typed `+command` to the other players as chat?** RWE says never; the chapter implies yes. | UNCLEAR, design against the chapter | two-machine play-test |
| 6 | **F4 slides the side panel in RWE**; the chapter makes the slide position the score panel's alone. | CONFLICT, weak, register section 76 | one play-test of F4 |
| 7 | **The score panel's background size and fill**, the slide curve, the kill and loss flashes, the commander-mode counters. | DIFFERS / MISSING (presentation) | bright-map screenshot; code |
| 8 | **The music evaluator's ring order and strict boundaries**: one bucket and up to two seconds of difference in when a fight is called. | UNCLEAR, small | spec-room read of the sampler |

No source file was changed.
