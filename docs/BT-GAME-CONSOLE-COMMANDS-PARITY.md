# Console commands, debug scripts and terrain diagnostic overlays parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-SHARE-COMMANDS-PARITY.md`, `BT-INGAME-DIALOGS-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one chapter of the
reference, `game-console-commands` (83 annotated services from `0x416240` to `0x418CD0`: the callbacks of the
three command tables, the world-to-screen helpers, the selected-goal drawing and the terrain diagnostic overlay).
This is the finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

Two neighbours already hold part of it. `TOTALA-EXE-COMMANDS.md` section 124 decodes the line interpreter, the sharing
commands and `+Give` from the executable and lists the other registered words by name only;
`BT-GAME-SHARE-COMMANDS-PARITY.md` audits the twelve services of `game-share-commands` (`ShareMetal`,
`ShareEnergy`, `ShareMapping`, `ShareRadar`, `ShareAll`, `SetShareMetal`, `SetShareEnergy`, `ShowRanges`,
`Compression`, `BPS`, `SFX`) and is not repeated. `BT-INGAME-DIALOGS-PARITY.md` rule 3 audits the talk dialog that
hands a `+` line to the interpreter. This audit is about **the remaining commands and the three tables' shape**.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\game-console-commands.md`). Nothing else under that
  tree was opened: no source, no decompilation, and none of its source links were followed. Addresses are the ones
  the chapter names; rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; **BT-176** (chat recipients) and
  **BT-177** (a `+command` line also sent as chat) are the rows that touch the talk dialog and are not re-raised.
- **Evidence class.** A **static reading**; the chapter says no command was run. Weaker than `TOTALA-EXE*.md`.
  Disagreements are marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of `game/chat_command.*`, `game/GameScene_chat.cpp` (`sendChatMessage`,
  `runChatCommand`), `game/GameScene_debug.cpp` (the ImGui windows), `sim/ResourceSharing.*` and the grep results
  below, at the working tree of 2026-10-17. Nothing was built, run or changed; every line number is "about".
- **No executed observation of the original was used.**

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. The three tables, the masks and how a line reaches them
3. The line interpreter
4. Ordinary table: display and option commands
5. Ordinary table: players, resources, audio and diagnostics
6. The cheat table
7. The debug table: simulation, mission and files
8. `Include` and the default handler
9. Diagnostic drawing, contours and the map overlay (not applicable)
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| registration, masks, the talk-dialog entry | `TOTALA-EXE-COMMANDS.md` section 124 (mask 1, cheats mask 2, the rest "not specified"), `BT-INGAME-DIALOGS-PARITY.md` rule 3 | rule 2 |
| tokens, numbers, case | section 124 ("Entry"), `chat_command.h` | rule 3: MATCHES |
| ordinary commands (display and options) | section 124 lists the names only; `BT-GAME-SETTINGS-PARITY.md` holds the stores they write | rule 4 |
| `Give`, `NoMetal`, `NoEnergy`, `Logo`, `Selectable`, audio, `BigBrother`, `NetStats`, `Now` | section 124 (`Give`; "none touches the simulation" for the rest) | rule 5, with a **CONFLICT** |
| the ten cheats | section 124 (names) | rule 6 |
| the thirty debug commands | section 124 ("not specified here") | rule 7 |
| `Include`, the default handler | none | rule 8 |
| overlays and contours | none | rule 9 |
| `ShareMetal` ... `SFX` | `BT-GAME-SHARE-COMMANDS-PARITY.md` | not repeated |
| `Film`, `FilmSpeed` | `BT-GAME-MAIN-LOOP-PARITY.md` rule 5 (the movie recorder, "worth one clause", decision stands) | not repeated |

## 2. The three tables, the masks and how a line reaches them

**Reference.** `InitCommands` registers three tables and a **default handler under mask four**. The ordinary
table has **43 commands, mask one**; the cheat table **ten, mask two**; the debug table **thirty, mask four**. A
command runs only when its registration mask **intersects** the caller's; a known name whose mask misses can
fall to the default handler. The talk dialog strips leading spaces, recognises a leading plus, and starts with
mask one; it uses mask **seven** when UI-option bit one is set and **adds the cheat bit (two) for a
non-campaign game**; a dispatch result containing bit two sets the chat mode to "everyone". The keyboard handler
also repeats the last line with mask minus one while that UI-option bit is set. `Now` (ordinary table) sets
UI-option bit one **only when it has exactly six tokens and the five after the name are the words Film, Chris,
Include, Reload and Assert in that case-sensitive order**; any other call clears the bit. The chapter's point is
that "debug" in the table's name does not mean compiled out: it is gated by two runtime bits and one phrase.

