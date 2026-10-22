# Battle key dispatch, squads, selection keys, STOP mode and the pause and speed keys parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-INGAME-CONTROL-PANEL-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `ingame-keys` (five services from `0x495860` to `0x496790`): the two
STOP-mode helpers, the numbered screenshot path, the large bitmap capture, and above all the
one-event battle key dispatcher with its modifiers, its Escape, self-destruct and speed rules and
its movie and debug tail. This is the finding that implementation work cites for any of them, per
`AGENTS.md` and ADR-0004.

`TOTALA-EXE-KEYBOARD.md` (sections 72 to 81) already decodes the same dispatcher from the
executable, forty cases and all, and is the more detailed side. So most of this audit is a
cross-check that finds the two sources agree. Its sections 79 and 80 ("what RWE binds today" and
the gap table) are **stale**: they describe the tree of 2026-09-03, before the keyboard commit
`a0de1761` and the camera-bookmark, `,` `.`, `n`, F3 and F4 work. This audit compares against the
tree of 2026-10-17 and says where the register's gap table no longer holds. The numbered
screenshot path and the Ctrl+F9 capture are `BT-GRAPHICS-PCX-PARITY.md` rule 4 and are not
repeated. The camera bookmarks are in `BT-INGAME-CAMERA-PARITY.md`, the share and talk dialogs in
`BT-INGAME-DIALOGS-PARITY.md`, and the speed setter's message and broadcast in
`BT-GAME-GAME-STATE-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\ingame-keys.md`). Nothing else
  under that tree was opened: no source, no decompilation, and none of its source links were
  followed. Addresses and event values are the ones the chapter names; rules are restated in
  prose. The spec-room discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`)
  was read, read-only; BT-104 (the page-step toggle argument) is the one row this chapter touches
  and is not re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed, and the
  chapter says no key event, capture or session was run. Weaker than `TOTALA-EXE*.md`.
  Disagreements are marked **CONFLICT** with no winner picked.
- **The shipped help text** (`gamedata/HELP.TDF` in `totala1.hpi`, read with `tools/hpi.py`) was
  used as a third witness for what the keys are meant to do. It is player-facing text, not a
  decode.
- **The RWE half** is a read-only audit of `game/GameScene_input.cpp` (`onKeyDown`),
  `game/GameScene_menu.cpp` (`togglePause`, `handleEscapeDown`), `game/GameScene_commands.cpp`
  (selection helpers, `PlayerSetGameSpeedCommand`), `game/GameScene_chat.cpp` and
  `game/GameScene.cpp` at the working tree of 2026-10-17. Nothing was built, run or changed.
  Another agent had uncommitted edits in the tree, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Who sees a key first: the gate, the second layer and RWE's order
3. Digits, squads and build pages
4. The selection keys: Ctrl+A, Ctrl+S, Ctrl+Z, Ctrl+C and the category range
5. Escape and STOP mode
6. Self-destruct
7. Pause, game speed and the broadcast
8. Camera, tracking and the message keys
9. Dialog, display and developer keys
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| STOP helpers | `TOTALA-EXE-KEYBOARD.md` section 73 (Escape) | agrees; rule 5 |
| numbered screenshot path, large BMP capture | `BT-GRAPHICS-PCX-PARITY.md` rule 4 (the PCX naming); the BMP poster is a console command | not applicable here |
| key table, event values | KEYBOARD sections 72, 73 (all forty cases, same event values, `0xC5` to `0xCD` squads, `0xE6` to `0xE9` recall) | **MATCHES**, with the conflicts of rules 4 and 6 |
| category range | KEYBOARD section 74 (`CTRL_%c`) | agrees on the range; rule 4 |
| Escape, speed, movie, debug | KEYBOARD section 73; INTERFACE section 70 | agrees; rule 7 adds the speed packet |

## 2. Who sees a key first: the gate, the second layer and RWE's order

**Reference.** The battle key handler is called from the battle pass only when bit 0 of the
interface flag is clear, so a menu being up silences every battle key (and the edge scroll,
`BT-GAME-MAIN-LOOP-PARITY.md` rule 4). A separate application-frame routine intercepts the global
screenshot, fullscreen and dismissal keys before the battle callback ever runs. The handler pops
**one** event per call; event zero returns at once. It samples the Shift state into a modifier
value, and some branches also test the Alt state. At the end of every nonzero event, if the debug
enable bit is set, the debug hotkey dispatcher receives the same event, so one event can have an
ordinary action and then a debug action.

**RWE: DIFFERS in order, MATCHES in effect for the menu.** `onKeyDown` (about 44-428) decides in
this order: Pause (always, even with the chat bar or the menu up, `repeat` ignored); the chat bar
(takes every key); Tab and F2 (the game menu, toggled); the open menu (takes every key, Escape
closes); Enter (the bar); then the panel's quick keys (unless Ctrl is down or the key is Space);
then the battle keys in one chain. A menu open therefore silences the battle keys, as in the
original. Two differences:

- **Pause is honoured while the options panel or the chat bar owns the keyboard.** In the
  original the pause key is an ordinary case of the handler the interface gate switches off, and
  the chat bar is a GUI layer that takes the key first. Pausing a network game from the chat bar is
  harmless; pausing from under the options panel is the same. Presentation, low.
- **One event per call is not modelled**: SDL delivers one event per call to `onKeyDown`, so the
  case of two keys in one frame is the same. Not applicable.

## 3. Digits, squads and build pages

**Reference.** `0xC5` to `0xCD` (Ctrl+1 to Ctrl+9) create squads 1 to 9 and **play the
`CreateSquad` sound**. `0x31` to `0x39` (plain 1 to 9) either select squads or open build-menu
pages 0 to 8, decided by display bit 8 (the `SwitchAlt` value) and the Alt state: with the bit
clear a plain digit is a build page and Alt+digit a squad; with it set the two swap. The Shift
state is forwarded to the squad-select service, which decides additive selection (Alt+Shift+digit
adds). `0xD2` to `0xD5` and `0xE6` to `0xE9` (Ctrl+F5 to F8, F5 to F8) store and recall camera
bookmarks and **play the `SelectSquad` sound** (rule in `BT-INGAME-CAMERA-PARITY.md`).
Membership lives on the unit, is exclusive, and a factory stamps its squad on what it builds
(`TOTALA-EXE-KEYBOARD.md` section 75; `TOTALA-EXE-INTERFACE.md` section 133).

**RWE: DIVERGENCE (documented in the roadmap, not in section 88).** Plain digits recall control
groups, Ctrl+digit binds, Shift+digit adds the selection, Ctrl+Shift+digit adds, and `0` is a
tenth group (about 368-428). `docs/ROADMAP.md` records this as the `SwitchAlt` arrangement ("digits
stay as control groups, the original's SwitchAlt arrangement"). It is the original's *swapped*
mapping, with two departures that nothing records:

- **No Alt, no preference.** The original reads the swap from a saved preference and the Alt state
  and so offers both mappings; RWE offers the swapped one only, so the shipped default (digits are
  build pages, `HELP.TDF` line 32 "1 - 9 Select the menu for the current unit") has no equivalent
  and Alt+digit does nothing. A player can still reach a build page with `,` and `.` (rule 9) and
  the panel buttons.
- **Groups are not squads.** They are sets in the scene (`controlGroups`), not a number on the
  unit, so there is no exclusivity (a unit can sit in two groups), no factory inheritance, and
  Shift+digit adds the *current selection to the group* where the original's Shift on a recall
  adds the *group to the current selection*. The two Shift meanings are opposite. Presentation
  and interface only; nothing reaches the simulation.
- **Both sounds are missing.** Neither `CREATESQUAD` nor `SELECTSQUAD` is looked up anywhere in
  RWE; both are `beep4` in the shipped `allsound.tdf`. The roadmap says the assignment sound "is
  ProTA's rather than TA's"; the register's own section 73 (the `CreateSquad` sound) and this
  chapter both say the original plays it. **CONFLICT** inside the tree, register and chapter
  against the roadmap's sentence.

## 4. The selection keys: Ctrl+A, Ctrl+S, Ctrl+Z, Ctrl+C and the category range

**Reference.** Event `0xAA` (Ctrl+A) "selects all idle units"; `0xBC` (Ctrl+S) selects all
**visible** units; `0xC3` (Ctrl+Z) selects units of the currently selected types; `0xAC` (Ctrl+C)
selects the category `CTRL_C` and then finds the local commander (and, with the register,
tracks it). The category events are `0xAB`, `0xAE` to `0xBB`, `0xBD` to `0xC2` (Ctrl+B, Ctrl+E to
Ctrl+R, Ctrl+T to Ctrl+Y): the seven-character name `CTRL_` followed by the letter, then the
category selection with the **sampled Shift state**. Event `0xBC` is deliberately the visible case,
not part of the generic range.

**Register.** KEYBOARD section 73: Ctrl+A (`0x48BD50`) selects **all** own units, "the whole
array, alive and owned, everywhere on the map"; `HELP.TDF` line 0 says "CTRL+A Select all units".

- **CONFLICT: what Ctrl+A selects.** Register and the help text say *all* units; the chapter says
  *idle* units. **RWE selects every living, non-mission-held own unit whose definition can move**
  (`GameScene_input.cpp` about 311-316, the filter is `canMove`), a third reading: it leaves out
  every building and factory. The commit that wrote it (`a0de1761`) says "selects everything you
  own"; the filter does not do that. Settled by a play-test (Ctrl+A with a factory and a radar
  tower on the map: do they highlight?) or a spec-room read of the selection service the key
  calls. Kind: register.
- **Ctrl+S: MATCHES in rule.** `selectAllOnScreen` (about 2749) selects own selectable units
  inside the camera rectangle. It tests the world position against the camera rectangle; the
  original tests the on-screen unit list. Equivalent except at the edge, where a unit whose model
  sticks into the view but whose centre is outside is on the original's list and not in RWE's.
- **Ctrl+Z: MATCHES.** The selection's own types, all own units of them, `heldByMission` excluded.
- **Ctrl+C: MATCHES in rule, DIFFERS in the commander test.** The original takes the `CTRL_C`
  category token (six shipped units carry it); RWE tests the definition's `commander` flag. Equal
  on shipped data, different for a mod that gives `CTRL_C` to a second unit type.
- **The category range: DIFFERS in extent (mod data only).** The original forms the key name from
  the letter, so any mod that writes `CTRL_E` in an FBI `Category` list gets a working Ctrl+E. RWE
  handles six letters, W, F, P, V, B and R, and ignores every other (about 357-367). The shipped
  data defines exactly those six plus `CTRL_C`, so retail play is unaffected. Moreover Ctrl+G, H,
  I, J, K, L, M, N, O, Q, T, U, X and Y do nothing at all in RWE (Ctrl+N is deliberately left to
  `n`, about 228).
- **The Shift state: MISSING.** All of these selection keys in RWE require Shift **up**
  (`!isShiftDown()`), so Ctrl+Shift+W and the rest do nothing; the original passes Shift to the
  category selection, where it means *add to the current selection*. The register states the
  additive behaviour (KEYBOARD section 74, "Shift adds to the selection"). Ctrl+Shift+C alone
  works in RWE.

## 5. Escape and STOP mode

**Reference.** Two helpers set the interface to the neutral/stop interaction state: they assign
order mode one, clear input flag `0x20`, find a gadget whose name contains STOP in the current
layer and clear its group status. They enqueue **no** order. Escape: with the options panel open,
clear its flag and close the top screen (clearing the unit index) unless the layer is already the
named HUD; otherwise, in a non-neutral order mode, reset to neutral through those helpers; if
already neutral, clear the selection and force-pop floating layers back to the HUD. It does not
stop selected units.

**RWE: MATCHES in effect.** `handleEscapeDown` (`GameScene_menu.cpp` about 1321) closes the debug
window, then the help overlay, then (game over) leaves for the menu, then a cursor mode other than
normal returns to normal, otherwise it clears the selection. The original's "press the STOP
gadget" is replaced by setting the cursor mode, and its "pop floating layers" has no counterpart
because RWE has no floating layers. `TOTALA-EXE-KEYBOARD.md` section 80 already says this. The
two additions (debug window and help overlay close first) are RWE's own surfaces.

## 6. Self-destruct

**Reference.** Ctrl+D (`0xAD`) is a **toggle by orders**: for the current selection it searches
each unit for an existing self-destruct order and deletes the ones it finds, remembering whether
any existed. Only when **none** existed does it issue a self-destruct order to the whole
selection. With a mixed selection, finding one existing timer cancels those and **arms none of the
others**.

**Register.** KEYBOARD section 73 describes it differently: for each selected unit the key looks
up its `SELFDESTRUCT` weapon, and a unit without one takes the generic path. The two are not
necessarily at odds (one is how the order is built, the other is when it is built) but the
mixed-selection rule is in the chapter alone.

**RWE: DIFFERS on a mixed selection.** The key sends one self-destruct command per owned, living
selected unit (`GameScene_input.cpp` about 323-339), and the simulation **toggles each unit
separately** (`GameSimulation::toggleSelfDestruct`, about line 1332). Selecting a unit already
counting down and a fresh one and pressing Ctrl+D cancels the first (as the original) **and arms
the second** (the original does not). With a uniform selection both agree. Not hash-visible in
itself (the command is the same kind), but it changes what the second press does. Kind: design
against the chapter; the register is silent.

## 7. Pause, game speed and the broadcast

**Reference.** Pause (`0xF8`) toggles the pause bit and broadcasts a **three-byte** control packet:
type `0x19`, subtype zero, the new pause bit. The speed keys (`+`, `=`, `-`, `_`; `0x2B`, `0x3D`,
`0x2D`, `0x5F`) are blocked when endgame/debug flag value 2 is set and return when the local
player is active and its player-info bit `0x40` (a watcher) is set; otherwise the requested
speed moves within one to twenty through the speed setter with broadcast on, **with no host-only
test** in the branches. The setter's own packet is type `0x19`, **subtype one**, and the clamped
speed byte (`BT-GAME-GAME-STATE-PARITY.md` rule 4 holds the rest of the setter).

**RWE: MATCHES the keys, DIFFERS on who may change the speed, and the wire layout is a CONFLICT.**

- **The keys: MATCHES.** `=`, keypad plus, `-` and keypad minus (Shift+`=` and Shift+`-` are the
  same keys, so `+` and `_` work) send `PlayerSetGameSpeedCommand` (about 300-310); Pause toggles
  through the network service or the command stream (`togglePause`, `GameScene_menu.cpp` about
  260). A watcher cannot issue them because a recording takes no commands.
- **Who may change the speed: DIFFERS (design).** RWE drops a speed command not issued by player 0
  (`GameScene_commands.cpp` about 3114: "Host-authoritative: only honor speed changes from player
  0"). The chapter has the key work for **any non-watcher**, with a broadcast. The chapter does not
  say what a receiver does with a broadcast from a non-host; that is in the network receive path it
  does not cover. So whether a joiner's `+` in the original changes the host's speed is not
  settled. Kind: design, no register finding. Settled by a two-machine play-test.
- **CONFLICT: the `0x19` payload.** `TA-DEMOS.md` and `TA-NETWORK.md` read the two bytes after the
  type as **one 16-bit number in 256ths of normal speed**, with zero the pause and 256 normal
  (every sender puts 256 on the wire at game start). `GameScene_tahost.cpp` (about 313-331)
  decodes a peer's `0x19` that way: zero pauses, anything else becomes level
  `(value x 10 + 128) / 256` clamped to 1 to 20. The chapter's layout is **subtype, then value**:
  subtype zero is the pause with a pause *bit*, subtype one is the speed with a *level byte* of 1
  to 20. Under the chapter the game-start record (the two bytes 256 = subtype zero, value one,
  little-endian) is a **pause-on**, not a normal speed, and a speed change to level 12 would be
  the bytes subtype one, level 12 = 3073. RWE's decoder would turn that 3073 into
  (3073 x 10 + 128) / 256 = 120, clamped to level **20**; it would read a pause-on (256) as normal
  speed, and a pause-off (value zero in the pause subtype) as a pause. `TA-NETWORK.md` flags the scale as "the one to check
  against a real TA"; this chapter is a second witness that the inference is wrong. No capture of a
  real speed change exists in the corpus. Kind: register. It touches the own-clock host only
  (lockstep games carry the speed as a command), but it is a live fault there.
- **A message on change: MISSING.** The setter prints a translated message when the requested
  speed changes. RWE prints nothing on a key press (the HUD strip shows the speed while Space is
  held, `GameScene_render.cpp` about 623). Presentation.

## 8. Camera, tracking and the message keys

**Reference.** `0x74` and `0x54` (`t`, `T`) cycle camera follow with argument zero and one;
`0xE4` (F3) cycles message-associated units; `0x6E` (`n`) focuses the next local unit; `0xE2` and
`0xE3` with Shift track the hovered unit and the hovered builder for the information displays.

**Register.** KEYBOARD section 73 agrees; `HELP.TDF` line 35 says F3 "Selects the unit that last
reported" and line 20 `n` "Scroll to the next unit off screen".

- **`t` and `T`: MATCHES in rule.** Both start or cycle tracking of the selection sorted by id
  (`startTrack`, `GameScene_commands.cpp` about 534); the argument's effect is not decoded in the
  register either, and RWE does not distinguish the two.
- **F3: DIFFERS.** RWE jumps the camera to the **position** of the last hit on an own unit
  (`GameScene_input.cpp` about 216-223); it neither selects the unit nor walks a message list
  (`TOTALA-EXE-KEYBOARD.md` section 81 item 8 already owes this).
- **`n`: DIFFERS in meaning.** The original scrolls to the next unit that is *off screen*; RWE
  walks **all** own units in id order, selecting and centering each (about 228-267), on screen or
  not. Closer to the help text than the register's gap table suggested ("missing"), but not the
  same rule.
- **Shift+F1, Shift+F2: MISSING**, developer overlays, not worth porting.

## 9. Dialog, display and developer keys

**Reference.** Enter plays `SmallButton` and opens the talk dialog; `h` opens the share dialog
(mission type 3 only); F1 without Shift opens the unit-information dialog; F2 without Shift opens
the in-game options when panel bit 0 is clear; Tab in type 3 opens the tab menu when the
orders-panel bit 2 is clear and otherwise behaves as F2; F4 toggles visual flag bit 7 (the score
panel latch); F12 resets the chat indices; `!`, `#`, `*`, backquote and `~` toggle visual flag
bit 0 (damage bars) and save the settings; `,` and `.` step the build-menu page with argument one;
Ctrl+F10 starts or stops movie capture; F11 toggles the debug state; backslash replays the retained
console buffer. The last three are gated by a UI option (the register: the `Games` registry value).

