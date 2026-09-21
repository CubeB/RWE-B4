# What the original executable does: the interface: panels, menus and the end of a game

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §19, §60, §61, §62, §63, §64, §65, §66, §67, §68, §69, §70, §71, §99, §103, §104, §107, §108.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

19. [The order panel, and which flag gates which button](#19-the-order-panel-and-which-flag-gates-which-button)
60. [Which keys open what](#60-which-keys-open-what)
61. [TABMENU.GUI -- the multiplayer drop-down bar](#61-tabmenugui----the-multiplayer-drop-down-bar)
62. [The GAME OPTIONS menu -- ARMOPT.GUI / COROPT.GUI](#62-the-game-options-menu----armoptgui--coroptgui)
63. [The exit flow -- EXITMENU.GUI, YESORNO.GUI, RESTART.GUI](#63-the-exit-flow----exitmenugui-yesornogui-restartgui)
64. [The Save/Load dialogs](#64-the-saveload-dialogs)
65. [The in-game options screens -- PREFS.GUI + the RT pages](#65-the-in-game-options-screens----prefsgui--the-rt-pages)
66. [The screen-squish transition (in-game only)](#66-the-screen-squish-transition-in-game-only)
67. [Settings lifecycle -- live apply, Undo, Restore, OK, Cancel](#67-settings-lifecycle----live-apply-undo-restore-ok-cancel)
68. [MUSICRT -- the CD music panel](#68-musicrt----the-cd-music-panel)
69. [GAMMA.GUI and other leftovers](#69-gammagui-and-other-leftovers)
70. [Pause semantics](#70-pause-semantics)
71. [Implementation spec for RWE](#71-implementation-spec-for-rwe)
99. [The unit info panel, and the three things the gadget renderer does with a colour](#99-the-unit-info-panel-and-the-three-things-the-gadget-renderer-does-with-a-colour)
103. [Loading is issued to the transport, and what a click on a unit does](#103-loading-is-issued-to-the-transport-and-what-a-click-on-a-unit-does)
104. [The end of a game: a banner, a fade, and a chart that runs its bars up](#104-the-end-of-a-game-a-banner-a-fade-and-a-chart-that-runs-its-bars-up)
107. [Download menus: how a patch adds a button to a builder it does not ship](#107-download-menus-how-a-patch-adds-a-button-to-a-builder-it-does-not-ship)
108. [What Space shows: a strip from the bottom and the players at the top right](#108-what-space-shows-a-strip-from-the-bottom-and-the-players-at-the-top-right)

## 19. The order panel, and which flag gates which button

Every side loads one panel — `ARMGEN.GUI` or `CORGEN.GUI`, via `0x41B0F0` —
carrying every order button there is. The game then takes away or greys out
what the current selection cannot use. There is no per-unit order panel.

### `def+0x245` in full

§9 gave bits 0–8, 10 and 11 of this dword and called bit 4 `canattack`. Here is
the whole of it. The parser's boolean helper leaves its result in `eax`, so the
key pushed immediately before the `call 0x4C46C0` owns the `shl` immediately
after it, and the pipeline trap of §82 does not apply.

| Bit | Key | `shl` at | Second site |
|---|---|---|---|
| 0 | `mobilestandorders` | `0x42C8DB` insert | `0x48D104`, under a name compare against `Standing_MoveOrder` |
| 1 | `firestandorders` | `0x42C8FF` | `0x48D0D7`, likewise for `Standing_FireOrder` |
| 2 | `onoffable` | `0x42C8BE` | `0x403010` ACTIVATE / `0x403040` DEACTIVATE |
| 3 | `canstop` | `0x42C92B` | **none** — the order panel is its only reader |
| 4 | `canattack` | `0x42C94A` | `0x43F154`, `0x401F98` |
| 5 | `canguard` | `0x42C970` | `0x43E615`, `0x43F4C7` |
| 6 | `canpatrol` | `0x42C996` | `0x43E5DE` picks cursor 7 over cursor 19 |
| 7 | `canmove` | `0x42C9C3` | `0x43FE03`, `0x44019D` |
| 8 | `canload` | `0x42C9E2` | `0x4067C4` the load handler, `0x489ABE` the transporter half of the predicate |
| **9** | **no key: a copy of bit 10** | `0x42CA3D`–`0x42CA4F` | `0x4899CC`, the can-repair predicate |
| 10 | `canreclamate` | `0x42CA0F` | `0x48996C` can-reclaim, `0x43FA13` the RECLAIMUNIT mission |
| 11 | `canresurrect` | `0x42CA2E` | `0x43FF46` → the `RESURRECT` mission |
| 12 | `cancapture` | `0x42CA72` | `0x4042CF` the CAPTURE handler; also used as a cheap "is this a commander" |
| **13** | **no key: `cloakcost > 0`** | `0x42CA93` | `0x403080` CLOAK_ON, `0x4676AE` in the cloaked-unit render |
| 14 | `candgun` | `0x42CABA` | `0x43F7EE` → the `ATTACKSPECIAL` mission |
| 15 | `norestrict` | `0x42CB4B` | `0x44C165`, the restriction table UI |
| 16 | `wacky` | `0x42ADC0` (first-pass loader only) | `0x46D33C` — read, meaning unrecovered |
| 17 | `showplayername` | `0x42CB77` | `0x46AF64` |
| 18 | `commander` | `0x42CB96` | only jointly with 17 at `0x46AF56` |
| 19 | `cantbetransported` | `0x42CBBF` | `0x489AA3`, the passenger half of the load predicate |
| 20–22 | `selfdestructcountdown` | `0x42CBFA`, default 5 at `0x42CC13` | `0x40202D` |

Two entries in that table are not keys at all and are the reason two buttons
have no FBI field behind them:

```
42ca3d  and ah,0xfd        ; clear bit 9
42ca40  shr edx,1
42ca42  and edx,0x200      ; bit 10 -> bit 9
42ca4d  or  edx,eax
```

**Bit 9 is `canreclamate` copied**, and `0x4899CC` — the routine that decides
whether a unit may repair another — is what reads it. So REPAIR and RECLAIM are
the same flag twice and always appear together. Likewise **bit 13 is
`CloakCost > 0`** (`0x42CA5A`–`0x42CA96`, comparing the float at `def+0x1DA`
against the zero at `0x4FD210`); there is no `Cloakable` key in the binary at
all, which is why honouring one gives the button to nothing.

The layout is confirmed away from the parser by the field-by-field definition
copy at `0x42BAC1`–`0x42BCAD`, which moves bits 0–19 as twenty separate one-bit
members and then `0x700000` as a single three-bit group, and stops there.

Shipped-data check, 189 FBIs: `canload` is exactly `armatlas`, `armtship`,
`cortship`, `corvalk`; `cancapture` and `candgun` are exactly the two
commanders; `canresurrect`, `wacky`, `selfdestructcountdown` and `cloakable`
are named by nothing at all.

### A mixed selection: ANY, not ALL

`0x41B2E0` walks the local player's units (stride `0x118`, from `player+0x67`
to `player+0x6B`), skipping any without `unit+0x110` bit 4 — selected. It keeps
two different kinds of accumulator, and the difference is the whole answer.

**The ten capability bits are a plain OR.** `0x41B49F`–`0x41B524` is ten
repetitions of `shr edi,N / test cl,1 / je skip / mov <slot>,esi` with
`esi = 1`, and **nothing ever clears a slot**:

| bit | flag | address |
|---|---|---|
| 7 | `canmove` | `0x41B4A1` |
| 3 | `canstop` | `0x41B4AE` |
| 4 | `canattack` | `0x41B4BC` |
| 5 | `canguard` | `0x41B4C9` |
| 6 | `canpatrol` | `0x41B4D7` |
| 8 | `canload` | `0x41B4E4` |
| 9 | repair | `0x41B4F2` |
| 12 | `cancapture` | `0x41B4FD` |
| 10 | `canreclamate` | `0x41B50B` |
| 14 | `candgun` | `0x41B518` |

So a button is offered when **any** selected unit names it. Boxing a solar
collector in with a squad of Peewees does not cost the Peewees their move
button, and one transport in the box puts LOAD up for the lot.

**The four stateful toggles are a sentinel accumulator**, which is the shape
§9 already described for the two mode buttons: fire orders start at 4, move
orders at 4, cloak and on/off at 3; a unit that does not name the flag is
skipped entirely (`0x41B403`, `0x41B42C`) so it cannot drag the shared state;
the first offerer's value is taken; a later disagreement collapses to 3
(`0x41B420`, `0x41B449`) or 2. The sentinel means "nobody offered it" and greys
the button out (`0x41A243`, `0x41A280`).

The cloak accumulator has a bug worth not copying: `0x41B485`–`0x41B497` does
not compare at all, so a **second** cloakable unit sets "mixed" even when the
two agree. On/off, three instructions away at `0x41B893`, does compare.

Results are packed into `world+0x37EC0` (move-order 0–2, cloak 3–4, on/off 5–6,
canmove 7, canstop 8, canattack 9, canguard 10, canpatrol 11, canload 12,
canreclamate 13, cancapture 14, repair 15), `world+0x37EBE` bits 12–14 (fire
order) and `world+0x37EC2` bit 0 (candgun).

### Hidden versus greyed, and the slot LOAD and BLAST share

`0x41A120` is the enable pass — seventeen hardcoded name lookups, each followed
by one bit test. Almost everything is **greyed**, via `0x4A1200(page, idx, 1)`
which sets bit 0 of `WORD [ctrl+0x13C]`: MOVE at `0x41A2E8`, STOP `0x41A310`,
ATTACK `0x41A338`, DEFEND `0x41A35F`, PATROL `0x41A387`, and RECLAIM, REPAIR
and CAPTURE the same way.

LOAD and BLAST are the exception, because **they are the same slot**: in
`ARMGEN.GUI` `ARMLOAD` and `ARMBLAST` are both at `xpos=64, ypos=317`, same
width and height. `0x41A409` resolves it on the ORed `canload` bit —

- nothing can load: LOAD is made inactive (`0x4A03F0(page, idx, 0)`, which
  writes `BYTE [ctrl+0x29]`), UNLOAD is greyed, BLAST is greyed unless
  `candgun`;
- something can load: **BLAST is made inactive** and LOAD and UNLOAD stand.

So selecting a commander together with an Atlas costs you the D-gun button.

One trap for anyone reimplementing this: `0x49FE60` finds a control by
`strstr`, not by name equality, so `"MOVE"` also matches `ARMMOVEORD` and
`"LOAD"` also matches `ARMUNLOAD`. It only works because the shipped GUI files
happen to list the short name first.

### What RWE was doing

`GameScene::createOrdersPanel` gated LOAD, UNLOAD, BLAST and CLOAK, and only
when **exactly one unit was selected**; a selection of two or more got the raw
`ARMGEN.GUI` with every button on it, which is the reported complaint. It also
gated LOAD on having transport capacity rather than on `canload` (the same four
units in the shipped data, but not the same rule), and gated nothing at all on
`canstop`, `canpatrol`, `canattack`, `canmove`, `canguard`, `canreclamate` or
`cancapture` — four of which RWE was not even reading out of the FBI.

It now builds the list of selected definitions and applies
`selectionOffersOrderButton`, which is the OR above.

> **Ported, 2026-09-02** ("Buttons can be greyed out, and the order panel
> greys instead of hiding", `3f1a6c12`). This list used to open with a bullet
> saying RWE had no disabled state for a `UiStagedButton` and removed a button
> the selection could not use instead of drawing it dim. It has one now:
> `UiStagedButton::setEnabled(false)` draws the button's greyed face and makes
> it ignore every event, `UiFactory` honours a gui file's `grayedout=1`, and
> `GameScene::applyOrderButtonGating` greys everything and hides only LOAD and
> BLAST, which is exactly the split above (`0x41A412`, `0x41A471`).
>
> **The greyed face is not the original's arithmetic, and does not need to
> be.** The original darkens the gadget's whole rectangle in place, running it
> through SHADE row 12 at level -20 — a measured 0.82x on luminance (§99).
> RWE draws the frame the artists put in the button's own GAF one past the
> pressed frame; every shipped button carries one, and the factory had been
> extracting it and throwing it away all along. So the two agree because the
> artwork was drawn to agree, not because the code does the same sum: what
> the original computes at run time, the artists had already painted. A mod
> whose GAF has no such frame gets no dimming at all here, where the original
> would still have darkened it — the one case where the two can be told
> apart.

### Deliberately not ported

- **The cloak accumulator's disagreement bug** at `0x41B485`.
- **`canresurrect`, `wacky` and `selfdestructcountdown`.** No shipped unit
  names any of them and RWE has no resurrect order.
- **`canstop`.** It is parsed and honoured for the button, but the original's
  own STOP mission builder at `0x43F82C` does not check it, so the flag gates
  the button and nothing else.

## 60. Which keys open what

The in-game keyboard dispatch is at `0x495e90`: the key code is fetched from
a ring buffer (`0x4c1ab0`), `key - 9` indexes a byte table at `0x496694`
(0xF0 entries) which selects one of 40 cases in a jump table at `0x4965f4`.

The key codes are TA's own. The VK-to-code translator (around `0x4c1fb9`
to `0x4c21cb`, feeding the ring buffer via `0x4c1b20`) shows the encoding:

| TA code | Key |
|---|---|
| `0x09` | Tab |
| `0x1b` | Escape |
| `0xAA`-`0xC3` | Ctrl+A .. Ctrl+Z -- corrected by the keyboard decode (sections 72-73): the translator formats CTRL_%c and polls VK_CONTROL; the earlier Shift reading was wrong |
| `0xC5`-`0xCD` | Shift+1 .. Shift+9 (`'1'+0x94`) |
| `0xE2`-`0xED` | F1 .. F12 (Shift+Fn = `0xCE`-`0xD9`) |
| `0xF8` | Pause |

The cases that matter here:

- **Tab (`0x09`), case at `0x496133`**: query the game type
  (`0x435100(session)`; 1 = campaign, 2 = skirmish, 3 = multiplayer --
  inference from how the three values gate briefing/save/gray logic).
  If multiplayer, and the tab bar isn't locked out (`[game+0x37ebe]&4`
  clear), call `0x495010` -- **toggle the TABMENU bar**. If *not*
  multiplayer, fall straight through to the F2 case below -- in single
  player **Tab opens the GAME OPTIONS menu directly**; there is no tab bar.
- **F2 (`0xE3`), case at `0x496165`**: if Shift is not held and no menu is
  already open (`[game+0x37ebe]&1` clear), call `0x460cc0` -- open the GAME
  OPTIONS menu -- and set `[game+0x37ebe] |= 1` ("menu open"). No game-type
  check: F2 works in multiplayer too. (With Shift held the case instead
  records something to `+0x391b9` -- unrelated, not decoded further.)
- **Escape (`0x1b`), case at `0x495ed4`**: if `[game+0x37ebe]&1` (a menu is
  open) -- clear the bit and close the stored options panel (name kept in a
  buffer at `game+0x37ea0`, destroy via `0x4a9660`). Otherwise ESC is the
  order-reset/deselect key: first press resets the current order mode by
  pressing the `STOP` gadget of the main panel (string `"STOP"` at
  `0x502714`, gadget lookup `0x49fe60` + `0x4a6a40`), a further press
  deselects all units (`0x48bd00`) and closes floating panels
  (`0x491d70(1)`).
- **Pause (`0xF8`), case at `0x496099`**: toggle `[game+0x38a51] & 1` -- the
  pause flag -- and broadcast the new state to the other players (a message
  built with leading byte `0x19`, formatted through `0x44fdb0(3, ...)` and
  sent via `0x451df0`). No game-type check; the pause key is the same in
  single and multiplayer.
- **`+`/`=` and `-`/`_`** (`0x496570` / `0x496512`): game speed up/down by
  1, clamped to 1..20 (`0x38a4b` is the desired speed), refused for
  watchers; applied via `0x490df0(speed, 1)`.

**Does opening the menu pause?** Yes, in single player. `0x460cc0` (GAME
OPTIONS open) ends with: if game type != 3, `[game+0x38a51] |= 1`
(`0x460e00`). The menu's close path (`0x460a33`) clears the bit again --
only when in-game and not multiplayer. Opening the Save (`0x49306d`) and
Load (`0x493330`) dialogs sets the same bit, and the options screens keep
it set (`0x45d0a1`). In multiplayer nothing menu-related pauses; only the
Pause key does, and it is broadcast.

---

## 61. TABMENU.GUI -- the multiplayer drop-down bar

File: `D:\RWE-extract\totala1\guis\TABMENU.GUI`.

Panel `HEADER`: 510x33 at (130, -33) -- it lives above the top edge and
slides down over the map (the negative `ypos` idiom; ALLIES/SHARE/CONTROL
use the same trick with y = -372/-429/-402). `crdefault=CANCEL`,
`escdefault=CANCEL`, `defaultfocus=OPTIONS`. With the 128-wide unit panel
on the left, 130+510 spans exactly to the right edge of a 640-wide screen.

| Gadget | Pos (in panel) | Size | Label | Quickkey |
|---|---|---|---|---|
| `OPTIONS` | (8,6) | 120x21 | "Options Menu" | O |
| `ALLIES` | (132,6) | 120x20 | "Allies" | A |
| `CANCEL` | (191,6) | 114x20 | (blank, `active=0`) | Backspace |
| `SHARE` | (255,6) | 120x20 | "Share" | S |
| `CONTROL` | (379,6) | 120x20 | "Control" | (none) |

No GAF carries TABMENU art (no `TABMENU.GAF`, no entry in
`commongui.GAF`), so the bar renders with the engine's default panel/button
drawing -- text buttons on the standard background, exactly what RWE's
UiFactory already produces for a GUI with no bitmap.

**Toggle, `0x495010`**: plays UI sound `"BigButton"` (ALLSOUND.TDF ->
`butmain1`). State lives in `[game+0x2bee]` bits `0xe0`: if any are set the
bar is up -- clear them and destroy the panel; otherwise set bit `0x20`,
load `TABMENU.GUI` (via `0x4aa8f0`, flags `0x800`), install the click
callback `0x494740`, then set button availability:

- Count the *other* live, non-watching players (10 player slots, stride
  `0x14b`, base `game+0x1b63`; a player is skipped when dead or when
  `[player+0x9b] & 0x40` -- the watcher bit).
- If game type != 3, or the local player is a watcher: ALLIES, SHARE and
  CONTROL are all disabled (`0x4a0570(gui, name, 0)`).
- Else ALLIES and SHARE are enabled iff at least one other such player
  exists; CONTROL additionally requires `[game+0x2c74]&1` clear and
  `0x457a50() != 0` (not decoded further).

`OPTIONS` is never disabled. Finally `0x4a81e0(gui, 0x40)` -- the slide-in
animation -- runs.

**Click dispatch, `0x494740`** (each button plays sound `"BigButton"`):

| Gadget | Action |
|---|---|
| panel-close event (`gadget+0x60 == -1`, i.e. CANCEL/ESC) | clear `[0x2bee]&0xe0`, destroy the bar |
| `OPTIONS` | `[game+0x37ebe] \|= 1`; `0x460cc0` -- open GAME OPTIONS |
| `SHARE` | `0x4936f0` -- SHARE.GUI (Transfer Resources: metal/energy sliders with player list, "Chosen Units", "Map Info") |
| `CONTROL` | `0x4466b0` -- CONTROL.GUI (Reject Player list, "No Watching\|Watching OK") |
| `ALLIES` | `0x4478b0` -- ALLIES.GUI (per-player ally checkboxes, "Allied Victory Off\|On") |

The three child dialogs all slide down from above the screen (negative
`ypos` in their GUI files). Also of note: bare key `h` (case `0x4964ae`)
opens the Share dialog directly in multiplayer.

---

## 62. The GAME OPTIONS menu -- ARMOPT.GUI / COROPT.GUI

Opened by F2 anywhere, Tab in single player, or TABMENU OPTIONS in
multiplayer. `0x460cc0` loads the literal name `"ARMOPT.GUI"` through the
GUI loader `0x4aa8f0`, which substitutes the local player's side prefix
(the same mechanism as `"%sMAIN2.GUI"`); art comes from `anims/ARMOPT.GAF`
(one entry, `OPTBG`, 128x354).

Panel: 128x352 at **(0, 128)** -- it sits exactly over the left-hand unit
panel, with the (paused, still rendered) game visible everywhere else.
There is no snapshot and no dimming at this stage. `crdefault`,
`escdefault` and `defaultfocus` are all `OK`.

| Gadget | Pos | Size | Label | Quickkey |
|---|---|---|---|---|
| `LOADGAME` | (13,22) | 96x31 | "Load Game" | L |
| `SAVEGAME` | (13,64) | 96x31 | "Save Game" | S |
| `PREFS` | (13,105) | 96x31 | "Options" | O |
| `MISSION` | (13,147) | 96x31 | "Briefing" | B |
| `HELP` | (13,189) | 96x31 | "Help" | -- |
| `EXIT` | (13,231) | 96x31 | "Exit" | E |
| `OK` | (14,291) | 96x31 | "Resume" (assoc=78, closes) | R |

There is also a `TEXT` label gadget reading "GAME OPTIONS".

On open (`0x460cc0`): SAVEGAME and LOADGAME are **grayed out in
multiplayer** (`0x49fdf0`/`0x4a1200` with `gametype==3`); in skirmish they
stay enabled (skirmish games can be saved). If game type is 2 or 3 the
MISSION button's text is replaced with **"Settings"** (string `0x506da4`).
Then the slide-in `0x4a81e0(gui, 0x40)` runs and, if single-player, the
pause flag is set.

**Click dispatch, `0x4609b0`** (buttons play UI sound `"Options"` ->
`butoptn`):

| Gadget | Action |
|---|---|
| close event (OK/Resume, CR, ESC) | destroy panel, free any options-screen surfaces, clear pause (if in-game, not MP), clear `[0x37ebe]&1` |
| `LOADGAME` | `0x4931d0` -- Load dialog (LOADGAME.GUI; pauses) |
| `SAVEGAME` | `0x493060` -- Save dialog (SAVEGAME.GUI; pauses) |
| `PREFS` | `0x460160` -- the in-game options screens (section 06) |
| `HELP` | load HELP.GUI (flags `0x1881`), draw bitmap `"dhelp"`, callback `0x45fac0` |
| `MISSION` | campaign: load BRIEFING.GUI, hide its `MOREBAR`/`TextRegion` gadgets, draw `"igmbrief"`, callback `0x45f770`. Skirmish/MP ("Settings"): `0x45f1d0` -- the settings summary screen ("Max Units:", "Starting Energy:", "Line of Sight:", ... strings at file `0x1050cc`+) |
| `EXIT` | `0x4608b0` -- the exit menu (section 04) |

**There is no Restart and no Pause button on this menu** -- Restart lives
inside the Exit menu, and pause is implicit (single player) or a key
(multiplayer).

`OPTION.GUI` (a 426x410 centred "GAME OPTIONS" with "End Mission",
"Preferences", "Mission Objective") is referenced nowhere in the exe -- a
leftover earlier design. The shipped menu is the side-panel ARMOPT.

---

## 63. The exit flow -- EXITMENU.GUI, YESORNO.GUI, RESTART.GUI

`0x4608b0` loads `EXITMENU.GUI` (150x155 at (279,117), a small centred
menu), callback `0x460800`:

| Gadget | Label | Action |
|---|---|---|
| `MAINMENU` | "Exit to Menu" | confirm mode 0 |
| `EXITGAME` | "Exit Game" | confirm mode 2 |
| `RESTART` | (text set at runtime) | restart dialog |
| `CANCEL` | "Cancel" | close |

On open: in campaign/skirmish the RESTART button is enabled and given the
text **"Restart"** (`0x4c5740(0x506d5c)`); in multiplayer it stays blank
and dead. In multiplayer, when `[game+0x2bee]&0x10` is set (the
battle-already-decided flag -- inference), "Exit to Menu" is disabled.

Confirmations go through `YESORNO.GUI` (`0x460680`, callback `0x4605c0`):
its `TITLE` gadget gets one of

- mode 0 ("Exit to Menu"): `"Surrender this battle and return to main menu?"`
- mode 2 ("Exit Game"): `"Surrender this battle and exit to Windows?"`, or
  just `"Exit the Battle"` when `[0x2bee]&0x10` (nothing left to
  surrender).

Yes (CHOICE1): mode 0/1 -> tear down to the front-end menu
(`0x491b60`/`0x490b30(1)`); mode 2 -> set `[game+0x3923b]|=4` and quit to
Windows (`0x491c60`). The buttons are named `CHOICE1`/`CHOICE2` and the
handler installs them as the panel's cr/esc defaults.

RESTART (`0x4604a0`) loads `RESTART.GUI` (252x221 at (287,106)): bitmap
`"drestart"`, the mission name is written into `MISSIONNAME`(1), and the
dialog offers "Adjust Difficulty" (Easy|Medium|Hard), Cancel, Restart;
callback `0x460340`.

---

## 64. The Save/Load dialogs

Only touched for completeness: `0x493060` (save) and `0x4931d0` (load) set
the pause bit on open and use `SAVEGAME.GUI`/`LOADGAME.GUI`. Not decoded
further here.

---

## 65. The in-game options screens -- PREFS.GUI + the RT pages

This is the front end's STARTOPT composite, re-skinned for in-game use.
The shared plumbing:

**The frame chooser, `0x45cfc0`.** One function serves both worlds: if
`[game+0x2a44]&4` (the "we are in a game" flag) it loads **`PREFS.GUI`**
and sets `[game+0x37ebe]|=1`; otherwise it loads **`STARTOPT.GUI`**. So
`PREFS.GUI` *is* the in-game STARTOPT. It also grays the frame's MUSIC tab
when no CD device exists (`[game+0x10]->[0] == 0`), and at its end
(`0x45d09c`) re-asserts the pause flag when in-game and not multiplayer.

**PREFS.GUI** (frame): 128x354 sidebar at **(0, 126)** -- the same place
ARMOPT occupied. Background gadget `IGOPT` -> `commongui.GAF` entry `IGOPT`
(128x354, one frame). `crdefault=escdefault=defaultfocus=PREV`.

| Gadget | Pos | Label | Quickkey |
|---|---|---|---|
| `SOUND` | (13,24) | "SOUND" | S |
| `MUSIC` | (13,66) | "MUSIC" | M |
| `SPEEDS` | (13,108) | "INTERFACE" | I |
| `VISUALS` | (13,150) | "VISUALS" | V |
| `PREV` | (13,251) | "OK" | O |
| `CANCEL` | (13,293) | "Cancel" | C |

(The tab gadgets have `assoc=30` -- radio-style; the exe highlights the
active tab with `0x4a1110(gui, name, 1)`.)

The `Igoptsoux/Igoptmusx/Igoptvisx/Igoptintx.pcx` files in `bitmaps/` are
**not referenced by the exe** (no such strings exist in the binary) and the
shipped GAF has only the single IGOPT frame -- they are source art for
per-tab sidebar variants that never shipped. Same for `PREFS.GAF`'s
`PREFSBG` entry.

**Opening a page** (SOUND `0x45de30`, MUSIC `0x45d7c0`, VISUALS
`0x45e5e0(0)`, SPEEDS `0x45ed50`; dispatched from the frame callback
`0x45fc60` on tab clicks): each one

1. calls `0x45cfc0` (frame reload -- PREFS or STARTOPT),
2. destroys the previous page panel (`0x45ce80`),
3. if `[0x37ebe]&1` (in-game) loads the **RT** GUI -- `SOUNDSRT.GUI`,
   `MUSICRT.GUI`, `VISUALRT.GUI`, `SPEEDSRT.GUI` (flags `0x200`); else
   loads the front-end GUI (`SOUNDS`/`MUSIC`/`VISUALS`/`SPEEDS`) *and*
   draws that page's full-screen backdrop bitmap (`optsound4x`,
   `optmusic4x`, `optvisual4x`, `optinterface4x` via `0x4288d0`),
4. installs the page's click callback (sound `0x45da90`, music `0x45d280`,
   visuals `0x45e100`, speeds `0x45ead0`),
5. wires the sliders: gadget`+0x13c` = range, `+0x140` = position,
   `+0x144` = a live callback that fires as the knob moves.

So in-game the composite is: **PREFS sidebar (0,126) + RT page panel at
(128,128), 150x352** -- the page background is the matching 149x354 frame
from `commongui.GAF` (`SOUNDSRT`, `MUSICRT`, `VISUALSRT`, `SPEEDSRT`,
resolved by the background gadget's name as usual; note the visuals GUI's
background gadget is named `VISUALSRT`). Together they fill x = 0..278;
the rest of the screen shows the squish animation of section 07 (ending
black).

**RT pages vs front-end pages** -- the gadget sets are the same except:

- Every RT page adds `RESTORE` ("Restore Defaults") and `UNDO`
  ("Undo Changes") at y=269/304.
- `VISUALRT` drops the screen-resolution control (`VIDSLDR`/`VIDVAL`
  "640x480"/`VIDTEXT`) -- the exe only wires video-mode enumeration and
  SELVMODE.GUI in the front-end branch (`0x45e6a4` runs only when
  `[0x37ebe]&1` is clear). **You cannot change resolution in-game.**
- Minor label drift ("Unit Chat" vs "Unit Text Acks", "Text Delay (secs)"
  vs "Screen Text Delay", TEST "TEST" vs "Sound Test").

Page contents (RT versions):

- **SOUNDSRT**: `MODE` "Off|Mono|3D" (Sound Mode), `FXVOL` slider (range
  64, live callback `0x45bde0`), `SPEECH` "Off|Medium|Full" (Unit Sounds),
  `TEST` -- plays `sounds\explode.wav` (string at file `0x104fd8`).
- **VISUALRT**: `GAMMA` slider, `SHADING` Off|On, `ANTI` Off|On
  (antialias), `BSHADOWS` Off|On (building shadows).
- **SPEEDSRT**: `GAME` slider -- game speed, range 21, live callback
  `0x45c070`, value `[game+0x38a4b]`, shown as "%d Slow/Normal/Fast/...";
  `SCREEN` slider -- scroll speed, range 65, value `[game+0x1434d]`;
  `TXTSCROL` -- text delay "%d secs"; `MAXLINES`; `UNITCHAT`
  "Off|Medium|Full"; `LEFTCLICK` "Left Click|Right Click" (interface
  style). The GAME slider is wired in-game too (speed changes go through
  the same `0x490df0` path as the +/- keys).
- **MUSICRT**: section 09.

---

## 66. The screen-squish transition (in-game only)

`0x460160` (ARMOPT PREFS-button handler), before opening the frame:

1. allocates `"FLIPSURFACE"` at the full screen size and copies the
   current framebuffer into it -- a snapshot of the game as it looked,
2. allocates a cleared 480x300 `"BKUPSURFACE"`,
3. sets `0x512fe4 = 1`, which arms a per-frame routine `0x45ffb0`,
4. snapshots every option value (section 08),
5. slides the frame in (`0x4a81e0(gui, 0xc0)`).

`0x45ffb0` runs each frame while the options are open: a counter
(`0x512fec`) advances 0x15/frame to 0x115 and the FLIPSURFACE snapshot is
redrawn progressively squeezed (a growing crop `0x512ff0 += 6`/frame,
rectangles built around x=127 -- the sidebar edge -- and the screen bottom
`0x1df`), with the 351x21 `LIGHTBAR` sprite from `commongui.GAF` drawn
across it during the collapse. The net effect is the familiar CRT-style
"game screen collapses behind the panel" wipe; the end state is a black
field behind the sidebar + page. (The rectangle interpretation is partly
inference; the snapshot/LIGHTBAR/collapse mechanics are decoded fact.) The
surfaces are freed when the options close (`0x45ff50`).

In the front end the same handler draws the `options4x`/`opt*4x` bitmaps
instead; no game snapshot is involved.

---

## 67. Settings lifecycle -- live apply, Undo, Restore, OK, Cancel

On options open, `0x460160` copies the whole settings block
(`game+0x37ee6`, 0x52 bytes -> `0x512f18`) plus the current CD track
(`0x4ce5a0` -> `0x512fd9`) and all 100 per-track CD types (`0x4ce7e0(i)`
-> `0x512f75[100]`).

- **Every control applies immediately** -- sliders through their `+0x144`
  live callbacks (volume audibly changes while dragging), stage buttons in
  their click handlers (`0x4cdb40` music enable, `0x4ce7a0` track mode,
  etc.). Nothing waits for OK.
- **UNDO (per RT page)** restores the entry snapshot for that page's
  values and re-applies them to the running systems (music page:
  `0x45d477`; the frame CANCEL uses the same restore code).
- **RESTORE ("Restore Defaults")** applies hard-coded defaults (music page
  `0x45d53c`: speech volume 0x20, track mode 4 = Custom, music on).
- **OK (the `PREV` gadget)**: `0x430f00` -- writes every setting to the
  registry under key `"Total Annihilation"` (a long series of
  `0x4b6a50(key, valueName, value)` calls) -- then the panel closes via
  its default-close path. Plays sound `"Options"`.
- **CANCEL** (frame): plays `"Previous"`, restores *all* pages' values
  from the snapshot -- volumes, gamma (value and ramp), shading toggle (it
  re-toggles the renderer if the bit changed), track mode, track types,
  game speed (`0x38a4b/0x38a4d`), scroll speed, text delay, chat levels --
  then closes without touching the registry (`0x45fd73`-`0x45ff28`).

Closing the options in-game returns to the game directly (the GAME
OPTIONS sidebar is not restacked), and the close path clears the pause
flag.

---

## 68. MUSICRT -- the CD music panel

> **Ported in part, 2026-09-11** (`3da7c98a`). TRACKMODE works: Play All
> walks the album in order and wraps, Random takes any track, Repeat plays
> the current track again, and Custom, the default, is the situational music;
> the evaluator keeps counting in every mode but only Custom acts on it.
> CDNEXT and CDPREV step Play All and Repeat through the album. The mode is
> saved to `rwe.cfg` as `music-mode`. TRACKTYPE and TRACKNUM are not ported
> (fork issue #20).

Layout (panel 150x352 at (128,128), background GAF entry `MUSICRT`):
`NOTRAK` "Off|On" (CD music on/off), `MUSICVOL` slider, `TRACKMODE`
"Play All|Random|Repeat|Custom", CD transport `CDPREV CDSTOP CDPLAY
CDNEXT` (16x16 buttons at y=151), `TRACKNUM` (y=178), `TRACKTYPE`
"Building|Battle|Victory|Defeat|Unused" (y=202), RESTORE, UNDO.

State: `[game+0x37f14]&1` = music enabled; `[game+0x37f16]` = track mode
1-4; `0x512fe0` = the panel's current track number.

Decoded behaviour (populate/sync `0x45d130`, dispatch `0x45d280`, display
refresh `0x45c3f0`, per-frame track watcher `0x45d0c0`):

- **`TRACKNUM` displays the current CD track number** -- `sprintf("%d")`,
  or the string **"NO DISC"** when the track is 0. It is display-only (no
  click case in the dispatch) and grays out when music is off. A per-frame
  watcher (`0x45d0c0`) keeps it in sync while the CD advances on its own.
- **`TRACKTYPE` shows the current track's type** (stage =
  `getTrackType(curTrack)`, `0x4ce7e0`) and **clicking it cycles the stage
  and assigns that type to the current track** (`0x4ce7c0(curTrack,
  stage)`, at `0x45d753`) -- confirmed: it retypes the track as
  Building/Battle/Victory/Defeat/Unused. The types live in a 100-entry
  table (snapshotted for Undo). TRACKTYPE is only enabled when music is on
  **and** the mode is Custom (`0x45d234`); in any other mode it sits
  grayed, showing the type read-only.
- **`TRACKMODE` cycles Play All -> Random -> Repeat -> Custom**
  (`[0x37f16]` = 1..4, applied via `0x4ce7a0`). Choosing Repeat pins the
  current track (`0x4ce580` re-asserted on every refresh); choosing Custom
  un-grays TRACKTYPE and immediately re-applies the current track's type.
  Custom is the mode in which track types matter (the game picks tracks by
  their assigned type), and it is the default -- Restore Defaults sets 4.
- **Transport**: CDPLAY plays the panel's track (`0x4ceb60(track,1)`);
  CDSTOP stops (`0x4ced40`) and resets the display to track 1; CDNEXT and
  CDPREV step with wrap-around at the disc's track count (`0x4ce450`).
  All four, plus TRACKMODE and MUSICVOL, gray out when NOTRAK is Off.
- `NOTRAK` toggles the enable bit and calls `0x4cedc0(on)`.

The front-end `MUSIC.GUI` is identical minus RESTORE/UNDO (plus a "CD
Music" label); both are driven by the same handlers -- the only fork is
which GUI file loads.

---

## 69. GAMMA.GUI and other leftovers

`GAMMA.GUI` (333x128 centred, "Gamma Correction" with one slider,
"Set"/"Previous Menu") is referenced **nowhere** in the exe -- no
`"GAMMA.GUI"` string exists in the binary. The live gamma control is the
`GAMMA` slider gadget on VISUALS/VISUALRT. GAMMA.GUI hangs off nothing; a
leftover. Same status: `OPTION.GUI` (section 03), the `Igopt*.pcx`
bitmaps, `IGOPT0X/1X.PCX`, `igoptionsTEMP.PCX` (section 06).

---

## 70. Pause semantics

The pause flag is `[game+0x38a51] & 1`. It is *only* a sim-tick gate: the
tick scheduler (`0x49527a`-`0x4953ee`) computes how many sim ticks to run
this frame and, when the flag is set, forces that count to zero
(`0x4953d9`) and returns. Everything else -- rendering, mouse scrolling,
GUI animation, chat -- runs normally; while paused the world renderer
draws the `igpaused` banner (117x29, from `IGTITLES.GAF`, alongside
`igvictory` and `igdefeat`) over the view (`0x46a107`).

Set by: the Pause key (toggle, broadcast in MP as message type `0x19`);
opening GAME OPTIONS / Save / Load / the options screens in a
single-player or skirmish game (never when game type is 3). Cleared by the
matching close paths. Bits 1 and 2 of the same word are unrelated (net-lag
and reduced-speed indicators, `0x49535c`).

Multiplayer note (not deeply decoded): the Pause key path has no
game-type branch -- a player can toggle the shared pause and the state is
broadcast; the menus deliberately never pause an MP game.

---

## 71. Implementation spec for RWE

Everything below maps onto `D:\RWE\src\rwe\game\GameScene.cpp` (which
already has `paused` and `guiVisible`), UiFactory (which already builds
GUI files and resolves gadget art from GAFs by name), and the
MainMenuScene options code (which already composites pages over
STARTOPT.GUI).

**Key bindings** (GameScene key handler):

- `Tab`: multiplayer -> toggle the TABMENU bar; single player -> open
  GAME OPTIONS.
- `F2`: open GAME OPTIONS (any game type).
- `Escape`: if a menu/options panel is open, close it (Cancel semantics
  for the options screens); else cancel the current order mode, then
  deselect all.
- `Pause`: toggle `paused`; in MP broadcast it (when RWE gets there).
- Menu open in single player/skirmish => `paused = true`; restore on
  close. Never auto-pause in multiplayer.

**GAME OPTIONS panel** -- build `<side>OPT.GUI` via UiFactory at (0,128),
over the live scene (no dimming, no snapshot; the game keeps rendering
behind it, frozen by `paused`). Buttons:

| Button | RWE action |
|---|---|
| Load Game / Save Game | gray until save/load exists; then the dialogs (both pause) |
| Options ("PREFS") | open the in-game options composite below |
| Briefing / Settings | campaign: BRIEFING.GUI with `igmbrief`; skirmish: a settings summary (can stay grayed initially) |
| Help | HELP.GUI + `dhelp` bitmap (optional) |
| Exit | EXITMENU.GUI flow below |
| Resume (OK; cr/esc default) | close panel, unpause |

Gray Save/Load in multiplayer; retitle MISSION to "Settings" for game
types 2/3.

**Exit flow** -- EXITMENU.GUI at (279,117): "Exit to Menu" -> YESORNO.GUI
("Surrender this battle and return to main menu?") -> GameScene exits to
MainMenuScene; "Exit Game" -> YESORNO ("...and exit to Windows?") ->
quit; "Restart" (campaign/skirmish only, text set at runtime) ->
RESTART.GUI -> reload the mission; Cancel closes. YESORNO's buttons are
CHOICE1 (yes) and CHOICE2 (no); set the TITLE text per mode.

**TABMENU bar** (multiplayer only): TABMENU.GUI at (130, -33 -> 0) with a
slide-down; OPTIONS always enabled, opening GAME OPTIONS; Allies/Share/
Control grayed until those dialogs exist (the original grays them by
player-count/watcher rules anyway). Toggled by Tab; ESC/Backspace closes.

**In-game options composite**: reuse the MainMenuScene composite logic but
with `PREFS.GUI` as the frame (sidebar at (0,126), GAF entry `IGOPT` as
background, tabs SOUND/MUSIC/INTERFACE/VISUALS + OK/Cancel) and the
`*RT.GUI` page at (128,128) -- `SOUNDSRT`, `MUSICRT`, `VISUALRT`,
`SPEEDSRT`, backgrounds resolved from `commongui.GAF` by the background
gadget's name (the visuals entry is `VISUALSRT`). Do **not** offer
resolution switching in-game. The transition can be a simple cut or fade
to black behind the panels; the original's CRT-squish (snapshot +
LIGHTBAR collapse) is cosmetic and optional.

**Settings lifecycle**: apply every change immediately (volume while the
slider drags, gamma, shading, game speed, scroll speed, ...); snapshot all
values on open; per-page "Undo Changes" and frame-level Cancel restore the
snapshot; "Restore Defaults" applies per-page hard defaults; OK persists
(RWE's config file standing in for the registry) and closes. FX volume,
music volume, scroll speed and game speed map directly onto settings RWE
already has; shading/shadows/antialias/gamma as available.

**Music page**: if RWE has no CD-audio equivalent, either load
MUSICRT.GUI faithfully with "NO DISC" in TRACKNUM and the transport
grayed (the original's no-disc presentation), or gray the MUSIC tab as
the original does when no CD device exists -- both are authentic. With a
music backend: TRACKNUM = current track number, TRACKTYPE = current
track's type (click retypes it; enabled only in Custom mode), TRACKMODE
cycles Play All/Random/Repeat/Custom, transport = play/stop/next/prev
with wrap.

**Pause**: keep `paused` a sim-gate only -- camera scrolling, GUI and
rendering continue; draw the `igpaused` sprite from `IGTITLES.GAF` over
the world view while paused.

---

## 99. The unit info panel, and the three things the gadget renderer does with a colour

Three related reads, all of them interface: the footer that describes the unit
under the cursor (`0x46A860`), the anti-missile ring's second state (an
addendum to §25), and the parts of the gadget renderer that change a colour
rather than draw something — the focus caret, the selected list row, and the
greying §19 recorded as unported.

### Which unit the footer describes

**The one under the cursor, and only that.** `0x46ABA9` reads a word from
`cfg+0x2CBA` and indexes the unit array with it; that word is written in
exactly two places (`0x491D23` and `0x499283`), both immediately after a call
to `0x48CD80`, which walks the unit list and returns what the mouse is over.
There is no fallback to the selection: move the pointer off a unit and the
footer empties, whatever is selected. When the word is zero the routine falls
through to `cfg+0x2CBC`, the hovered *feature*, and draws its description
instead (`0x46B78C`).

RWE already keyed the footer on the hovered unit, which this confirms.

### The redraw key

Before it draws anything, `0x46ABA3`-`0x46ACDD` fills a 60-byte block and
`repz cmpsb`s it against a cached copy at `cfg+0x37E60`; equal means nothing
has changed and the whole routine is skipped. The block is worth listing
because it is the complete set of things the footer can show:

| Offset | Field |
|---|---|
| `+0x00` | `0x439DF0(unit)` — the current mission's display name pointer |
| `+0x04` | the hovered unit's index |
| `+0x06` | `unit+0x108`, hit points |
| `+0x08` | `unit+0xB8`, kills |
| `+0x0A`, `+0x0E`, `+0x12` | per-weapon reload counter, or `-1` |
| `+0x16`, `+0x1A`, `+0x1E`, `+0x22` | `unit+0xD0`, `+0xCC`, `+0xE8`, `+0xE4` — the four resource rates |
| `+0x26`, `+0x28` | the current mission's target unit and its hit points |
| `+0x2A` | the hovered feature |
| `+0x2C` | the build button under the cursor |
| `+0x30`, `+0x34` | `cfg+0x37E90`, `cfg+0x37E94` |

The three weapon reload counters are collected (`0x46AC4B`, skipping any
weapon whose `reloadtime` is under thirty ticks) and then never drawn — the
same dead end as the RELOAD1/2/3 rectangles in §29. They are in the key, so a
reloading weapon forces a redraw of a panel that does not show it.

### What it draws, rectangle by rectangle

`SIDEDATA.TDF`'s footer rectangles, at the offsets the parser at `0x432310`
gives them (the renderer addresses the same struct 0x4A higher, which is where
§29's `DAMAGEBAR` at `side+0x152` comes from):

| Key | Parser offset | What goes there |
|---|---|---|
| `LOGO2` | `+0xE8` | the owner's side logo, `0x467C00` |
| `UNITNAME` | `+0xF8` | the unit's name, **centred on x1** |
| `DAMAGEBAR` | `+0x108` | health, `hp/maxhp`, green over dark red |
| `UNITENERGYMAKE` | `+0x118` | `"+%.0f"` |
| `UNITENERGYUSE` | `+0x128` | `"-%.0f"` |
| `UNITMETALMAKE` | `+0x138` | `"+%.1f"` |
| `UNITMETALUSE` | `+0x148` | `"-%.1f"` |
| `MISSIONTEXT` | `+0x158` | the mission's display name, centred |
| `UNITNAME2` | `+0x168` | what the current mission is pointed at, centred |
| `DAMAGEBAR2` | `+0x178` | that target's health, or the weapon percentage |
| `NAME` | `+0x188` | the hovered feature or build button, `"%s %s%s"` |
| `DESCRIPTION` | `+0x198` | the build button's description |
| `RELOAD1..3` | `+0x1A8`, `+0x1B8`, `+0x1C8` | parsed, never read |

Five details worth having:

- **Metal takes one decimal place and energy none.** Not a rounding
  convention anyone chose later: the four format strings are `"+%.1f"`
  (`0x50788C`) and `"-%.1f"` (`0x50787C`) for metal against `"+%.0f"`
  (`0x507884`) and `"-%.0f"` (`0x507874`) for energy.
- **Every rate is clamped at zero first.** `fcomp` against the zero at
  `0x4FD568` in front of each `sprintf`, so a negative figure prints as `0`
  rather than as a negative.
- **The name is the player's, not the unit's, for a commander in a network
  game.** `0x46AF56` ORs `showplayername` (flags bit 17) with `commander`
  (bit 18) and requires `0x435100` to return 3.
- **The damage bar is skipped for someone else's `hidedamage` unit**
  (`0x46B03F` tests bit 14 of `def+0x241`), and its colours are interface
  slots `0x0A` over `0x04` — bright green over dark red.
- **The four rates, the kills line and the mission line are one block behind
  one ownership test** (`0x46B119`). An enemy unit shows you its logo, its
  name and its health and nothing else. The second name-and-bar slot is
  *outside* that test.

### Kills, and Veteran

`0x46B2B8`, gated on bit 31 of `unit+0x110` and on the kill count at
`unit+0xB8` being non-zero:

```
"%d %s"        kills, "kill" if 1 else "kills"      when kills <= 4
"%d %s - %s"   kills, "kills", "Veteran"            when kills >= 5
```

The comparison is `cmp cx,4 / jbe` at `0x46B30D`. Five kills is the whole of
TA's veterancy display; there is no other reader of the count in the
interface.

It has **no rectangle of its own**. `0x46B2D6` takes `DAMAGEBAR`'s x1 and its
y2 plus two, so the line hangs under the health bar wherever `SIDEDATA.TDF`
put it. The colour is interface slot `0x0F`, which §50's GUIPAL nearest-match
resolves to white.

### The mission line is a table lookup

`0x439DF0` reads the mission id byte at `mission+0x04` and returns
`[table + id*25]`, the `char*` at the front of the mission record. So the
footer never composes a string: every mission in the two tables
(§`TOTALA-EXE-MISSIONS.md`, ground at `0x4FC490`, air at `0x4FCA18`) carries
its own wording. The full vocabulary, both tables:

`Stopping`, `Attacking`, `Activate`, `Deactivate`, `Cloaking`, `Decloaking`,
`Acknowledged`, `Nanolathing`, `SELF DESTRUCT ENGAGED`, `Paralyzed`,
`Under construction`, `Being transported`, `Unit is available`, `Waiting`,
`Waiting for attack`, `Ready`, `Repairing`, `Ready with orders`, `Standby`,
`Moving`, `Guarding`, `Suppressing fire`, `Annihilating`, `Parking`,
`Patrolling`, `Loading`, `Unloading`, `Teleporting`, `Repair patrol`,
`Capturing`, `Resurrecting`, `Reclaiming`, `Landing`, `Airstrike`,
`Engaging target`, `Evading`, `Seeking to attack`, `Seeking to guard`,
`Under repair`, `Seeking to land`.

Note `AttackSpecial` is `Annihilating`, not `Attacking`, and that the three
air-to-X missions share `Engaging target`.

### §29 was wrong about the stockpile bar, and here is where it is drawn

§29 concluded that the progress of the round a silo is building is shown
**nowhere**, having read `0x46AD90`-`0x46B400`. The routine does not end at
`0x46B400`. At `0x46B445`:

```
46b445  push esi
46b446  call 0x439d20              ; ticksPaid * 100 / (reloadtime*30)
46b44b  test eax,eax
46b451  je   0x46b571              ;   nothing on order -> the target-unit slot
46b471  jne  0x46b8ea              ;   not our unit -> nothing
46b477  push 0x5077b0              ;   "Weapon"
...     centred in UNITNAME2
46b4c3  add  ebp,0x1c2             ;   DAMAGEBAR2
46b4f6  cmp  eax,0x64              ;   clamped to 0..100
46b536  call 0x4bf6f0              ;   filled to that percentage
```

`0x439D20` is exactly the arithmetic §29 predicted — it walks the mission list
from `unit+0x60` for the one with the `BUILDWEAPON` flag (`mission+0x42` bit
19), takes the ticks paid at `mission+0x3E` and divides by the weapon's own
`wdef+0xE4` — and the answer is drawn as a percentage bar under the caption
`Weapon`. So the silo readout RWE added as "an addition, not a restoration" is
in fact what the original does, in the same two rectangles, and only the
caption was RWE's invention.

### The second name-and-bar slot is not build-only

When there is no weapon on order, `0x46B571` uses the word the redraw key
collected from `0x439DD0(unit)` — `mission+0x16`, the current mission's target
unit. Whatever the current mission points at gets its name centred in
`UNITNAME2` and its health drawn in `DAMAGEBAR2`, subject to the usual
visibility test (`0x465AC0`) and to `hidedamage`. A builder shows what it is
building and how far along it is; a guard shows what it is guarding; an
attacker shows what it is shooting at.

### Addendum to §25: the anti-missile ring has two states, and the magazine picks

§25 recorded that the coverage ring is dashed when `[weaponSlot+0x0E]` is
non-zero without saying what that byte is. It is the **magazine**: weapon
records are `0x1C` apart from `unit+0x04` with the definition pointer at
`+0x0C` and the round count at `+0x1A`, and `0x46707C` walks the slots by the
definition pointer, so `slot+0x0E` is `record+0x1A` — the same byte
`0x419A33` reads to caption the MAKENUKE button (§29). So:

- **magazine empty → a solid ring** (`0x4C0070`);
- **at least one round stocked → a dashed ring** (`0x4C01A0`), sixteen of the
  thirty-two segments, with the starting parity taken from `cfg+0x142F1` bit
  0.

That blink bit flips every eight passes of the main loop: `0x466580` counts a
word at `cfg+0x142EF` down from 7 and flips the bit when it underflows, and it
is called once a frame from the main loop at `0x4955E5`. So the gaps chase
round the ring about twice a second, and a loaded launcher is distinguishable
from an empty one at a glance.

The rings are also **clipped to the minimap**: `0x4C0070` clips each segment
before handing it to the Bresenham at `0x4CC7AB`, which is what stops a
2000-unit coverage ring painting over the rest of the screen.

### The gadget renderer's three colour tricks

The per-gadget draw dispatches on the type byte through a table at `0x4A962C`
(index `type-1`, so type 1 Button → `0x4A5F40`, type 2 ListBox → `0x4A1B40`,
type 3 TextBox → `0x4A4D70`). Three of the things it does are colour changes
rather than draws, and all three go through the same pair of 32x256
palette-index tables that the model shading uses: `0x4BF4D0(surface, rect,
level)` clamps `level` to `[-32, 31]`, picks row `level+32` of the **SHADE
TABLE** when it is negative and row `level` of the **LIGHT TABLE** when it is
not, and remaps every pixel in the rectangle through it. The two tables are
allocated at `0x4BA610` and `0x4BA660` under the literal tags `"SHADE TABLE"`
and `"LIGHT TABLE"`, and they are the shipped `palettes/PALETTE.SHD` and
`palettes/PALETTE.LHT`.

1. **A greyed control is darkened, not drawn dim.** `0x4A5A9E` tests the
   gadget's greyed flag (bit 0 of the byte at `gadget+0x148`, the flag
   `0x4A1200` sets and §19 found the order panel using) and runs the gadget's
   whole rectangle through level **-20** — SHADE row 12, a measured 0.82x on
   luminance. This is the mechanism §19 recorded as "RWE has no disabled state
   ... recorded here so it is not mistaken for the original's behaviour".
2. **The selected row of a list box is brightened.** `0x4A1FAE`, reached when
   the row index equals the box's selected index at `gadget+0xBA`, runs the
   row's rectangle through level **+30** — LIGHT row 30, a measured median of
   1.84x with the lift largest on dark pixels and none at all on white. It
   runs *after* the row's text, so the text is brightened with the
   background. (A row the list has separately marked — a byte array at
   `gadget+0xD6`, or a caption beginning with the literal `&G` — is instead
   darkened four times over, at levels -19, -20, -21 and -22.)
3. **The focused control shows a caret, and nothing else does.** The only
   comparison against the panel's focused-gadget index (`panel+0x64`) in the
   whole renderer is at `0x4A4F14`, in the text-box draw. When it matches,
   `0x4BE950` draws a one-pixel vertical line at the right-hand end of the
   text, from the text's top to two pixels past the font's height, in
   interface colour **9** — GUIPAL light blue, nearest-matching to palette 9,
   `(84, 84, 252)`. Buttons and list boxes draw no focus indicator at all.

Two smaller findings from the same pass, recorded because they are cheap to
port. A button's caption can carry a drop shadow: `0x4A59A4` tests bit 3 of
the gadget's attribs (`gadget+0x1B`, the dword before `colorf` at `+0x1F`)
and only when it is set draws the caption once at (x+1, y+3) in interface
colour 0 before drawing it in the gadget's own `colorf`
(`0x4A59A9`-`0x4A59E3`). The shipped menu buttons do not set it. And if the
gadget's `quickkey` character appears in its caption, `0x4A5B2C`-`0x4A5CFC`
find it with `strstr`, measure the caption up to it and the character itself
in the font's per-glyph widths, measure the font's `I` (`0x4A5CB7`, its
height plus two), and draw a line under the character with the line routine
`0x4BE950` in interface colour 2 (`[cfg+0x8B4]`), GUIPAL green: from the
character's left edge to its right edge less one, at y + height(`I`) + 1.

### What RWE does with all this

Done: the hovered unit is the subject, metal at one decimal and energy at
none, all four rates clamped at zero, the kills line under the damage bar with
`- Veteran` from the fifth kill, the mission line from a transcription of the
two tables, and the second name-and-bar slot showing the current order's
target unit with its health — with `Weapon` and the percentage taking priority
for a launcher, which is now a restoration rather than an addition. The
minimap coverage ring is drawn, dashed while the launcher has a round, and
both it and the four detection rings are clipped to the minimap. The text box
draws the blue caret only when it has the focus, and a list box's selected row
is brightened rather than washed with 12% white. The focus now decides more
than the caret: a key goes to the focused control and to nothing else, the way
one focused-gadget index a panel implies, where RWE used to hand every key to
every child of the panel -- which is what made Space press every button on a
panel at once. A `quickkey` still answers from anywhere on the panel (S:78),
except while a text box holds the focus, since a letter that is also a
button's quick key belongs in the name being typed. The characters themselves
come from SDL's composed text rather than from a table of keycodes, so a save
name typed on a layout other than US comes out as typed. A button's caption carries
the drop shadow only where its attribs ask for it and its quick key is
underlined in interface green, and a greyed control is drawn
greyed rather than removed — see the note below the list.

Deliberately different, and recorded in §88 rather than left to be found:

- **RWE has no palette, so the brightening is an alpha blend.** The 1.84x
  median of LIGHT row 30 is reproduced on a mid-tone by compositing white at
  45%; the original's per-index behaviour (a dark pixel lifted 3.8x, a white
  one not at all) cannot be reproduced without carrying palette indices
  through the UI renderer, which nothing else would use.
- **`antiweapons` is still not parsed.** The original gates the coverage ring
  on FBI flags bit 29; RWE gates it on the unit having an `interceptor`
  weapon. In the shipped data the two sets are identical — `ARMAMD` and
  `CORFMD`, `AMD_ROCKET` and `FMD_ROCKET` — so nothing shipped can tell the
  difference, but a mod could.
- **A builder walking to its site already says `Nanolathing`.** The original
  would be running a move mission and saying `Moving`; RWE has one
  `BuildOrder` covering the walk and the work.
> **Ported, 2026-09-10.** This list used to end with a bullet saying greying,
> the caption shadow and the quick-key underline were all still unported. All
> three are in.
>
> Greying went in on 2026-09-02 and the bullet was stale when it was written;
> §19 now carries the note, including why RWE's greyed face is the artwork's
> own frame rather than SHADE row 12.
>
> The other two are `UiStagedButton::render`. The shadow is drawn at (+1, +3)
> in black — interface colour 0, as a literal with the slot named, RWE having
> no runtime interface-colour table — for a button whose gadget sets attribs
> bit 3 (`GuiButtonAttrib::CaptionShadow`, passed on by both of `UiFactory`'s
> button builders). The quick-key underline is a one-pixel `fillColor` in
> (0, 128, 0), interface colour 2 on screen, under the first occurrence of the
> gadget's `quickkey` character in the caption, measured with
> `findCharacterInText` in the font's own per-glyph advances so that it lands
> under the character `drawText` actually drew, at textY + bottom(`I`) + 1.
>
> Two things had to be settled that the finding does not record. **The
> alignment**: RWE's button draws its caption through three paths (left,
> centred, bottom-centred), and the shadow and the underline have to follow
> whichever one the gadget uses, so `render` now works out the caption's
> origin once — reproducing what `drawTextCentered` and `drawTextCenteredX`
> compute internally, rounding included — and all three draws go from that.
> The pressed shift survives the move, `round(a + 1)` being `round(a) + 1`.
> **The case**: the original compares the `quickkey` byte against the caption
> as it stands, and the shipped gui files are authored to suit — SKIRMISH.GUI's
> `SelectMap` carries a lowercase `e` for `Select Map`. RWE has folded the
> quickkey to an SDL keycode by the time a button holds it, so the original's
> case is gone and the match is case-insensitive. That is not only the
> available reading but the better one: four shipped gadgets are authored in
> the *other* case and would lose their underline to an exact test —
> MISSION.GUI's `SELECT`, whose key is `L` against `Select Mission`, and the
> `UNDO` buttons on MUSICRT, SOUNDSRT and VISUALRT, whose key is `c` against
> `Undo Changes`.
>
> **Corrected, 2026-09-11** (`2fc621c0`). A play-test against the original
> showed both of these wrong as first ported: no shadow under the menu
> captions, and a green underline. The listing agrees on both. The first
> reading of `0x4A59A4` missed the attribs test in front of the shadow, and
> took the underline for the caption's colour where `0x4A5CD7` loads
> interface colour 2. Measured from the font's `I`, the line also sits one
> row lower than first ported for hattfont12.

---

## 103. Loading is issued to the transport, and what a click on a unit does

> **Ported, 2026-09-10.** The roadmap's "units ordering themselves aboard
> (select units, click transport)" is closed as **not-TA**: there is no
> passenger-side boarding order in the v3.1 binary, and the count below is
> exhaustive rather than a search that came up empty. What is ported instead
> is the thing next door that *is* the original — the default-action ladder,
> which RWE had written out twice, in the cursor chooser and in the click
> handler, already disagreeing. It is one free function now,
> `computeDefaultAction` in `src/rwe/game/DefaultAction.{h,cpp}`, taking the
> simulation, one selected unit, what is under the cursor and which scheme is
> in force, and returning the order *and* the cursor together, so the two
> cannot come apart. `GameScene.cpp`'s cursor arms and `GameScene_input.cpp`'s
> left, right, minimap and MOVE-armed click handlers all call it.
> `CursorType::Pickup` is new and loads `cursorpickup`, which RWE had never
> used, so the air/crane split can be drawn. The visible change is that a
> click on a friendly unit now does something: in "Right Click" mode it
> guards, repairs, completes, lands on a pad, picks up or moves, where before
> it silently dropped every click that was not an enemy or a nanoframe.
>
> **Three things are deliberately not ported.** A passenger-side board order,
> because there is none — implementing one would be a §88 divergence and
> belongs under features rather than fidelity. Picking up an enemy unit:
> §5 below shows the original has no ownership test at any of the five
> decision points, and RWE keeps its own-units rule, now stated once in
> `DefaultAction.cpp` instead of scattered (§88). And `cursorrevive`, which
> the base game's `CURSORS.GAF` does not ship; a resurrect keeps the reclaim
> cursor, as it already did for an order in flight. The air pickup's order of
> operations — `QueryTransport` and `BeginTransport` before the descent rather
> than after the attach — is the other thing this section turned up, and it
> was fixed in its own commit the same day rather than here. What is left
> unported and recorded is the crane path's ten-second self-attach fallback
> and its `CraneReach` gate, neither of which the original has an equivalent
> of.
>
> `src/rwe/game/DefaultAction.test.cpp` pins the arms, including the pair the
> roadmap item was about: a Peewee over a friendly Hulk guards it, and an
> Atlas over a friendly Peewee loads it.

§31–§41 decode the transport *missions*: what `CanLoadUnit` allows, what the
crane and the Atlas do once a pickup mission is running, and what the carried
state is. This is the layer above them — the click, the command and the
cursor.

**The headline answer is no.** Loading is a *transport-side* order in every
path: the transport is always the unit being commanded and the cargo is always
the click target, never the other way about. What a selection of ordinary
mobile units gets for clicking a friendly transport is, depending on the mouse
scheme, either nothing but a change of selection or a **Guard** order:

| `Interface Type` | Button | Cursor shown | What the click does |
|---|---|---|---|
| **0 — "Left Click"** (the shipped default) | left | 15 `cursorselect` | **selects the transport**, replacing the selection. No order at all |
| 0 | right | (unchanged) | cancels an armed command; otherwise starts the screen drag-scroll |
| **1 — "Right Click"** | left | 15 `cursorselect` | selects the transport |
| 1 | right | 15 `cursorselect` (feedback only) | **`FOLLOW_GROUND` / `VTOL_FOLLOW`** — a Guard order on the transport |
| either, with the MOVE button armed (command 2) | left | 5 `cursordefend` | **`FOLLOW_GROUND` / `VTOL_FOLLOW`** — Guard |

The confidence is **high**, and it rests on a count rather than on a failed
search. There are exactly five calls to `CanLoadUnit` (`0x489A90`) in the whole
image, all five pass the *ordering* unit as the transport, and the ground
mission table has 46 rows of which exactly three touch transports at all
(`BeCarried`, `Ground_Pickup`, `Ground_Unload`) — and `BeCarried` is entered
only from the attach routine, never from an order.

### The command table: fourteen commands, and where they come from

A click becomes an order in two steps. The order panel arms a **command id**
in `BYTE [game+0x2CC3]` (`game` = `ds:0x511DE8`); a click in the world then
runs that id, the clicked unit and the clicked point through a builder that
produces a **mission name** per selected unit. The ids are not the mission rows
and not the cursor ids — three separate numberings, which is the trap in
reading any of this.

The arming sites are one per button, all in `0x419BA0`–`0x41A0E0`, each
`strstr`-matched by gadget name (§19's `0x49FE60` caveat applies) and each
toggling back to 1 when pressed a second time:

| Button | Arms `[game+0x2CC3]` | Order-builder arm | Cursor-chooser arm |
|---|---|---|---|
| — (nothing armed) | **1** | `0x43F9E9` | `0x43E505` |
| `MOVE` | **2** | `0x43F845` | `0x43E8BB` |
| `ATTACK` | 3 | `0x43F154` | `0x43E545` |
| `BLAST` | 4 | `0x43F7E8` | `0x43E850` |
| `UNLOAD` | **5** | `0x43F735` | `0x43E80C` |
| `LOAD` | **6** | `0x43F701` | `0x43E7D3` |
| `DEFEND` | 7 | `0x43F4C7` | `0x43E615` |
| `REPAIR` | 8 | `0x43F46C` | `0x43E5FA` |
| `PATROL` | 9 | `0x43F3B9` | `0x43E5DE` |
| `STOP` | 10 (issued at once, then back to 1) | `0x43F82C` | — (`0x43F098`) |
| (no button) | 11 — `TELEPORT` | `0x43F813` | `0x43E8AE` |
| `RECLAIM` | 12 | `0x43F4F7` | `0x43E65C` |
| `CAPTURE` | 13 | `0x43F6D1` | `0x43E797` |
| building placement | 14 | `0x43F7A0` | `0x43E828` |

Both dispatchers are the same shape — `eax = cmd & 0xFF; dec; cmp 0xD; ja
default; jmp [table + eax*4]` — with tables at `0x4401EC` (order builder, entry
`0x43F0E0`) and `0x43F0A8` (cursor chooser, entry `0x43E490`).

`0x43E470` is the small predicate that says whether a command consults the unit
under the cursor at all: commands **5 (UNLOAD), 10 (STOP) and 14 (placement)**
return 0 and are point-or-nothing orders; everything else returns 1 and takes
the hovered unit as its target. That is why an unload is aimed at a spot and a
load is aimed at a unit.

**`LOAD` is command 6 and only the LOAD button arms it**, which §19 has already
shown is offered only when some selected unit has `canload` — the four base-game
transports (`armatlas`, `armtship`, `cortship`, `corvalk`) plus the two Core
Contingency hover transports. A Peewee can never have the LOAD button, so a
passenger can never arm command 6.

### The order builder, `0x43F0E0`, called once per selected unit

```
0x43F0E0(char* outMissionName, BYTE cmd, Unit* orderer, Unit* target, Point* clickXZ)
```

The issue routine `0x48CF30` walks the local player's unit array (stride
`0x118`), keeps those with `unit+0x110` bit 4 (selected), **skips the click
target itself** (`0x48D07B` — a selected unit that is also the thing clicked
does not order itself), and calls the builder once per survivor at `0x48D0A0`.
So a mixed selection produces a different mission per unit from the same click,
and `orderer` is always the *selected* unit.

Its preamble sets the two relationship flags the rest reads
(`0x43F0EC`–`0x43F12D`):

```
if (target) {
    require target+0x110 & 0x10000000            ; else no order at all
    ebx = 1 if ordererPlayer->allyTable[targetPlayerIndex] != 0   ; ALLIED
    eax = 1 if that byte == 0                                     ; ENEMY
}
```

The polarity is pinned twice over: the guard arm is friendly-only and requires
the `!= 0` flag, and the cursor chooser's red/green pair puts `cursorred` under
the `== 0` flag and `cursorgrn` under the other.

**Command 6, LOAD** (`0x43F701`) refuses without a target, calls
`CanLoadUnit(transport = orderer, candidate = target)` with `ecx = ebp =
orderer` — the transport is the unit being commanded — and produces
`VTOL_PICKUP` or `GROUND_PICKUP` on `canfly`. There is no call with the
operands the other way round anywhere in the image.

**Command 5, UNLOAD** (`0x43F735`) produces `VTOL_LANDING` when a `canload`
aircraft is aimed at an `isairbase` target, and otherwise requires `canload`
and gives `VTOL_UNLOAD` or `GROUND_UNLOAD`.

**Command 2, MOVE** (`0x43F845`) is not a plain move: it is the full context
ladder, and the only armed command that can produce a pickup without the LOAD
button. Read top to bottom; the first arm that fires wins:

```
43f845  require ordererDef+0x245 bit 7 (canmove)          ; else no order
43f854  if (orderer has no mover)          -> "QMOVE"     ; a factory: rally point
43f873  if (no target unit)                -> canfly ? "VTOL_MOVE" : "MOVE_GROUND"
43f893  if (cancapture && ENEMY)            -> "CAPTURE"
43f8b5  if (canreclamate && ENEMY)          -> canfly ? "VTOL_RECLAIMUNIT" : "RECLAIMUNIT"
43f8d9  if (ALLIED && CanRepair && target buildFraction != 1.0)
                                            -> canfly ? "VTOL_HELPBUILD" : "HELPBUILD"
43f918  if (ALLIED && CanRepair && target hp < maxhp)
                                            -> canfly ? "VTOL_REPAIRUNIT" : "REPAIRUNIT"
43f959  if (canfly && ALLIED && targetDef isairbase)
                                            -> "VTOL_LANDING"
43f97d  if (CanLoadUnit(orderer, target))   -> canfly ? "VTOL_PICKUP" : "GROUND_PICKUP"
43f9a4  if (canguard && ALLIED)             -> canfly ? "VTOL_FOLLOW" : "FOLLOW_GROUND"
43f9cd  otherwise                           -> canfly ? "VTOL_MOVE" : "MOVE_GROUND"
```

`CanRepair` is `0x4899B0`; `CanLoadUnit` is `0x489A90` and is again called with
`ecx = orderer` (`0x43F97E`: `mov ecx,ebp`).

**Trace a Peewee down that list against a friendly Hulk.** Not an enemy, so the
capture and reclaim arms are skipped. `CanRepair` fails — a Peewee is not a
builder. Not `canfly`. `CanLoadUnit(peewee, hulk)` fails at its second test,
`0x489AB8`, because the *transport* argument is the Peewee and a Peewee has
`canload = 0`. `canguard` is set on essentially every mobile unit and the Hulk
is allied, so the Peewee gets **`FOLLOW_GROUND`** — ground mission row 27,
handler `0x406300`, display "Guarding". It walks over and escorts the
transport. It does not board it.

**Command 1, the default action**, reads `DWORD [game+0x37EFA]` at `0x43F9E9`
and forks on it. **Interface Type 1's right-click chain** (`0x43FA00`) is
command 2's ladder with `CAPTURE` at the front, and it keeps the transport
arms: capture (enemy) → `RECLAIMUNIT` (enemy) → `HELPBUILD` → `REPAIRUNIT` →
`VTOL_LANDING` (`0x43FB04`) → **`CanLoadUnit` at `0x43FB3D`** → `FOLLOW_GROUND`
at `0x43FB7B` → feature reclaim → `MOVE_GROUND` at `0x4401C2`.

**Interface Type 0's left-click chain** (`0x43FE35`) is shorter, and the
difference is the interesting part:

```
43fe35  if (canattack && ENEMY)     -> recurse into 0x43F0E0 with cmd 3   ; ATTACK
43fe68  if (canreclamate && ENEMY)  -> recurse into 0x43F0E0 with cmd 12  ; RECLAIMUNIT
43fe91  if (CanRepair && under construction / damaged) -> HELPBUILD / REPAIRUNIT
43ff38  if (canresurrect && the feature under the cursor is resurrectable) -> RESURRECT
440065  if (canreclamate && the feature under the cursor is reclaimable)   -> RECLAIM
44019d  if (canmove && has a mover) -> canfly ? "VTOL_MOVE" : "MOVE_GROUND"
        else no order
```

**There is no guard arm and no load arm in the left-click chain.** In the
shipped default scheme, clicking a friendly unit can never produce a Guard
order and can never produce a pickup; the DEFEND and LOAD buttons are the only
routes to either.

### The mission tables

Ground table: base `0x4FC490`, **25-byte records**, 46 live rows, laid out as
`TOTALA-EXE-MISSIONS.md` S:1 describes. The rows this section needs are **14
`BeCarried`** (`0x4FC5EE`, `0x402FC0`, "Being transported"), **27
`Follow_Ground`** (`0x4FC733`, `0x406300`, "Guarding"), **34 `Ground_Pickup`**
(`0x4FC7E2`, `0x406780`, "Loading") and **35 `Ground_Unload`** (`0x4FC7FB`,
`0x406900`, "Unloading"). Row 23 is not a mission at all: the twenty-five bytes
there are float constants that happen to sit inside the array's stride.

**The air table's prose in `TOTALA-EXE-MISSIONS.md` was wrong and is corrected
by this pass.** It said the VTOL table "begins at `0x4FCA7C`, 18 records" where
its own table (correctly) starts at `0x4FCA18` and runs 22 rows; `0x4FCA7C` is
row 4, `VTOL_Unload`. The rows wanted here are **3 `VTOL_Pickup`**
(`0x4FCA63`, `0x4111B0`, "Loading"), **4 `VTOL_Unload`** (`0x4FCA7C`,
`0x411560`, "Unloading"), 2 `VTOL_Landing` (`0x4118E0`) and 5 `VTOL_Follow`
(`0x40FBE0`).

**Every transport-related row is transport-side except `BeCarried`, and
`BeCarried` is not orderable.** It is installed only by `0x4384A0`, called from
the attach routine `0x48AAC0` at `0x48ACF3` when a unit is put aboard something
that is not a repair pad (§37). Nothing names it in the order builder, no
button arms it, and it carries no target of its own.

There is no `Load`, `LoadUnits`, `Board`, `BoardTransport`, `GetLoaded`,
`EnterTransport` or `GotoTransport` string anywhere in the binary. The only
`Transport` literals are `TransportPickup` (`0x5016F4`), `TransportDrop`
(`0x501734`), `EndTransport` (`0x501B0C`), `BeginTransport` (`0x501BA4`),
`QueryTransport` (`0x501BB4`), the three announcements ("Transport mission
failed", "Unit is too large to transport", "Unit is too heavy to transport"),
"Being transported" (`0x501160`), and the FBI key names.

> A lead, recorded but not decoded: the **low byte of the word at `+0x10`**
> agrees with the cursor id the next part derives for the same action in most
> rows — `Ground_Pickup` 12, `VTOL_Pickup` 8, `Ground_Unload` 13,
> `Follow_Ground` 5, `Move_Ground` 14, `Capture` 4, the reclaim rows 11, the
> attack rows 1. It is **not** a reliable source for the cursor:
> `VTOL_Unload` carries 9 where the chooser returns 13, `MobileBuild` carries
> 0, and `Standby` carries 15. Treat the chooser as the authority and this as
> a coincidence worth someone's afternoon.

### The cursor chooser, `0x43E490`, and the cursor table

```
0x43E490(BYTE cmd, Unit* orderer, Unit* target, Point* clickXZ) -> cursor id
```

It is called from exactly one place, `0x48D3E4`, in a loop over the selection
that starts at `0x13` (`cursornormal`) and keeps the **minimum**
(`48d3e9  if (eax < edi) edi = eax`). So a mixed selection shows the cursor of
whichever selected unit has the lowest-numbered applicable action — the
cursor's counterpart to §19's "any, not all" button rule. `0x43F098`, the
switch default, returns 19, which is the sentinel a unit contributes when it
cannot do the armed command at all.

The cursor ids are **1-based indices into an array of GAF handles at
`game+0x14883`**, loaded in one straight-line run at `0x429C9A`–`0x429E94` from
`CURSORS.GAF`. The consumer at `0x4992B9` reads `[game + id*4 + 0x1487F]`,
which is the same array biased by one; `BYTE [game+0x2CBE]` caches the id
currently displayed.

| id | Field | Sequence | Frames |
|---:|---|---|---:|
| 1 | `+0x14883` | `cursorattack` | 10 |
| 2 | `+0x14887` | `cursorairstrike` | 16 |
| 3 | `+0x1488B` | `cursortoofar` | 2 |
| 4 | `+0x1488F` | `cursorcapture` | 13 |
| 5 | `+0x14893` | `cursordefend` | 16 |
| 6 | `+0x14897` | `cursorrepair` | 12 |
| 7 | `+0x1489B` | `cursorpatrol` | 14 |
| **8** | `+0x1489F` | **`cursorpickup`** | 24 |
| 9 | `+0x148A3` | `cursorteleport` | 46 |
| 10 | `+0x148A7` | `cursorrevive` | 18 |
| 11 | `+0x148AB` | `cursorreclamate` | 11 |
| **12** | `+0x148AF` | **`cursorload`** | 16 |
| **13** | `+0x148B3` | **`cursorunload`** | 16 |
| 14 | `+0x148B7` | `cursormove` | 8 |
| 15 | `+0x148BB` | `cursorselect` | 2 |
| 16 | `+0x148BF` | `cursorfindsite` | 2 |
| 17 | `+0x148C3` | `cursorred` | 1 |
| 18 | `+0x148C7` | `cursorgrn` | 1 |
| 19 | `+0x148CB` | `cursornormal` | 1 |
| 20 | `+0x148CF` | `cursorhourglass` | 8 |
| 21 | `+0x148D3` | `pathicon` | 1 |

`cursorrevive` is out of order in the array because the load run assigns it
last (`0x429E94`) into the slot skipped at `0x429D9D`; it is also the only one
of these absent from the base game's `CURSORS.GAF` and present in `rev31.gp3`'s.
`pathicon` is the marching-waypoint sprite of §26, riding in the same array.
`CURSORS.GAF` also ships `cursorprotect` and `MISCART.GAF` a whole parallel set
(`cursor load`, `cursor moveto`, …) — **the exe loads none of them**; no such
strings exist in the binary.

**Command 6, LOAD** (`0x43E7D3`) returns 19 without a target or when
`CanLoadUnit(orderer, target)` fails, and otherwise computes
`((~(def+0x241 >> 11) & 1) | 2) << 2` — that is, **8 (`cursorpickup`) when the
transport is `canfly` and 12 (`cursorload`) when it is not**. That split is the
only air-versus-crane difference anywhere in the cursor path. **Command 5,
UNLOAD** (`0x43E80C`) is `canload ? 13 : 19` and does *not* branch on `canfly`
— the Atlas and the Hulk both show `cursorunload`.

**Command 2, MOVE** (`0x43E8BB`) mirrors its ladder arm for arm: 14 with no
target or no mover, 4 for capture, 11 for reclaim, 6 for both repair arms, 13
when a flyer is over an air base, `canfly ? 8 : 12` for a pickup, 5 for a guard,
14 otherwise.

**Command 1** forks on `Interface Type` at `0x43E505` exactly as the order
builder does:

```
43e505  if (Interface Type == 1) goto 0x43eb02        ; the RIGHT-click scheme
        ; Interface Type 0 -- the LEFT-click scheme:
43e512  if (canattack && ENEMY)     { cmd := 3;  re-enter the switch }
43e52a  if (canreclamate && ENEMY)  { cmd := 12; re-enter the switch }
43edb6  if (CanRepair && under construction) -> 6  (cursorrepair)
43edec  if (target is the LOCAL player's own, selectable, fully built,
            [target+0xFB]==0, and either unattached or on a repair pad)
                                             -> 15 (cursorselect)
43ee4f  if (canresurrect && resurrectable feature) -> 10 (cursorrevive)
43ef6c  if (canreclamate && reclaimable feature)   -> 11 (cursorreclamate)
43f07c  canmove ? 14 (cursormove) : 19

43eb02  ; Interface Type 1 -- selection feedback only:
        if (target is the local player's own and selectable, as above) -> 15
43eb63  if (ENEMY)  -> 17 (cursorred)
43eb74  if (ALLIED) -> 18 (cursorgrn)
43eb85  ... otherwise probe the terrain and features, ending at 14/19
```

Note the fourth arm of the left-click chain: **your own units answer with
`cursorselect` before anything else can fire.** That is why, in the shipped
default scheme, no amount of hovering your own transport produces a load
cursor — and why the LOAD button exists.

### The cursor is the decision

**Left button** (`0x4993B6` for the down-event with a command armed;
`0x4995AE` for a click that ended a drag of under 32 pixels in under 0x19
ticks) → **`0x498F70`**, which dispatches on the **currently displayed cursor
id**, not on the command:

```
498f77  cl = [game+0x2CC3]                     ; armed command
498f7d  if (cl == 14) { building placement, separate path }
499027  dl = [game+0x2CBE]                     ; the cursor on screen right now
49902d  if (dl == 15 /*cursorselect*/) { 0x48C7F0(click); return }   ; select that unit
499041  if (dl >= 17) {                        ; red, green, normal, hourglass, pathicon
            if (Interface Type == 1 && cl == 1) { clear the selection }
            return                             ; otherwise the click does nothing
        }
49906d  0x48CF30(click, cl, 0, &game+0x2CAA, 0, 0)     ; issue the order
49908c  if (!shift) unarm the command and un-press its button
```

That is the tidiest thing in this whole path: **the cursor is the decision.** A
left click issues an order exactly when the chooser returned an id below 17,
selects when it returned 15, and does nothing otherwise. A real drag goes to
`0x48C390` instead, which is the rubber-band box selection and issues nothing.

**Right button down** → `0x499100`: an armed command is cancelled first
(`0x499107`), Interface Type 0 then starts the screen drag-scroll
(`0x499162`), and Interface Type 1 calls `0x48CF30` with command 1 directly
(`0x4991D5`). So cancelling with the right button works in **both** schemes,
only "Right Click" mode issues on it, and — because that path does *not*
consult the cursor — in "Right Click" mode the cursor reads `cursorselect` over
your own transport while the right button still issues a Guard order on it.

`DWORD [game+0x37EFA]` is the registry value **`Interface Type`** under
`HKCU\Software\Cavedog Entertainment\Total Annihilation` (read at `0x42F9A0`,
written back at `0x430F1C`), clamped to 0..1 at `0x42F9CF` and defaulting to
**0** when absent. Its UI is the `LEFTCLICK` gadget on `SPEEDSRT.GUI` /
`SPEEDS.GUI`, labelled `"Left Click|Right Click"` (§65).

The unit under the cursor is resolved once per frame by `0x48CD80` into
`WORD [game+0x2CBA]`, from the drawn-unit list by a ray test with **no owner
filter**; `0x48CF30` converts it to a pointer only when `0x43E470` says the
command in hand takes a unit target.

### An aside the code is unambiguous about: enemies are loadable

Neither `CanLoadUnit` (§31 already noted this), nor the LOAD cursor arm
(`0x43E7D3`), nor any of the three order-builder call sites applies an
ownership or alliance test. In the command-2 ladder the `CanLoadUnit` arm at
`0x43F97D` sits *after* capture and reclaim but is itself unguarded, and a
transport has neither `cancapture` nor `canreclamate`, so it is reached with an
enemy target; and with the LOAD button armed, `0x43E7D3` will happily hand back
cursor 8 or 12 over an enemy. §31's parenthetical "the UI only offers the
cursor on the player's own units" and §39's "RWE should keep its UI-level
own-units-only rule" both read as though the UI supplies the missing test —
**it does not**. That rule is a deliberate RWE house rule, recorded in §88, not
a description of the original. Whether the resulting pickup then completes was
not traced and was not play-tested.

### What the transport's script sees, and when

The roadmap's second item asks whether the `TransportPickup` boom animation is
timed to the actual attach. **On the crane path it is the other way round: the
attach is timed to the animation, and the engine never attaches at all.** It
starts the script and polls a field.

`Ground_Pickup` (`0x406780`) state 2 is the whole of the engine's involvement:
it starts `TransportPickup(cargoId)` through `0x4B0A70`, plays announcement
slot 0xC, bumps an attempt counter and sets a 15-tick timer. State 3 sleeps
while the COB `BUSY` value at `unit+0x10F` bit 1 is set, and state 4 tests
`target+0x86 != 0` — *is the cargo attached to anything at all*. The attach
itself happens inside the script when it executes `ATTACH_UNIT`. Three
consequences worth naming:

- **There is no engine-side range gate for the crane.** The script's own
  `BoomCalc` reach test is the only one. The engine's check in state 0
  (`0x4067B1`) is a *footprint* test, not a distance test, and its failure
  message is "Unit is too large to transport".
- **The announcement is not timed to the attach on the ground path** — slot 0xC
  plays when the *script starts*, before any hook has touched anything. On the
  air path (`VTOL_Pickup` state 4, `0x411479`) the same slot plays *at* the
  attach. The two genuinely differ.
- **A script that never sets `BUSY` costs 15 ticks, not a failure.** State 4
  finds nothing attached and, while the attempt counter is under 3, installs a
  ground move goal at the cargo's position. Three attempts exhausted, it returns
  9 and parks for `rand(30)+30` ticks before restarting from state 0. There is
  no timeout after which the engine takes the unit aboard itself.

`VTOL_Pickup` (`0x4111B0`) is the reverse. State 2 calls `QueryTransport` after
arriving within 48 wu of the cargo at cruise altitude and **before** any
descent, and the script returns the piece to hang the cargo from. State 3 calls
`BeginTransport(targetDef+0x16E)` — the cargo's model height — still before the
descent, and the Atlas's `BeginTransport` is a single `MOVE_NOW link y -> -h`;
the engine then resolves that piece's offset and descends until the hook sits
on the cargo's roof. State 4 does the attach itself, `0x48AAC0`, with no script
call at all. So on the air path **the animation is timed to the engine's
attach**, where on the crane path the attach is timed to the animation.

`EndTransport` has five call sites and two are not where you would look:
`0x411E27` runs it inside `VTOL_Landing` whenever `transport+0x8A` is
non-empty — the Atlas folds its arms whenever it *lands* loaded, not only when
it lets go — and `0x411D9C` on that mission's abort path. The others are
`VTOL_Pickup`'s abort (`0x411489`), `VTOL_Unload` state 2 (`0x411790`), and
`VTOL_LandIfCan` (`0x40F42E`).

`Ground_Unload` state 0 starts `TransportDrop(passengerId, (intX<<16)|intZ)`
and the same `BUSY` protocol runs. The release is the script's `DROP_UNIT`, and
the engine's contribution is the legality veto inside that opcode's handler
(`0x4813B0`, §37): a `DROP_UNIT` onto an illegal cell does nothing at all and
the unit stays hooked, which is what makes the mission retry.

### Sea transports and the AI

§39 answers "how do the crane transports load" completely. What it does not
carry, and what an AI would need, is the surrounding geography. Recorded as
gaps rather than findings, because none of it was decoded in this pass:

- **Whether the original's computer player uses transports at all** was not
  traced. Nothing transport-shaped turned up in the AI while walking these
  tables, but that is an absence of evidence.
- **Nothing in the original ever moves the passenger.** `Ground_Pickup` state 4
  installs the move goal on the *transport* with tolerance 0. The cargo is
  never ordered anywhere. Whatever RWE does about meeting points is RWE's own
  invention with no original behind it.
- **`Ground_Unload`'s arrival tolerance** is the one hover-specific number:
  `int(footprintZ * 16 * 1.5)` world units when `canhover` is set, 0 otherwise
  — 96 wu for the 4-footprint Bear and Turtle (§35). That is the beach-reach
  allowance, and the closest thing the original has to a "dock here" rule.
- **The load predicate already forbids the interesting case**: a sea or hover
  transport refuses any candidate whose `minwaterdepth >= 0` (`0x489B44`), so a
  Hulk can never carry a ship, and every surface ship's footprint exceeds
  `transportsize = 3` anyway. An AI planning sea transport is only ever
  planning to move *land* units *across* water.
- **One unload order sets down one unit** (§35); a full Hulk needs the order
  re-issued twenty times, which the `Standby` re-execution loop does for the
  human player. An AI queueing unloads must queue one per passenger.

### What RWE does with this

The ladder is `computeDefaultAction` in `src/rwe/game/DefaultAction.{h,cpp}`,
over `(simulation, scheme, orderer, hovered unit, hovered feature)`, returning
the order to issue — or "select it", or "move to the point under the cursor",
or nothing — together with the cursor. Three schemes, named after the arms they
run: `LeftClickDefault`, `RightClickDefault` and `MoveButton`.
`GameScene::selectionDefaultCursor` folds it over the selection with
`preferredCursor`, which is the original's minimum-id rule; `GameScene::
issueDefaultAction` runs it for one unit and issues or queues on the shift key.
The old arrangement — an `any_of` ladder in `GameScene.cpp` and an if-chain in
`GameScene_input.cpp` — is gone.

| Question | Original | RWE before | RWE now |
|---|---|---|---|
| Passenger-side board order | none | none | none — and now known to match rather than merely to coincide |
| Selected transport, click cargo with LOAD armed | `GROUND_PICKUP` / `VTOL_PICKUP` | `LoadOrder` per selected unit | unchanged |
| Cursor with LOAD armed | 8 `cursorpickup` if `canfly`, else 12 `cursorload`; 19 over anything unliftable | always `CursorType::Load` | the split, and the plain arrow when the hover cannot be lifted |
| Ownership test at order time | **none**, anywhere | `isFriendly` in two places | one place, `canLoad` in `DefaultAction.cpp`, still own-units-only (§88) |
| Default action on a friendly unit, "Left Click" | selects it | selected it | unchanged |
| Default action on a friendly unit, "Right Click" | guard, repair, complete, land, pick up, or move | **nothing at all** unless it was a nanoframe | the ladder |
| Default action on an enemy, "Right Click" | capture if `cancapture`, else reclaim if `canreclamate` | always attack | capture, then reclaim, then attack |
| MOVE button armed | the whole ladder, with its own cursors | land on a pad, else move | the whole ladder |
| Whether the cursor gates the click | yes below 17, selects at 15 | two independent ladders that disagreed | one ladder; in "Left Click" the cursor is the decision, in "Right Click" it is feedback, as in the original |
| `Interface Type` | registry DWORD 0/1, `LEFTCLICK` gadget, default 0 | `globalConfig->leftClickInterfaceMode`, same polarity | unchanged |
| Crane pickup | script does everything; engine polls `target+0x86`; no range gate; three attempts then park | `TransportPickup` then a 10-second self-attach fallback, and a `CraneReach` gate | unchanged, and both differences recorded here |
| Air pickup | `QueryTransport` and `BeginTransport` before the descent | both after `loadUnitIntoTransport` had already succeeded | the two calls sit either side of the descent, fixed in its own commit rather than this one |
| Pickup announcement | ground at script start, air at the attach | neither played | unchanged |

Two arms are RWE's own and are marked as such in the source. The **attack arm
in the right-click ladder** is kept although the decode of `0x43FA00` lists
none: right-clicking an enemy has always attacked it here, and removing it on
the strength of an elided list would be the worse mistake. And the **resurrect
arm shows the reclaim cursor**, because `cursorrevive` is not in the base
game's `CURSORS.GAF`.

### What is unsettled

- **Whether an enemy unit can actually be picked up.** The order layer permits
  it at all five decision points and `CanLoadUnit` has no team test, so the
  order will be issued and the mission will start. Whether `ATTACH_UNIT` or
  `0x48AAC0` refuses later was not traced, and this was not play-tested. Do not
  port "transports can steal enemy units" on the strength of this section.
- **Whether `0x43FA00` really has no attack arm.** The chain was read from its
  capture arm onward; an attack arm ahead of it, or a recursion into command 3
  like the left chain's, would not have shown up in that reading. RWE keeps its
  own attack arm until this is settled.
- **`BYTE [game+0x2CC6]`**, the mouse state byte, is used as opaque bits above.
  Only bit 3's role (a selection box is being dragged) is firmly established;
  bit 2's is guessed from context.
- **Command 10's issue path.** The STOP button writes 1 to `[game+0x2CC3]` and
  acts immediately rather than arming 10, yet command 10 has a live builder arm
  (`0x43F82C` → `"STOP"`) and appears in `0x43E470`'s no-target list. Something
  issues it; that something was not found.
- **`0x489960` and `0x4899B0`**, the reclaim and repair predicates, are used as
  named black boxes here; only their positions in the ladders were established.
- **`0x43E828`** (command 14, the placement cursor) and the feature probes in
  the command-1 and command-2 arms were read only far enough to identify their
  return values. RWE's own feature arms therefore fire only when nothing is
  under the cursor but the feature, where the original probes the map cell
  after its unit arms have failed.
- The **frame counts** in the cursor table come from a prior `gaf.py` sweep of
  every shipped GAF, not from a fresh read of `CURSORS.GAF`. The sequence
  *names* are from the binary and are certain.
- The mission tables' **`+0x10` word** is tabulated above only as a lead; its
  meaning is not decoded, and its correlation with the cursor id has three
  counter-examples.

---

## 104. The end of a game: a banner, a fade, and a chart that runs its bars up

The module is `endgame.cpp` -- the original leaves its own source path in the
binary at `0x502A97`, `c:\cavedog\wargame\endgame.cpp` -- and it occupies
`0x41D700`-`0x420600`. Its whole shape is one function, `0x41F7F0`, driven by a
nine-way state in `DWORD [game+0x39057]` through the jump table at `0x4205AC`:

| State | Handler | What it does |
|---|---|---|
| 0 | `0x41F830` | Takes a copy of the last game frame (the surface is created by name, `"Copy of last game frame"` at `0x502BE0`) and blits the screen into it |
| 1 | `0x41F976` | Waits on the message box, if one was raised |
| 2 | `0x41FA3D` | Arms the fade: counter `[game+0x39067] = 10`, next step at `now + 1` |
| 3 | `0x41FA8F` | One fade step a tick, ten of them, then sets the done flag |
| 4 | `0x41FB6C` | The campaign's CD check |
| 5 | `0x41FC12` | Campaign mission messages; otherwise builds the chart and goes to 7 |
| 6 | `0x41FDF8` | The campaign's full-screen outcome picture, with `"Click to continue."` |
| 7 | `0x41FF42` | The chart, running its bars up |
| 8 | `0x42055B` | The chart standing still, waiting for a button |

Only 0, 2, 3, 5, 7 and 8 are on a skirmish's path.

**The fade is ten steps.** `0x41FA3D` sets the counter to ten and `0x41FA8F`
spends one a tick -- `[game+0x3905F] = now + 1` each time -- calling
`0x4BF4D0(0, &rect, counter - 0x1D)` over the whole screen. The level therefore
runs -19, -20 ... -28: darker each step, and black at the end. A third of a
second at the original's thirty ticks a second.

**The clock.** `0x4B6340` is `GetTickCount() * ticksPerSecond / 1000` and
`0x4B6330` returns that `ticksPerSecond` on its own, so every interval below is
in ticks of that rate.

### The chart

The background is `bitmaps/OUTCOME0.PCX`, shown by `0x4288D0("outcome0")` at
`0x41F1C8`; a campaign win gets `outcome1` instead. It is a 640x480 bitmap that
paints the frame, the button surround, and all eight column headings -- **Name,
Kills, Losses, Energy Produced, Metal Produced, Excess Energy, Excess Metal,
Score**. Over it the original builds `guis/ENDMSN.GUI` (`0x4AA8F0` at
`0x41F0D3`) and enables exactly one of its buttons, `MainMenu` (`0x4A76B0` at
`0x41F1E4`); the rest of that gui -- the mission list, Start, Save, Load, the
difficulty dial -- belongs to the campaign's end-of-mission screen, which is
the same gui worn differently.

**The score table** is ten records of 58 bytes at `game+0x38DD9`, filled by
`0x41DC20` and zeroed first with a `rep stos` of 145 dwords. Per record:

| Offset | Field | Source |
|---|---|---|
| +0x00 | name, 30 bytes | `player+0x2B` |
| +0x1E | Kills | `(int16)player+0xFC` |
| +0x22 | Losses | `(int16)player+0xFE` |
| +0x26 | Energy Produced | `(int)(double)player+0xAC` |
| +0x2A | Metal Produced | `(int)(double)player+0xB4` |
| +0x2E | Excess Energy | `(int)(double)player+0xCC` |
| +0x32 | Excess Metal | `(int)(double)player+0xD4` |
| +0x36 | Score | computed, below |

**The score** (`0x41DDBE`-`0x41DE11`) is

```
score = (int)(kills * killmul) + (int)((gameTicks / 30) * timemul)
if (score < 0) score = 0
```

`killmul` and `timemul` are floats read out of the **map's OTA** at `0x4365EB`
and `0x436603` into `[gametype+0xD54]` and `[gametype+0xD58]`. Each term is
truncated to an integer on its own -- the original converts twice, once per
multiply -- so the two truncations do not combine. The shipped default is
`killmul=50` and `timemul=0`, which scores a game on kills alone.

**What a full bar means.** Seven dwords at `game+0x3918F` hold the maximum for
each column, seeded at `0x41DCA4` with **10, 10, 100, 100, 100, 100, 100** and
then raised to the largest value any player reached (`0x41DE12` onwards). The
floor is what stops one kill in a quiet game drawing as a full bar.

**Geometry**, from `0x41E420`, and confirmed against the artwork itself -- the
cell runs measured out of `OUTCOME0.PCX` agree with the gadget rects to the
pixel:

- name cell x 16, width 90 (`0x41E556` builds the `PlayerColor%d` gadget there);
- bars at x 112, 186, 260, 334, 408, 482, 556 -- 112 and a stride of 74 -- each
  67 wide and 18 high, in cells the artwork draws 68 wide;
- rows start at y 93 and step 20, ten of them.

**The run-up.** `[game+0x3906B]` is the column, 0 to 6, and the jump table at
`0x4205D0` has one arm each: **Kills, Losses, EProduced, MProduced, EWasted,
MWasted, Score** -- left to right. Each arm walks the ten records, builds the
gadget name with `sprintf("%s%d", label, index)` (the `"%s%d"` at `0x502B30`)
and enables that player's bar; then `0x47F1A0` starts the group, the next
column is scheduled at `now + 10` (`0x42053A`) and the column index is bumped.
A bar advances by `max(target / 15, 1)` per gadget update -- the `1/15` at
`0x4FD008` and the `1.0f` floor at `0x4FD00C` -- so every bar is full after
fifteen frames whatever it is counting.

A **click during the run-up** (`0x4C1AB0` tested at `0x41FFD9`) jumps to
`0x420028`, which enables all seven groups at once and then
`ActivateAllStatBars`. When every bar has reached its target the state goes to
8 and the screen simply sits there.

**The title.** `anims/ENDMSN.GAF` carries `victory` (129x29) and `defeat`
(101x29) beside an `outcdivider`; `OUTCOME0.PCX` leaves an empty band above its
column headings for them. These are not the `igvictory`/`igdefeat` banners the
world renderer flashes over the battlefield (§70) -- those are 117x29 and live
in `IGTITLES.GAF`.

**What sets the banner in the first place** is `0x4169D0` (victory) and
`0x416A30` (defeat), which each call `0x486F10` -- the routine that destroys a
player's units -- and then set bits in `game+0x3923B`: bit 4 mission complete,
bit 5 `igvictory`, bit 6 `igdefeat`, bit 2 game over.

### Not decoded

**Which sound the two beeps are.** `sounds/BEEP1..6.WAV` and
`VICTORY2/VICTORY4.WAV` all ship, but no string in the executable names any of
them, `gamedata/SOUND.TDF` has no entry for them, and no gui file carries a
sound field that reaches them. `0x46C620`, which the endgame calls with 7 at
`0x41F897`, turns out to be the statistics recorder rather than a sound call.
RWE plays `BEEP6` twice and says so at the call site.

---

## 107. Download menus: how a patch adds a button to a builder it does not ship

**Why this was read.** Reported from play: "the construction ship should have
three pages of build options." RWE showed one. `ARMCS1.GUI` is the only page
the construction ship ships, and nothing in RWE looked anywhere else. The rest
of its menu is 70 small TDFs under `download/`, shaped like this:

    [MENUENTRY1] { UNITMENU=ARMCS; MENU=3; BUTTON=5; UNITNAME=ARMUWMEX; }

**How much was missing.** Audited over the whole install (`rev31.gp3`,
`btdata.ccx`, `ccdata.ccx`, `totala1.hpi`, `totala2.hpi`, in RWE's mount
order): **111 buttons on 26 builders, and for 53 units this is the only way
they can be built at all** -- the Vulcan, the Buzzsaw, the Krogoth gantry, the
Flakker, the fortification wall, the Sniper, the Spy, the floating defences and
the underwater extractor on the construction ship. None could be built in RWE.
There was a second, smaller bug under it: RWE's default for the directory was
`downloads`, and the original's is `download`.

### Verified, from the binary

- **The loader is `0x42DCF0`, called exactly once**, from `0x4918CF`, in the
  long row of no-argument initialisers that runs at startup. There is no other
  caller in `.text`. Download menus are read once, not per map and not when a
  panel opens.
- **It scans `download\*.TDF`**: the three literals are `"download"`
  (`0x503730`), `"*"` (`0x50372C`) and `"TDF"` (`0x50341C`), handed to the
  path builder at `0x4290F0` and then the directory enumeration at `0x4BCA30`.
- **The string `MENUENTRY` is not in the binary.** Zero hits. The block's name
  is not looked for; the blocks of a download TDF are walked by position.
- **Four keys are read**: `UNITMENU` and `UNITNAME` through the string fetch
  (`0x4C48C0`), `MENU` and `BUTTON` through the integer fetch (`0x4C46C0`) with
  a default of 0. The fifth string in that cluster, `DOWNLOADMENU`, is not a
  key: it is the allocation tag handed to `0x4D83B0`. Nobody should look for a
  `DOWNLOADMENU=` field.
- **The fill routine is `0x41ACE0`**, called from the panel refresh near
  `0x41B800` whenever the displayed unit changes. For each record it compares
  the stored `MENU` word, raw, against the open page's own field at `+0x21E`,
  then the `BUTTON` byte against the slot, and on a match writes the unit name
  into the gadget (`+2`) and marks it live (clears bit 0 of the word at
  `+0x13C`, sets the byte at `+0x2A` to 4, ORs 1 into the word at `+0xB4`).
  **It is one mechanism**: nothing in it distinguishes a page that shipped a
  GUI file from one that did not.
- **`%sGEN.GUI` is a dead end**, recorded so it is not walked again.
  `0x41B0F0` loads `ARMGEN.GUI` / `CORGEN.GUI`, and they exist, but it is the
  *alternative* branch to `0x41ACE0` (flag at `0x37EBE`), and the file holds
  the generic orders strip -- no `IGPATCH`, no build grid.

### Verified, from the data

- **`page = MENU - 1`, counting pages from one.** `ARMACK2.GUI` holds three
  units and then three gadgets named `IGPATCH`; ARMACK's download entries at
  `MENU=3` are `BUTTON=3,4,5`, landing exactly on those three. The construction
  ship's entries are six at `MENU=3` and one at `MENU=4`: pages two and three,
  which is the three pages reported. No builder's numbering has a gap.
- **`BUTTON` is a 0-based index into the 2x3 grid**, row-major: `(0,27)
  (64,27) (0,91) (64,91) (0,155) (64,155)`, each 64x64. `BUTTON=0` is real
  (`ARMFDRAG` on the ship's third page). The binary compares `BUTTON - 1`
  against the slot the caller passes; that caller was not traced, so the two
  presumably cancel. **Trust the data here, not that decrement.**
- **An empty slot is a Button named `IGPATCH`, or one carrying attribute 32.**
  Both tests are needed: `CORACA2.GUI` is the one shipped page whose `IGPATCH`
  gadgets have `attribs=0`, and asking only for the attribute left `CORFLAK`,
  `CORFORT` and `CORTOAST` nowhere. With both, 49 of 49 shipped pages resolve
  to six slots.
- **No download unit has a frame in its builder's page GAF.** All 111 ship a
  `unitpics/<UNIT>.pcx` instead, which is therefore what the button is drawn
  from.

### Inferred, and built anyway

- **What a page with no GUI file looks like.** The code that makes the six
  gadgets for such a page was not found, and no template file exists. Since
  the fill routine cannot tell the two cases apart, and every shipped build
  page has the identical grid, RWE copies the builder's first page, blanks its
  six slots to `IGPATCH`, and fills from there. This is RWE's choice.
- **Two entries for one slot**: not traced. RWE keeps the first, in VFS order.
- **An unknown `UNITMENU` or `UNITNAME`**: the shape of a not-found path was
  seen and its outcome was not. RWE skips the entry.
- The record layout (189 bytes a file, 37 a record, so perhaps five records a
  file) is arithmetic only. No shipped file has more than three.

### What RWE does

`src/rwe/game/DownloadMenus.{h,cpp}`, called from `LoadingScene` after the
builder GUIs are read. The log line is the check: on the full install it reads
`Download menus: 111 buttons placed, 0 skipped`, which is the audit's figure.
Because the AI's build tree is made from the same pages, the AI gains all 53
units by the same stroke -- which is what turned construction ships from a
measured loss into a measured win (see `targetConstructionShipCount`).

## 108. What Space shows: a strip from the bottom and the players at the top right

**Why this was read.** Reported from play: in the original, while Space is
held, a tab rises from the bottom of the screen with the game time, the
player's own unit count and the game speed, and another at the top right lists
the players in their colours with kills and losses. RWE slid the side panel
away (§76) and showed neither. §76 is right and was incomplete: it stopped
reading its function at the slide arithmetic.

### Verified

- **Space is polled, not pressed.** Both routines call `IsKeyDown`
  (`0x4C1B80`) with a literal `0x20`. They are called one after the other from
  the world render, `0x469F65` and `0x469F9F`, with the same rectangle.
- **The players' list is drawn by the side panel's own updater**, `0x4948E0`,
  on the side panel's own slide (`ds:0x51F2D8`, 0 to `0x7D`) and gate: the F4
  latch (`game+0x37F06` bit 7), or Space held with the cursor off the panel.
  The two always move together. F4 alone therefore brings the list out.
- **The bottom strip is separate**, `0x4689C0`: its own slide at
  `game+0x37E90`, a signed 0 to -31, driven by Space alone -- there is no test
  of the F4 bit anywhere between the function's entry and its key poll. It is
  polled on a throttle (`ds:0x51E544`, next poll fifteen timer units on) and
  each poll moves it `max(1, remaining / 3)`. At exactly 0 every draw call is
  skipped. It plays the side panel's two sounds, `Panel` on leaving rest and
  `Options` on settling.
- **Its graphic** is `LIGHTBAR` in `anims/commongui.GAF`, **frame 1**, 507 by
  32, fetched once at `0x4679E3` and cached at `game+0x37E94`.
- **Its text**, each format read at its address:
  `"%s : %02d:%02d:%02d"` with `Game Time` -- always with the hours;
  `"%s : %d  (Max %d)"` with `Total Units`, the count being the word at
  player record `+0x144` of the LOCAL player (`game+0x2A42`) and the maximum
  the first word of the settings block at `game+0x37EE6`;
  `"%s %s"` with `Game Speed` and either `Normal` (speed word `game+0x38A4D`
  equal to 10) or `"%+d"` of the word less ten -- no colon on this one -- and
  a further `" (%+d)"` while the wanted speed (`game+0x38A4B`) has not landed.
- **The list**: ten records at `game+0x1B63`, 331 bytes each. A row is a colour
  swatch blitted as a graphic (the colour index is byte `+0x96` of the
  sub-record at `+0x27`, looked up through the table at `game+0x148DB`), the
  name at `+0x2B`, kills at `+0xFC` and losses at `+0xFE` as plain `"%d"`. The
  local player's row has two nested filled rectangles under it. The box is
  `40 * rows + 46` tall, at y = 32, its x being the screen width less the
  slide plus 125 -- off the right edge at rest, flush with it when out.
- **Order is a stored rank byte (`+0x148`), not a leaderboard.** It is closed
  up when a player drops and otherwise left alone.
- A row is skipped for an unused record, a state byte outside 1 to 3, a
  `+0x146` of 10, a record that has and has had no units, or bit `0x40` of
  `+0x9B` in the colour sub-record. **No alliance or visibility test was found
  in the loop**, so it appears to list everyone.

### Not found

The font and colour handed to the text primitive `0x4A50E0`; where across the
screen the strip sits and how its three lines are laid out inside 32 pixels;
the list's exact width; what palette the highlight's two fill constants (31
and 20) index; and the `game+0x37EF6 == 2` mode that reads kills and losses
from `+0x104` / `+0x106` instead.

### What RWE does

`GameScene::updateStatsBarSlide` and `renderSpaceTabs`. The slides, the gates,
the strings and formats, the graphic, the box height and the rule that the
list rides the side panel are the original's. **RWE's own, for want of a
decode:** the strip is centred under the world view with its three items laid
left, centre and right on one line; the list is 125 wide on a translucent
ground with RWE-chosen highlight colours, and uses the `radlogo` colour dots
as swatches; and `Total Units` stops at the count, because RWE has no unit
limit to print after it.

---
