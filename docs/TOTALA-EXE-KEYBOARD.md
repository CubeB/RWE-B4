# What the original executable does: the keyboard

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §72, §73, §74, §75, §76, §77, §78, §79, §80, §81.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

72. [The key pipeline: translator, ring buffer, dispatch](#72-the-key-pipeline-translator-ring-buffer-dispatch)
73. [The dispatch at 0x495e90, all forty cases](#73-the-dispatch-at-0x495e90-all-forty-cases)
74. [Ctrl+letter is data-driven: `CTRL_%c` and the FBI Category field](#74-ctrlletter-is-data-driven-ctrlc-and-the-fbi-category-field)
75. [The digits, Alt, and the `SwitchAlt` registry value](#75-the-digits-alt-and-the-switchalt-registry-value)
76. [F4, Space, and the sliding panel](#76-f4-space-and-the-sliding-panel)
77. [The second layer: screenshots, movies, and the gated debug keys](#77-the-second-layer-screenshots-movies-and-the-gated-debug-keys)
78. [The order keys are GUI data: `quickkey`](#78-the-order-keys-are-gui-data-quickkey)
79. [What RWE binds today](#79-what-rwe-binds-today)
80. [The gap table](#80-the-gap-table)
81. [Implementation list for RWE, in priority order](#81-implementation-list-for-rwe-in-priority-order)

## 72. The key pipeline: translator, ring buffer, dispatch

Keys reach the game through a ring buffer at `game+0xf2` (fetched by
`0x4c1ab0`, pushed by `0x4c1b20`). Plain printable characters go in as their
ASCII codes; everything else goes through the translator at `0x4c1d50`, which
is keyed on the Windows VK code:

```
  4c1d50: sub    esp,0x8
  4c1d54: push   0x11                       ; VK_CONTROL
  4c1d56: call   DWORD PTR ds:0x4fc350      ; GetKeyState
  4c1d5c: and    al,0xfe
  4c1d5e: mov    ecx,DWORD PTR [esp+0x10]   ; the VK code
  4c1d62: neg    ax
  4c1d65: sbb    eax,eax
  4c1d67: lea    esi,[ecx-0x13]
  4c1d6a: neg    eax                        ; eax = 1 if Ctrl is down
  4c1d6c: cmp    esi,0x68
  4c1d6f: ja     0x4c20ea
  4c1d75: xor    edx,edx
  4c1d77: mov    dl,BYTE PTR [esi+0x4c2230] ; VK-0x13 indexes a byte table
  4c1d7d: jmp    DWORD PTR [edx*4+0x4c21d0] ; 24-way jump table
```

The letter and digit paths settle the modifier question. With the Ctrl flag
set, a letter gets `+0x69` (`'A'` becomes `0xAA`) and a digit gets `+0x94`
(`'1'` becomes `0xC5`); without it the letter is lowercased and pushed as
ASCII. An F-key handler is one `neg`/`sbb` idiom:

```
  4c1fca: neg    eax                ; eax = Ctrl flag
  4c1fcc: sbb    eax,eax            ; 0 or 0xFFFFFFFF
  4c1fce: and    al,0xec
  4c1fd0: add    eax,0xe2           ; F1 = 0xE2 plain, 0xCE with Ctrl
```

So `0xCE`-`0xD9` is **Ctrl+F1..Ctrl+F12**. Shift and Alt do not alter the
pushed code at all -- the handlers poll them live through `0x4c1b80`, whose
own jump table (byte table `0x4c1c6c`, targets `0x4c1c48`) maps the special
codes to `GetKeyState` calls. That table is the decoder ring for the
modifiers:

| TA code | Key | Evidence |
|---|---|---|
| `0x09` | Tab | dispatch table |
| `0x0D` | Enter | dispatch table |
| `0x1B` | Escape | dispatch table |
| `0x20`-`0x7E` | printable ASCII | translator / char path |
| `0xAA`-`0xC3` | **Ctrl**+A..Z (`'A'+0x69`) | translator `+0x69` on the Ctrl flag; handler formats `"CTRL_%c"` |
| `0xC5`-`0xCD` | **Ctrl**+1..9 (`'1'+0x94`) | translator `+0x94` on the Ctrl flag |
| `0xCE`-`0xD9` | **Ctrl**+F1..F12 | `neg/sbb` on the Ctrl flag |
| `0xE2`-`0xED` | F1..F12 | translator |
| `0xEE` / `0xEF` | Insert / Delete | translator, VK `0x2D`/`0x2E` |
| `0xF0` / `0xF1` | Home / End | translator, VK `0x24`/`0x23` |
| `0xF2` / `0xF3` | PgUp / PgDn | translator, VK `0x21`/`0x22` |
| `0xF4`-`0xF7` | Left, Up, Right, Down | translator; polled for map scroll |
| `0xF8` | Pause | translator, VK `0x13` |
| `0xF9` | Shift (state poll only) | `0x4c1bb5`: `GetKeyState(0x10)` |
| `0xFA` | Ctrl (state poll only) | `0x4c1bc9`: `GetKeyState(0x11)` |
| `0xFB` | Alt (state poll only) | `0x4c1ba1`: `GetKeyState(0x12)` |

`"CTRL_%c"` (at `0x5094f0`) is the smoking gun for the Ctrl reading; Alt for
`0xFB` is `GetKeyState(VK_MENU)`, and HELP.TDF's "ALT1 - ALT9|Select squad"
line confirms it end to end.

Arrow codes `0xF4`-`0xF7` and Space `0x20` are polled per frame, not
dispatched -- arrows scroll the map, Space is covered with F4 below. Insert,
Delete, Home, End, PgUp and PgDn are produced by the translator but hit the
dispatch's default case: **no in-game binding found** for them.

---

## 73. The dispatch at 0x495e90, all forty cases

The dispatch pops a key, samples Shift into `esi`, and indexes the byte table:

```
  495eab: push   0xf9                       ; Shift
  495eb0: call   0x4c1b80                   ; is it down?
  495eb5: mov    esi,eax
  495eb7: lea    eax,[ebp-0x9]              ; key - 9
  495eba: cmp    eax,0xef
  495ebf: ja     0x4965ce
  495ec5: xor    ecx,ecx
  495ec7: mov    cl,BYTE PTR [eax+0x496694] ; 0xF0-entry case selector
  495ecd: jmp    DWORD PTR [ecx*4+0x4965f4] ; 40-way jump table
```

Every non-default entry, with what the handler was found to do:

| Key(s) | Case target | Action |
|---|---|---|
| Esc | `0x495ed4` | menu open: close it. Else first press: press the main panel's `STOP` gadget (resets the order mode); further press: deselect all (`0x48bd00`) and close floating panels (`0x491d70(1)`) |
| Tab | `0x496133` | multiplayer: toggle the TABMENU bar (`0x495010`); otherwise fall into the F2 case -- GAME OPTIONS |
| Enter | `0x4964fd` | "SmallButton" UI sound, then `0x494050`: open the message bar -- `TALK.GUI` single player, `TALK2.GUI` multiplayer; refused for watchers |
| `!` `#` `*` backquote `~` | `0x496058` | toggle bit 0 of `[game+0x37f06]` and re-derive (`0x430f00`) -- **damage bars** (HELP.TDF line 27; registry value `damagebars`) |
| `+` `=` | `0x496570` | game speed +1, clamped to 20, refused for watchers and while the F11 mode is up; applied via `0x490df0(speed,1)` |
| `-` `_` | `0x496512` | game speed -1, clamped to 1, same guards |
| `,` | `0x496081` | `0x41bf10(1)` -- previous build-menu page for the selected builder, "nextbuildmenu" sound |
| `.` | `0x49608d` | `0x41bde0(1)` -- next build-menu page |
| `1`-`9` | `0x495fb9` | build page or squad select, swapped by Alt and the `SwitchAlt` registry value -- own section below |
| Ctrl+`1`-`9` | `0x495f9d` | `0x48d920(n)`: assign squad n -- every selected unit gets `[unit+0xac] = n`, every unselected member of squad n is removed. "CreateSquad" sound |
| `T` (uppercase) | `0x4964f1` | `0x41c2e0(1)` -- track selected unit |
| `t` | `0x4964e6` | `0x41c2e0(0)` -- track selected unit (the flag's effect inside `0x48c190` not decoded further) |
| `h` | `0x4964ae` | multiplayer only: open `SHARE.GUI` -- resource sharing (`0x4936f0`) |
| `n` | `0x4964d2` | `0x48d4d0` -- "Scroll to the next unit off screen" (HELP.TDF line 20) |
| `\` | `0x49637a` | registry-gated (developer section below); `0x417b50(0,-1)` -- plays something from a buffer at `0x511bd0`, not decoded further |
| F1 | `0x4960ea` | plain: `0x4942e0` -- unit info display for the selected (or pointed-at) unit. With Shift: latch the hovered unit into `0x391b3/0x391b7` -- a **debug text overlay** (`0x467e50` prints unit id, health, flag words as text) |
| F2 | `0x496165` | plain: open GAME OPTIONS (`0x460cc0`) if no menu is up. With Shift: latch hovered unit into `0x391b9/0x391bd` -- second debug overlay (`0x4685a0`) |
| F3 | `0x4961cd` | `0x464000` -- clear the "seen" bit on the message backlog and jump to the unit that last reported (HELP.TDF line 35) |
| F4 | `0x49639d` | toggle bit `0x80` of `[game+0x37f06]` -- the side-panel slide-away, own section below |
| F5-F8 | `0x49603c` | recall camera bookmark 1-4 (`0x41d3f0`): stop tracking, load saved position, snap |
| Ctrl+F5-F8 | `0x496020` | save camera bookmark 1-4 (`0x41d3b0`): current `[game+0x1431f]`/`[+0x14323]` into slot, mark valid |
| Ctrl+F10 | `0x4961d7` | registry-gated: movie-frame recording -- scans `%s\MOVIE*`, starts writing `MOVIE%03i` "FRAM" frames at the "Movie Output Rate" registry rate |
| F11 | `0x4962f2` | registry-gated: toggle bit 1 of `[game+0x3923b]` -- a debug panel with its own sub-keymap (developer section) |
| F12 | `0x4963c4` | `0x463c80` -- zero the chat-message indices at `[game+0x2a3e]`/`[+0x2a40]`: "Clear all chat messages" |
| Pause | `0x496099` | toggle `[game+0x38a51]&1` and broadcast (message lead byte `0x19`) |
| Ctrl+A | `0x4963ce` | `0x48bd50` -- select **all** own units, the whole array, alive and owned, everywhere on the map |
| Ctrl+C | `0x4963fe` | select category `CTRL_C` (the Commander), then `0x41c310`: set the tracked-unit id to it -- select and center |
| Ctrl+D | `0x49641d` | self-destruct: for each selected unit look up its `SELFDESTRUCT` weapon; units without one get the generic path `0x48cf30` |
| Ctrl+S | `0x4964dc` | `0x48c030` -- clear selection, then select every unit in the on-screen list `[game+0x1435f]` |
| Ctrl+Z | `0x496413` | `0x48be00` -- select all units of the same type(s) as the selection |
| Ctrl+B..Y (rest) | `0x4963d8` | the generic category select, next section |

The default case at `0x4965ce` does nothing except, when the F11 debug mode is
active, forward the key to the sub-handler `0x4956c0`.

---

## 74. Ctrl+letter is data-driven: `CTRL_%c` and the FBI Category field

Every Ctrl+letter that is not special-cased (A, C, D, S, Z are; C only half
-- it goes through the same lookup first) lands here:

```
  4963d8: lea    eax,[ebp-0x69]             ; key code back to 'A'..'Z'
  4963db: lea    ecx,[esp+0x10]
  4963df: push   eax
  4963e0: push   0x5094f0                   ; "CTRL_%c"
  4963e5: push   ecx
  4963e6: call   0x4e42b0                   ; sprintf
  4963eb: add    esp,0xc
  4963ee: lea    edx,[esp+0x10]
  4963f2: push   esi                        ; Shift held -> add to selection
  4963f3: push   edx
  4963f4: call   0x48bf30                   ; select all units in category
```

`0x48bf30` selects every own unit whose FBI `Category` line contains that
token. The bindings are therefore **entirely data-driven** -- there is no
hard-coded "Ctrl+B selects builders" anywhere in the exe. The shipped 3.1
unit set defines seven tokens:

| Token | Units carrying it | Meaning (readme.txt / HELP.TDF) |
|---|---|---|
| `CTRL_W` | 68 | all mobile units with weapons except the Commander |
| `CTRL_V` | 61 | all aircraft (VTOL) |
| `CTRL_F` | 48 | all factories (plants) |
| `CTRL_B` | 42 | all construction units |
| `CTRL_R` | 17 | radar, jammers, sonar and their jammers |
| `CTRL_P` | 13 | all aircraft with weapons |
| `CTRL_C` | 6 | the Commanders |

So out of the box: Ctrl+B, Ctrl+F, Ctrl+P, Ctrl+R, Ctrl+V, Ctrl+W select
those groups (Shift adds to the current selection instead of replacing);
Ctrl+C selects and centers; every other Ctrl+letter looks up a category that
no shipped unit has and selects nothing. A mod can invent `CTRL_X` and the
key springs to life.

---

## 75. The digits, Alt, and the `SwitchAlt` registry value

The digit case reads an option bit and polls Alt (`0xFB`):

```
  495fb9: mov    edx,DWORD PTR ds:0x511de8
  495fbf: xor    ecx,ecx
  495fc1: push   0xfb                       ; Alt
  495fc6: mov    cl,BYTE PTR [edx+0x37f07]  ; bit 8 of the display word
  495fcc: test   cl,0x1
  495fcf: je     0x495fef
  ...     ; bit set:   Alt -> build page,  plain -> select squad
  ...     ; bit clear: Alt -> select squad, plain -> build page
```

Default (bit clear): **`1`-`9` switch the selected builder's build-menu page**
("Select the menu for the current unit", HELP.TDF line 32) and **Alt+`1`-`9`
select squad 1-9**. The bit is the registry value `SwitchAlt` under
`Total Annihilation` (loaded at `0x43020a`), which swaps the two -- the
interface preference for group selection on bare digits. Squad select passes
the Shift state into `0x48d9a0`, so Alt+Shift+digit adds the squad to the
current selection. There is no squad 0: `'0'+0x94 = 0xC4` sits between Ctrl+Z
and Ctrl+1 in the table and is unbound, and plain `0` only reaches the
build-page path as an out-of-range page.

Squad membership lives on the unit (`[unit+0xac]`), is exclusive (assigning a
unit to squad 3 removes it from squad 1), and readme.txt section 5 adds the
factory rule: a factory assigned to a squad stamps that squad onto everything
it builds.

---

## 76. F4, Space, and the sliding panel

The display-options word `[game+0x37f06]` is persisted bit by bit in the
registry: bit 0 `damagebars`, bit 1 `Anti-Alias`, bit 2 `Shadows`, bit 3
`VehicleShadows`, bit 4 `FeatureShadows`, bit 5 `Shading`, bit 6
`DitheredFog`, bit 8 `SwitchAlt`. Bit 7 is the odd one out -- no registry
name, toggled only by F4, and read in exactly one place, the panel-slide
updater at `0x4948e0`:

```
  494935: test   BYTE PTR [eax+0x37f06],0x80
  49493c: jne    0x4949e5                   ; bit set: slide pos toward 0x7d
  494942: push   0x20                       ; bit clear: is Space down?
  494944: call   0x4c1b80
```

`0x51f2d8` is a slide position animated between 0 and `0x7d` (125 px, the
side panel width), with "Panel" and "Options" UI sounds at the endpoints.
With the F4 bit set the position runs to `0x7d`; with it clear it runs to 0,
except while Space is held (and the cursor is not on the panel's own gadget),
which drives it to `0x7d` again. Reading: **F4 latches the side panel out of
the way; holding Space slides it away temporarily** to see the map under it
(inference on which endpoint is "hidden" -- the arithmetic and sounds are as
stated, the direction is inferred from the Space semantics). A second updater
at `0x4689c0` runs the same Space logic for another sliding element.

RWE has no equivalent; its Space is the UI's "press the focused button" key
(`UiStagedButton.cpp:198`).

---

## 77. The second layer: screenshots, movies, and the gated debug keys

Ctrl+F9 (`0xD6`) is deliberately absent from the main dispatch -- it is
handled a layer up, in the per-frame routine at `0x4998e0` that also clears
the menu-pause bit:

```
  499915: cmp    edi,0xd6                   ; Ctrl+F9
  49991b: jne    0x49997d
  49991d: call   0x4c1ab0                   ; consume the key
  ...
  499945: push   0x5024fc                   ; "%s\screenshots"
  ...
  499961: push   0x50966c                   ; "SHOT"
  499967: call   0x4cb170                   ; write SHOTnnnn.pcx
```

`0x4cb170` scans the directory for the highest existing number and writes the
next `%s%s%s%04i.pcx` -- so **Ctrl+F9 = screenshot to
`screenshots\SHOTnnnn.pcx`** (HELP.TDF line 38), no gating.

> **Ported, 2026-09-11** (`1b16930e`). `SceneManager` catches Ctrl+F9 above
> every scene, consumes it, and writes `screenshots/SHOTnnnn.pcx` under the
> local data directory, one past the highest number already there. Two
> departures, both in §88: the file is 24-bit where the original's was 8-bit,
> and the picture is taken before the cursor is drawn. Where the original's
> numbering starts is not decoded; RWE's first is `SHOT0000`.

The registry value `Games` under `Total Annihilation` (read at `0x430e43`;
`== 1` sets bit 1 of `[game+0x37f2f]`) gates the developer keys:

- **Ctrl+F10**: movie recording -- each press starts numbering `MOVIE%03i`
  frame dumps, written from the frame pump at `0x4969f7` at the
  "Movie Output Rate" registry rate into "Image Output Directory".
- **F11**: toggles a debug mode (bit 1 of `[game+0x3923b]`) that opens a
  panel, freezes the speed keys, and routes every key through a second
  33-case jump table at `0x4956c0` (targets `0x495810`) -- pokes the hovered
  unit's state, five-press counters, and similar. Not enumerated further;
  it is not a player surface.
- **`\`**: `0x417b50(0,-1)`, plays something from a runtime buffer -- not
  decoded further.
- **Shift+F1 / Shift+F2**: latch the hovered unit for the two debug text
  overlays (`0x467e50`, `0x4685a0`) that print its internals.

---

## 78. The order keys are GUI data: `quickkey`

`a` attack, `m` move, `s` stop and friends are nowhere in the exe's dispatch.
They are `quickkey=` fields on the order-panel gadgets -- the ASCII code of
the key -- and the GUI layer presses the button when that character arrives.
The shipped bindings, from all 149 `.GUI` files:

**ARMGEN.GUI / CORGEN.GUI** (the shared ORDERS panel) and the per-unit
panels agree on one keymap:

| Key | Gadget | Order |
|---|---|---|
| `o` | ORDERS | switch to the Orders menu |
| `b` | BUILD | switch to the Build menu |
| `m` / `M` | MOVE | move |
| `s` / `S` | STOP | stop |
| `p` / `P` | PATROL | patrol |
| `a` / `A` | ATTACK | attack |
| `g` | DEFEND | guard |
| `f` | FIREORD | cycle fire orders (hold fire / return fire / fire at will) |
| `v` | MOVEORD | cycle move orders (hold pos / maneuver / roam) |
| `x` | ONOFF | activate / deactivate |
| `c` | CAPTURE | capture |
| `e` | RECLAIM | reclaim |
| `r` | REPAIR | repair |
| `l` | LOAD | load |
| `u` | UNLOAD | unload |
| `k` | CLOAK | cloak toggle |
| `d` | BLAST | **the D-gun** ("Use the Disintegrator Gun", HELP.TDF line 15) |

The per-unit panels store the movement four as uppercase (65/77/80/83) and
the rest lowercase; the engine treats them alike (HELP.TDF documents them as
plain letters). Shipped-data quirks: `ARMAAP1.GUI` alone gives DEFEND
`quickkey=113` (`q`) -- almost certainly a typo for `g`; the main panel's
SHARE button carries `S`, colliding with STOP. NEXT/PREV build-page arrows
carry `quickkey=0` -- paging is the exe's `,`/`.` binding, not a quickkey.

RWE's quickkey plumbing (`UiFactory::convertQuickKeyToSdlk`, lowercasing
65-132 into SDL keycodes; `UiStagedButton::keyDown` matching) faithfully
reproduces this layer -- **but a quickkey is only as alive as its activation
handler**, and RWE's `GameScene` message chain handles ATTACK, MOVE, DEFEND,
STOP, RECLAIM, REPAIR, PATROL, CAPTURE, LOAD, UNLOAD, FIREORD, ONOFF, CLOAK,
NEXT, PREV, BUILD, ORDERS -- and not `BLAST`, not `MOVEORD`, not `SHARE`. So
in RWE today **`d` (D-gun) and `v` (move orders) are dead keys** even though
the buttons render (OrderButtons.cpp knows both for visibility only).

---

## 79. What RWE binds today

From `src/rwe/game/GameScene.cpp` `onKeyDown`/`onKeyUp` (branch `revival`):

- Tab / F2 toggle the game menu; Esc closes it, else cancels the cursor mode,
  else deselects (no STOP-gadget press, but equivalent in effect).
- Arrows scroll; Shift/Ctrl tracked; `+`/`=`/`-` (and keypad) speed;
  Pause pauses -- all matching.
- F10 debug window, F1 help overlay, backquote (scancode) health bars,
  `t` track, Ctrl+C select-and-track commander.
- Ctrl+A select all **on screen**, Ctrl+S **stop**, Ctrl+D self-destruct,
  Ctrl+Z **attack-ground mode**, Ctrl+W **guard mode**, Ctrl+F **attack
  mode**, Ctrl+P **move mode**.
- Digits: Ctrl+digit bind group, Shift+digit add selection to group, digit
  recall, `0` = a tenth group, Ctrl+Shift+digit = add.
- `SceneManager` binds F11 to the ImGui debug window globally.

---

## 80. The gap table

Key -> what the original does -> RWE status.

| Key / combo | Original action | RWE |
|---|---|---|
| Esc | close menu / press STOP gadget / deselect all + close floating panels | **works** (cancels cursor mode rather than pressing STOP; same effect) |
| Tab | MP: TABMENU bar; SP: GAME OPTIONS | **different** -- always the game menu; no TABMENU bar |
| Enter | message bar (TALK.GUI / TALK2.GUI) | **missing** -- no message/chat bar at all |
| `+` `=` / `-` `_` | speed 1..20 | **works** |
| Pause | pause toggle, broadcast | **works** |
| Arrows | scroll map | **works** |
| backquote `~` `!` `#` `*` | toggle damage bars | **works** (backquote only; the alias keys absent -- harmless) |
| `,` / `.` | previous / next build-menu page | **missing** (PREV/NEXT handlers exist, keys not wired) |
| `1`-`9` | build-menu page 1-9 (default; `SwitchAlt` swaps with Alt) | **different** -- control-group recall |
| Alt+`1`-`9` | select squad (Shift adds to selection) | **missing** -- Alt unused |
| Ctrl+`1`-`9` | assign squad (exclusive membership; factories stamp their products) | **works** in spirit (RWE groups are sets; no exclusivity, no factory inheritance, extra group 0) |
| `t` / `T` | track selected unit | **works** |
| `n` | scroll to next unit off screen | **missing** |
| `h` | MP: SHARE.GUI resource sharing | **missing** |
| F1 | unit info display for selected unit | **different** -- RWE help overlay |
| F2 | GAME OPTIONS | **works** |
| F3 | jump to the unit that last reported | **missing** |
| F4 | latch the side panel away | **missing** |
| Space (held) | slide the side panel away temporarily | **different** -- activates focused UI button |
| F5-F8 | recall camera bookmark 1-4 | **missing** |
| Ctrl+F5-F8 | save camera bookmark 1-4 | **missing** |
| Ctrl+F9 | screenshot `screenshots\SHOTnnnn.pcx` | **missing** |
| F12 | clear all chat messages | **missing** (nothing to clear yet) |
| Ctrl+A | select **all** units | **different** -- selects on-screen only (the original's Ctrl+S) |
| Ctrl+S | select all units **on screen** | **different** -- stops selected units (the original's `s` quickkey) |
| Ctrl+D | self-destruct selection | **works** |
| Ctrl+C | select Commander and center | **works** |
| Ctrl+Z | select all of same type | **different** -- attack-ground cursor mode |
| Ctrl+W | select armed mobiles (`CTRL_W`) | **different** -- guard cursor mode |
| Ctrl+F | select factories (`CTRL_F`) | **different** -- attack cursor mode |
| Ctrl+P | select armed aircraft (`CTRL_P`) | **different** -- move cursor mode |
| Ctrl+B | select builders (`CTRL_B`) | **missing** |
| Ctrl+R | select radar/jammer/sonar (`CTRL_R`) | **missing** |
| Ctrl+V | select all aircraft (`CTRL_V`) | **missing** |
| Ctrl+other letters | category lookup, no-op with stock data | **missing** (moot until modded categories) |
| quickkeys `o b m s p a g c e r l u x k f` | panel buttons | **work** |
| quickkey `v` (MOVEORD) | cycle move orders | **dead key** -- no activation handler |
| quickkey `d` (BLAST) | the D-gun | **dead key** -- no activation handler |
| Shift (held) | queue orders; build-square + cloak-radius ghosts; x5 factory clicks | **works** for queueing and x5; hover ghosts out of scope here |
| Ins/Del/Home/End/PgUp/PgDn | nothing found in-game | n/a |
| Ctrl+F10, F11, `\`, Shift+F1/F2 | registry-gated developer keys | n/a (RWE's F10/F11 debug windows are its own equivalent) |

RWE-only extras, all benign: F10/F11 debug windows, group `0`,
Ctrl+Shift+digit, keypad speed keys.

---

## 81. Implementation list for RWE, in priority order

1. **Re-point the Ctrl+letter family at selection, where the original has
   it.** Ctrl+A = select all everywhere; Ctrl+S = select on screen; Ctrl+Z =
   same type; Ctrl+B/F/P/R/V/W = category token match against the FBI
   `Category` word list, generic `CTRL_%c` style, Shift adding to the
   selection. The current cursor-mode bindings on Ctrl+S/W/F/P/Z are
   inventions that shadow the original meanings; the orders they duplicate
   already have their true keys (`s`, `g`, `a`, `p` quickkeys). This is the
   loudest muscle-memory break in the list.
2. **Wire the two dead quickkeys**: `d` -> BLAST (D-gun -- an activation
   handler that enters a manual-fire attack mode for the dgun weapon) and
   `v` -> MOVEORD (cycle move orders). The buttons already render and gate
   correctly; only activation is missing.
3. **`,` and `.`** -> the existing PREV/NEXT build-page logic. Trivial.
4. **Digit semantics.** Default digits to build-menu page switching, move
   squad selection to Alt+digit, keep Ctrl+digit assignment, and offer the
   `SwitchAlt` preference to swap -- or consciously keep the modern scheme
   and record it in the "deliberately differs" section of TOTALA-EXE.md.
   If squads are kept TA-true: exclusive membership and the factory
   inheritance rule come with them.
5. **Camera bookmarks** Ctrl+F5..F8 / F5..F8 -- small, self-contained,
   pure scene state.
6. **`n`** next-unit-off-screen scroll -- small.
7. **F1 unit info** display (UNITINFO.GUI); move RWE's help overlay to an
   unused key if kept.
8. **The message bar**: Enter to open (TALK.GUI), F12 to clear, F3 to jump
   to the last reporting unit -- one work item, since F3/F12 only mean
   something once messages exist. MP chat and `h` sharing (SHARE.GUI) hang
   off the same UI.
9. **F4 / held-Space panel slide** -- cosmetic, needs the panel to be a
   sliding element first.
10. **Ctrl+F9 screenshot** to `screenshots\SHOTnnnn.pcx` -- nice-to-have.

Not worth porting: the `Games`-registry developer keys (Ctrl+F10 movie mode,
F11 debug panel, `\`, Shift+F1/F2 overlays) -- RWE's ImGui windows already
serve that purpose.

---