- **Enter: MATCHES in effect.** RWE opens its own one-line bar (`openChatBar`, `GameScene_chat.cpp`
  about 34) in place of TALK.GUI, refuses it to a playback; `BT-INGAME-DIALOGS-PARITY.md` has the
  talk dialog's rules. No `SmallButton` click on opening.
- **`h`, F1, Tab's menu: MISSING.** No share dialog, no unit-information dialog (F1 is RWE's help
  overlay), and Tab is the options menu in every game type. `TOTALA-EXE-KEYBOARD.md` section 80
  lists them; nothing has changed since.
- **F2: DIFFERS slightly.** F2 and Tab *toggle* the menu in RWE (`GameScene_input.cpp` about
  68-72); in the original F2 only opens it and Escape closes it, because the open panel is a
  modal layer. Presentation.
- **F4: MATCHES the register's section 76 and 108, see `BT-INGAME-DIALOGS-PARITY.md` rule 6** for
  what the latch toggles.
- **F12: MATCHES in effect** (clears the console, where chat lands, about 224).
- **Damage bars: DIFFERS in extent.** The original has five aliases; RWE binds only the backquote
  scancode (about 175), so a layout where `~` and backquote are different physical keys is
  unaffected, but `!`, `#` and `*` do nothing. The register already calls this harmless. RWE does
  not save the setting when toggled (`healthBarsVisible` starts off every game and has no
  preference); the original calls the settings save.
  Presentation.