**Register.** Section 124: ordinary commands carry mask 1, "the mask gains bit 2 (cheats) in non-campaign play
and further bits through a UI option"; the cheat table is named and "not specified here", the debug table likewise.

**RWE: DIVERGENCE (undocumented as such).** RWE has one table of one mask: `parseChatCommand`
(`chat_command.cpp` about 102) recognises `sharemetal`, `shareenergy`, `shareall`, `sharemapping`, `shareradar`,
`setsharemetal`, `setshareenergy` and `give`, and every other word is `Unknown` (`GameScene_chat.cpp` about 109).
There is no mask, no cheat table, no debug table, no default handler, no `Now` phrase and no repeat key.
`BT-INGAME-DIALOGS-PARITY.md` rule 3 records "not applicable"; **`TOTALA-EXE.md` section 88 does not list it**
and section 91 (decoded, not ported) does not either, so the next reader comparing against the executable sees a
mask-two table that does not exist and no reason. The reason is lockstep: a command that writes a player's
resources or a unit's flags on one machine is a desync unless it is a replicated command (rule 5).
**Two questions the chapter leaves open and the register shares:**

- **Is the cheat bit gated by the lobby's "Cheat Codes" option?** The chapter says non-campaign play; the lobby
  has a `CHEATING` button (`LOUNGE2.GUI`, whose caption is "Cheat Codes", linked, `BT-GUI-GUI-PARITY.md` rule 6)
  and the host record carries a cheating state (`BT-GAME-GAME-LOAD-PARITY.md`). If the bit is simply "not a
  campaign", every player of every skirmish or network game in the original can type `+ATM` and
  `+View`. **UNCLEAR**; a spec-room read of the talk dialog's mask selection settles it.
- **What does the original say for an unknown word?** The chapter says the default handler (mask four) handles
  a name that is not found; a mask-one caller never reaches it. RWE prints `Unknown command: <word>`. Whether the
  original prints anything is open; cosmetic.

## 3. The line interpreter

**Reference.** `ExecuteCommandLine` initialises an argument object, tokenises and dispatches with the supplied
mask. A null line repeats the saved buffer; a non-null one copies **at most 79 bytes into the saved buffer
without terminating it** (the tokeniser reads the original, so 79 is not the execution limit, but a later repeat
may read an unterminated buffer). The token store keeps token zero as the name and converts numbers with `atoi`
and `atof`.

**RWE: MATCHES.** `tokenise` (`chat_command.cpp` about 29) splits on white space, stops at `#` even mid-word,
keeps at most 20 tokens, has no quoting, matches the word case-insensitively and reads a missing integer as 0;
`chatAtoi` (about 69) is `atoi` without its undefined overflow (saturating, where the C library's value for an
overflowing digit string is unspecified). The 79-byte saved buffer and the repeat key have no counterpart; the
bar's own limit is 128 code points (`BT-INGAME-DIALOGS-PARITY.md` rule 3).

## 4. Ordinary table: display and option commands

**Reference.** Nineteen commands that write option bits and (some of) save the settings: `Shading` (visual bit
5, flush the unit bitmap cache, save), `AntiAlias` (bit 1, flush, save), `Shadow` (bit 2, flush, save), `Dither`
(bit 6, save, no flush), `SwitchAlt` (no argument: toggle bit 8 and save; with an argument assign it, **no
save**), `TShadow` (bit 3) and `FShadow` (bit 4), neither flushing nor saving, `LOSType` (map bit 2, recompute
sight, no save), `Light` (three integers, default 0, set the light vector and flush), `RCache` (flush),
`Gamma` (brightness **= argument times 0.1**, the raw integer stored, **no clamp**, save), `ScrollSpeed`
(narrowed to a byte, saved, **no minimum**), `IFace` (stored, saved, **no range check**), `ScreenChat` (exclusive-or
with one, saved), `Clock` (UI-option bit 6, saved), `NoShake` (UI-option bit 4, **not saved**), `ShootAll`
(UI-option bit 10), `Drop` (UI-option bit 0 set when the argument is zero, cleared otherwise) and `Sing` (a
separate global). `Contour` stores spacing and offset as float arguments times 256 (offset default 0.75), and
**zero spacing disables the overlay while negative spacing is accepted and can stop the contour loops from
ever terminating**. The flags live in three different words (visual, UI-option, map visibility) and
similar-looking bits do not share a cache or persistence effect.

**RWE: PARTLY PORTED as commands; the settings they write are also the options screens' business.** The words
with an RWE effect are registered and listed in `TOTALA-EXE.md` section 88: `Shading`, `AntiAlias`, `Shadow`,
`TShadow`, `Gamma`, `ScrollSpeed`, `IFace`, `NoShake`, `ScreenChat`, `Clock`, `SwitchAlt` and the cheat/stock
words below. `SwitchAlt` is the one ordinary-table word with a saved RWE preference (`rwe.cfg` key
`switch-alt`); it now decides the digit arrangement (`KEYBOARD.md` section 75). The rest of the table --
`Dither`, `FShadow`, `LOSType`, `Light`, `RCache`, `Contour`, `Drop`, `Selectable` and the rest -- is still
`Unknown command`, because RWE has no effect for it to drive. The differences a port must not copy:

- **Gamma and scroll speed through the console are unclamped**, the options pages clamp (gamma 50 to 133, scroll
  speed 25 to 200). A command that wrote straight through would be a way to a gamma the screen cannot return from.
- **Only some of these save the settings.** `Dither`, `Shading`, `AntiAlias`, `Shadow`, `Gamma`, `ScrollSpeed`,
  `IFace`, `ScreenChat`, `Clock` and `SwitchAlt` (without an argument) do; `TShadow`, `FShadow`, `LOSType`, `NoShake`
  and `SwitchAlt` with an argument do not. This is the "save points" row of `BT-GAME-SETTINGS-PARITY.md`
  (the original also saves from console commands).
- **`Selectable` is not a selection**: it adds the selectable flag (`0x20`) to every unit that already carries
  `0x10000000`. Section 124 says it "sets selectable on every live unit"; the chapter's reading is narrower.
  **Minor CONFLICT of detail** with section 124 (which units, which flag).
- **`Contour` with a negative spacing is a hang in the original**; RWE has no contour renderer (rule 9).

## 5. Ordinary table: players, resources, audio and diagnostics

**Reference.** Many handlers validate a player by narrowing the integer to an unsigned byte and requiring a
byte below ten, an active player of type one, two or three and an index not equal to ten; some then **reuse the
original integer as an array index**, so a wrapped value passes the check and indexes wrongly.

- **`Give`** takes target player (argument one, a **slot index**), amount (two, integer) and a word (three),
  validates the player, then calls the transfer with the local player as source and the final flag one. The
  chapter notes the helper chosen is **reversed from the word**: "metal" calls `TransferEnergy` and "energy"
  calls `TransferMetal`, and warns not to silently reverse the documentation. No amount clamp, no success report.
- **`NoMetal` and `NoEnergy`** directly **assign** a resource from integer argument **two**. With only the
  command token they target the local player and the missing argument is **zero**; with any extra token the
  player comes from argument one, so `+NoMetal 3` selects player 3 and **assigns zero** (the lone number is
  the player, not the amount), and `+NoMetal 3 5000` sets player 3's metal to 5000. No transfer helper, no capacity
  or sign check. Mask one.
- **`Logo`** requires a logo number below the first count in the logo bank and a validated player (default
  zero), writes the colour/logo field and flushes the unit cache; failure prints "Invalid logo setting."
- **Audio:** `CDPlay` (argument one, default 0, flag one), `CDStop`, `Sound3D` (query and toggle, saved either
  way), `MusicMode` (argument straight to `SetTrackCategory`).
- **`BigBrother`** toggles auto-follow (disabling clears the follow state, enabling sets a tick counter to one,
  no target chosen). **`NetStats`** stores the current tick, clears the aggregate counts and bytes and per-kind
  rows one to 44 (not row zero) and calls a no-op.
- **`Now`** (rule 2).

**Register.** Section 124's list of "other registered commands" says: "(mask 1, **effects are options and display
and none touches the simulation**)" and names `Selectable`, `Logo`, `NoMetal`, `NoEnergy`, `BigBrother` among them.

**RWE: MISSING, and a CONFLICT with the register's classification.**