- **`,` and `.`: DIFFERS from the register's resolved row BT-104** (the keys should toggle: step
  past the last page closes the menu, and stepping from closed opens page one or the last page;
  RWE always wraps, about 179-199). Already open as BT-104, not re-raised. The keys also **play the
  next-menu sound with no focus unit** in the original; RWE does nothing without a single selected
  unit.
- **Developer keys: not applicable.** Ctrl+F10, F11, backslash and the debug hotkey tail (six
  events: a bit toggle, a mode counter wrapping at five, two page-flip switches, a hovered-unit
  poke and a fill-both-stores) have RWE's F10 and F11 ImGui windows in their place.
  `KEYBOARD` section 81 already says so.

---

## 10. Gaps to fix

Ranked by what a player would notice. None of these touches the simulation except rule 6, whose
fix changes which command stream a keypress produces (the command kind itself is unchanged).

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **The `0x19` payload is read as 256ths of normal speed**; the chapter's layout (subtype, then value) makes the game-start record a pause-on and a speed change level N a different number. A real TA joining an own-clock host could pause RWE at start-up or send it to level 20. | CONFLICT, register inference | one packet capture of a real TA changing speed, or a spec-room read of the speed and pause senders |
| 2 | **What Ctrl+A selects**: register and help text say all units; chapter says idle units; RWE selects mobile units only (buildings and factories excluded). | CONFLICT, three readings | play-test or a spec-room read of the select service |
| 3 | **Ctrl+Shift+letter and the other Ctrl letters**: RWE needs Shift up for every Ctrl selection and handles only six category letters, so the additive form is missing and a mod's `CTRL_E` does nothing. | MISSING (mod data and Shift) | code: drop the `!isShiftDown()` guard for categories and form the token from any letter |
| 4 | **A mixed self-destruct selection arms the units that had no timer**; the chapter cancels the timers found and arms none. | DIFFERS (design against the chapter) | decision, then a one-line change in the key (count first, then send) |
| 5 | **Control groups are not squads**: no Alt, no preference, no exclusivity, no factory inheritance, the Shift meaning is reversed, and neither the `CreateSquad` nor the `SelectSquad` sound plays. | DIVERGENCE (roadmap only), plus a roadmap sentence the register contradicts | decision; at least add the two sounds, which are in the data |
| 6 | **Who may change the game speed**: RWE host only; the chapter has any non-watcher broadcasting. | DIFFERS (design) | two-machine play-test |
| 7 | **F3 selects nothing and walks no list; `n` visits on-screen units too.** | DIFFERS | code |
| 8 | **No message on a speed change; no `SmallButton` click on Enter.** | MISSING (presentation) | code |
| 9 | **`h`, F1, the Tab menu.** | MISSING | `BT-INGAME-DIALOGS-PARITY.md` |
| 10 | **Pause is honoured under the options panel and the chat bar**; the original's pause is an ordinary battle key. | DIFFERS (low) | decision |

No source file was changed.

## 2026-10-22 clarification for issue #654

[Ctrl category selection](BT-CATEGORY-CAMERA-KEY-FINDINGS.md#ctrl-categories-modifiers-names-and-selection-pool) confirms additive Shift, Ctrl precedence with Alt, and category selection beyond the six retail letters. It also limits the earlier arbitrary-letter claim: A/D/S/Z have dedicated commands and C has commander-follow semantics. Replacement changes only eligible local units. This was a static comparison, with no original-game execution.