- **CONFLICT: do the mask-one commands touch the simulation?** The register: none does. The chapter: `NoMetal`
  and `NoEnergy` **write a player's stockpile outright** (including a different player's), `Selectable` writes a
  per-unit flag on every unit, `Logo` writes a player field, `Give` moves resources. In a lockstep engine this
  is the difference between "port as a local option" and "port as a replicated command": the stockpiles and the
  unit flags are hashed. A port that took section 124 at its word and put `NoMetal` beside `Gamma` would
  desync the first time it was typed. Neither side is chosen. Kind: register. A spec-room read of the two
  handlers settles whether they are guarded by the net-game flag as the sharing commands are.
- **`Give`: partly built, with three differences.** `runChatCommand` (`GameScene_chat.cpp` about 195) builds it
  as a replicated `PlayerShareCommand` (so it is lockstep-safe and a deliberate difference from the original,
  section 124 "What RWE does"). **The player argument is read differently**: RWE takes the name a player joined with
  or `Player n` counted **from one** (about 198 to 220), the original takes the **slot index from zero**, so
  `+Give 1 100 metal` gives to the **second** player in the original and the **first** in RWE, and `+Give 0 ...`
  works in the original and answers "Give: no such player." in RWE. RWE also refuses the local player as target; the
  original's check does not exclude it. And the **word** (metal or energy) follows the register: section 124 says
  the routines are misnamed in the source, so "metal" does move metal; the chapter's statement about the helper
  names is the same fact seen from the other side, and is **UNCLEAR** only until a spec-room read of the call
  confirms which accounts the word selects (rule 10). A negative amount is refused in RWE and unclamped in the
  original (section 124).
- **`NoMetal`, `NoEnergy`, `Selectable`, `Logo`: not built.** RWE's stockpiles have no override command; the
  debug spawner and the skirmish setup cover what a tester wants (rule 7).
- **`Logo`, `CDPlay`, `CDStop`, `Sound3D`, `MusicMode`: not built.** RWE's colour choice is in the lobby and
  the music has its own panel (`BT-FRONTEND-OPTIONS-PARITY.md`).
- **`BigBrother`, `NetStats`: not built.** The network overlay (F9) is RWE's own and a pure observer
  (`CLAUDE.md`); auto-follow has no counterpart (the camera follow in the original's tick order is `BT-1`).
- **The player-validation pattern** (narrowed byte, reused original integer) is a defect in the original and
  does not apply: RWE's `Give` range-checks the integer it uses.

## 6. The cheat table

**Reference.** Ten commands, mask two: `Radar` (UI-option bit 9), `ATM` (**+1000 energy and +1000 metal to the
viewed player, which can differ from the local player**, no storage cap), `View` (after the player check, writes
the original argument into the **viewed-player byte only**, so control and view can diverge), `LOS` (toggle map
visibility bit 1, save, recompute), `Mapping` (toggle map bit 0, save, recompute with one), `DoubleShot` (bit 7)
and `HalfShot` (bit 8) (flags only: the name does not mean the callback duplicates projectiles), `NowISee`
(clear map bits 0 and 1, recompute, no save), `Meteor` (with an argument enable or disable periodic meteors by
truth value, with none start a shower now) and `MakePoster`. `MakePoster` defaults to 3200 by 2400, `all` selects
the map's pixel size, each dimension is raised to at least the visible tile span times sixteen and lowered to the
map's, it centres on the view, clamps, creates the screenshots directory and writes a `BIGSHOT` bitmap, then
resets the pacing timestamp so the capture delay is not charged as elapsed frame time.

**RWE: MISSING, by design (and by lockstep).**

- **`ATM`, `View`, `LOS`, `Mapping`, `NowISee`, `Radar`: not built.** RWE has no human cheat option: the "cheat"
  words in the tree are the computer player's own tuning knobs (`cheatModeOmniscient`, `resourceCheatMultiplier`,
  `ai/AiTuningProfile.cpp`). A human cheat would have to be a replicated command like `PlayerShareCommand`, and
  `ATM`'s "viewed player" has no meaning when control and view cannot diverge in RWE. The community patch
  `+atm 10000` (`TA-PATCHES.md`) is for the original's ATM amount.
- **`DoubleShot`, `HalfShot`: not built.** They are flags consumed by the weapon code (`TOTALA-EXE-WEAPONS.md`
  names the UI-option bits); whether the register's weapon findings include their effect on reload was not
  traced here.
- **`Meteor`: nothing to drive.** RWE does not implement the meteor shower (`BT-WEAPONS-WEAPON-TYPES-PARITY.md`
  rule on the `meteor` flag; the OTA reader parses the fields only).
- **`MakePoster`: not applicable** (`BT-INGAME-KEYS-PARITY.md`: "the BMP poster is a console command"). The
  pacing-timestamp reset is the same class of side effect as the movie recorder's
  (`BT-GAME-MAIN-LOOP-PARITY.md` rule 5): nothing RWE depends on.

## 7. The debug table: simulation, mission and files

**Reference.** Thirty commands, mask four. Player and ownership: `AI` (toggle a player between human-like type one
and AI type two by a validated index, default the local player), `Control` (writes **both** the local and the
viewed player). Orders: `Assign` (resolves a mission type from argument one's **text** and then also uses
arguments one and two **as integers** for the final parameters, so it is not an "order, x, z" grammar). Kill and
victory: `Kill` (**one token kills all units of all players**, otherwise `KillPlayerUnits` with argument one
narrowed to a byte; then deactivates mission conditions either way), `IWin` (kills player 0 if the local side is
one, else player 1, then sets end-game bits 4, 5 and 2), `ILose` (kills the player indexed by the local side and
sets bits 6 and 2): both assume sides of 0 or 1. Map and features: `BurnAll` (remove all features), `BurnOne`
(remove the feature at the cursor cell), `Feature` (place a feature of the named type at the cell, owner
sentinel ten), `SeaLevel` (narrow to the global byte, **no cache rebuild**), `Edge` (map pixel size minus the
arguments, defaults 32 and 128, unchecked), `Search` (writes the pathfinder value only when argument one is
nonzero, so it cannot clear it; with exactly three tokens also writes argument two times 65536 as fixed point),
`Move` (adds integer arguments shifted left twenty to the debug position: **sixteen whole world units per
step**). Toggles: `SelBoxes`, `TreeDeath`, `Profile`. Files and reloads: `MemDump` (creates an empty
`memdump.txt`), `Mem`, `Assert`, `DPrint`, `ZBuffer` (**empty bodies**), `Film` (copies the argument unbounded into
the game path, strips a trailing slash, **indexes before an empty string**), `FilmSpeed` (positive only), `Save`
(a 256-byte stack name, `.sav`, description "Generic Game Description", identifier `0x29A`, no outcome report),
`Reload` (**kills every unit of the named type** and reloads its definition), `ReloadAIProfiles`, `PrintWeights`
(three tokens, writes an AI diagnostic file), `Senderror` (0 to 100 else 0, sets the simulated drop percent),
`Include` (rule 8) and `DebugBreak` (below).

**RWE: replaced by tools, not commands.**

- **Spawn, kill by owner:** the ImGui "Unit spawner" and "Kill every unit of the chosen owner"
  (`GameScene_debug.cpp` about 392 and 656) and `RWE_DEBUG_SPAWN` cover `Kill` (by owner) and the default
  handler's unit creation (rule 8); they are local tools for the host or a test, not typed commands.
- **`IWin`, `ILose`, `Kill` (all): not built.** Victory is the rules of `BT-GAME-VICTORY-PARITY.md`; a self-destruct
  or the debug kill reach it. The side-derived 0 or 1 assumption is the original's.
- **Feature and map editing (`BurnAll`, `BurnOne`, `Feature`, `SeaLevel`, `Edge`): not built.** `SeaLevel` would
  invalidate the pathing and terrain caches in RWE, which the original's does not.
- **`Search`, `Move`, `AI`, `Control`, `Assign`, `Reload`, `ReloadAIProfiles`, `PrintWeights`, `Senderror`:** the
  analogues are RWE's own: `--ai-engine`, the arena (`ai_arena`), `RWE_SIM_LAG` and `RWE_DESYNC_AT` (`Senderror`'s
  counterfeit), the state-dump variables. None is a replacement for the original's behaviour and none needs to be.
- **`Film`, `FilmSpeed`, `Save`:** `BT-GAME-MAIN-LOOP-PARITY.md` rule 5 and `BT-GAME-SAVEGAME-PARITY.md`.
- **Empty bodies:** `Mem`, `Assert`, `DPrint`, `ZBuffer` do nothing, and `MemDump` writes an empty file. Their
  names do not imply a hidden diagnostic or a depth-buffer renderer in this executable. Worth one clause in
  section 124's "not specified here".

## 8. `Include` and the default handler

**Reference.** `Include` builds `debugdat` plus **token zero** plus `.txt` in a 60-byte stack buffer, so it
looks for a file named after the *command* ("include.txt"), not argument one; on an open file it snapshots the
position, loads the bytes, executes the block with the current arguments as substitutions and **mask all bits**,
and restores the position. The default handler visits unit definitions one to count minus one, matches token zero
as a **wildcard pattern against each internal unit name**, and for every match creates a unit for owner argument
one (default zero), advancing the position by the model's maximum X plus `0x200000` and wrapping to a new row at
the map edge; it never checks the creation. **Only when nothing matched** does it try `debugdat/<token>.txt` with
the same unbounded formatting and execution. A script can recurse without bound, and restoring the position does
not undo created units, resources or flags.

**RWE: not applicable.** There is no script inclusion (a command file executed with every permission is the
opposite of the sandbox rule in `CLAUDE.md`), and unit creation by name is the debug spawner's.

## 9. Diagnostic drawing, contours and the map overlay (not applicable)

**Reference.** The world-to-screen helpers use the sampled ground height at the point, not its Y component
(`x - scrollX + 128`, `z - scrollY - half height + 32`); line helpers take 16.16 positions plus whole deltas;
`AiSearchGoal::DrawOnSurface` outlines the owner's tile region and draws an **open polyline** (no closing
segment, no count clamp, no owner guard) in colour slot 9 or 12; `DrawCellContours` splits each quad into four
triangles around a rounded centre; `DrawTriangleContours` draws levels from the highest multiple of the spacing
plus offset downward, indexing a colour table by `(height - sea level + 256) / 16`. `DrawMapDebugOverlay` has four
modes (the selected unit's movement-class cells and the pathing arrows; tile edges plus occupancy; edges plus the
metal byte; edges plus a square where the viewed player's fog cell is set) and shows contours on top. It reads
negative scroll unclamped, breaks on the first corner above the screen height, uses `rand` for the "G" glyph
colour, and can reuse an uninitialised arrow colour.

**RWE: not applicable.** RWE's diagnostics are the F10 ImGui panels, the path profile (`RWE_PATH_PROFILE`) and the
tooling in `CLAUDE.md`; none reproduces the overlay. If a path-cell overlay is ever added, the useful facts are
the cell-state encoding (two bits per cell per movement class, states below three drawn) and the direction-arrow
flag test, both from `BT-AI-PATHFIND-PARITY.md` and `BT-UNITS-MOVEMENT-CLASS-PARITY.md`, not from this chapter.

---

## 10. Gaps to fix

Ranked by what an implementer or a tester would notice. None changes the simulation as it stands.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **CONFLICT: `NoMetal`, `NoEnergy`, `Selectable`, `Logo` and `Give` are mask-one commands that write simulation state** (stockpiles, per-unit flags), where section 124 calls every non-sharing registered command display or options only. A port following section 124 would desync. | CONFLICT (register) | spec-room read of the handlers; then correct section 124 and decide which, if any, become replicated commands |
| 2 | **`+Give` names its target differently**: RWE counts `Player n` from one (and accepts a joined name), the original takes a slot index from zero; RWE refuses the local player. | DIFFERS | decision (RWE's way is friendlier); document in section 124's "What RWE does" |
| 3 | **Cheat and debug tables, the `Now` phrase and the masks are an undocumented divergence**: not in section 88 or 91. | DIVERGENCE (undocumented) | add a line to section 88: lockstep forbids local state commands; the debug panel replaces the debug table |
| 4 | **Is the cheat mask gated by the lobby's "Cheat Codes" option, or just "not a campaign"?** | UNCLEAR | spec-room read of the talk dialog's mask selection |
| 5 | **Which account does `+Give <p> <n> metal` move?** The chapter's helper names are crossed; section 124 says the source names are crossed too. | UNCLEAR | spec-room read of the call and the two accounts |
| 6 | **Section 124 line "Selectable sets selectable on every live unit"** is narrower in the chapter (flag `0x20` on units carrying `0x10000000`). | CONFLICT of detail | spec-room read |
| 7 | **The empty bodies** (`Mem`, `Assert`, `DPrint`, `ZBuffer`, `MemDump` writes nothing) should be named so nobody ports a hidden diagnostic. | documentation | add to section 124 |
| 8 | **An unknown word**: RWE prints `Unknown command`; what the original prints is open. | UNCLEAR, cosmetic | none |
| 9 | **The console save points** (which commands save the settings) are the original's and differ command to command. | DIFFERS (no console) | `BT-GAME-SETTINGS-PARITY.md` rule on save points |
| 10 | **The console's option commands bypass the clamps the options screens apply** (gamma, scroll speed): do not copy that. | design note | none |

No source file was changed.
