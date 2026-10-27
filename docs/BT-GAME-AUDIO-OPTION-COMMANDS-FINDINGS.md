# Audio and option commands: exact behaviour for #908 and #907

Spec-room finding, 2026-10-27. It settles the ordinary-table commands that
#908 (audio) and #907 (display and option setters) are about, against the
byte-tactics decompilation, and audits RWE's current audio and settings
surface read-only. It allocates no section in the main register; it is the
finding an implementation cites for these words.

**Issues:** #908 primary (`CDPlay`, `CDStop`, `Sound3D`, `MusicMode`, `Sing`),
#907 secondary (`Clock`, `Gamma`, `ScrollSpeed`, `IFace`, `ScreenChat`,
`ShootAll`, `SwitchAlt`, `Dither`, `TShadow`, `FShadow`, `Shadow`, `Shading`,
`AntiAlias`, `Light`, `RCache`, `Contour`, `LOSType`).

**Evidence class:** a **static reading** of the decompilation plus a
read-only source audit of RWE. Nothing was executed: no original game, no
demo oracle, no RWE run and no RWE test. Where a fact could only be settled
by running something, it is in "Remaining uncertainty" and labelled as such.

## Evidence and limits

- **Reference revision:** `e89721c46c3fa06e59558419b54fe26061fafb73` in
  `D:\bytetactics` (read-only, clean worktree, dated 2026-10-19).
- **Reference binary:** `D:\bytetactics\orig\TotalA.exe`, 1,178,624 bytes,
  MD5 `8E74A1DFFA1F5988624C52048F5B20CD`, SHA-256
  `3B9C0FADABF3DC67ED5F05A70F1E1505A0C65DEADD1A3C930ADFE30E2A84995E`,
  the same executable the other `BT-*` findings use (the 3.1-era GOG build).
- **Read:** the decompiled bodies behind the addresses below, the settings
  load and save, the music object and its CD methods, the sound-flag and
  palette code paths that consume the bits, and the RWE tree at
  `D:\RWE-spec-room-908` on branch `908-audio-option-findings`.
- **Not run:** the original executable, any shipped-data scenario, the demo
  oracles, the RWE build and the RWE tests. Every "does" below is what the
  code says, not what a session showed.
- **One data check was run**, and it is not a behaviour check: the retail
  archive `D:\RWE-Data\totala1.hpi` was listed read-only with the project's
  own `tools/hpi.py`. It contains `sounds/SING.WAV` (33,673 bytes) and
  `sounds/HONK.WAV` (8,145 bytes), so the `Sing` easter egg has its assets in
  the shipped data. The GOG album in `D:\RWE-Data\music` was also listed:
  `0.mp3` through `17.mp3`, 18 files, with `0.mp3` and `1.mp3` the same size
  as `9.mp3` and `10.mp3` (the duplicates `TOTALA-EXE-MUSIC.md` §44 records).
- **Wording:** per ADR-0004 this finding carries no byte-tactics identifiers,
  quoted code or decompiled control flow. Routines are named by address and
  by what they do.

Two standing documents are corrected rather than merely extended:
`TOTALA-EXE-MUSIC.md` §44/§48 and the audio clause of
`BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 4/5. The corrections are in §7.

## How the words are reached

Every word here is in the **ordinary table: mask 1**, so any player may type
it in any game type (`BT-GAME-COMMAND-MASK-GATING-FINDINGS.md` rule 1-2).
All are reached through the chat bar's `+` path. Arguments are tokenised on
whitespace and converted with `atoi` (integers) or `atof` (the two floats in
`Contour`); a missing argument takes the handler's default, a present but
non-numeric one converts as the C library does (leading digits, else zero).
`count` counts the command word itself, so "no argument" is `count == 1`.

**None of the words below prints anything.** There is no message call in any
of them; the only ordinary-table commands with feedback are `Logo`
(`"Invalid logo setting"`) and the sharing commands. RWE's `Unknown command:
<word>` is its own, not the original's.

All of them are **local presentation**: none writes a hashed simulation
field. The audio five touch the local MCI/sound object, the seventeen option
words touch the local settings block, the renderer or the local chat HUD.
None is replicated, and none is gated by the cheat or debug masks.

---

## 1. Audio (#908)

The CD player is one object hanging off `game+0x10`. Fields that matter
here (`TOTALA-EXE-MUSIC.md` §42, re-confirmed):

| Object offset | Meaning |
| --- | --- |
| `+0x1FC` | **situation category**, an `int`: 0 Building, 1 Battle, 2 Victory, 3 Defeat, 4 silence |
| `+0x200` | playable audio-track **count**, not the MCI TOC total |
| `+0x204` | locked/repeat track, 1-based ordinal |
| `+0x208` | current track, **1-based ordinal** |
| `+0x214` | track-type bytes, 100 of them, indexed by ordinal |
| `+0x27C` | CD-audio enabled flag, the live "CD Music On" |
| `+0x280` | data-track offset: 1 on a mixed-mode disc, 0 on an all-audio disc |

**Track numbering, settled.** The object's own track number is a 1-based
**ordinal over the playable audio tracks**: ordinal 1 is the first audio
track. On a mixed-mode disc (track 1 is data) the offset is 1 and the MCI
track played is ordinal + 1, i.e. ordinal 1 = MCI track 2. That is the same
number the music screen's `TRACKNUM` shows and the same index the type table
uses. It is **not** the MCI track number and **not** the mp3 file stem once
the two diverge.

There are two neighbouring counters that are easy to confuse with it:

- the raw TOC count from `status cdaudio number of tracks`;
- the playable count, which the disc query leaves at the raw count for an
  all-audio disc but **decrements by one** when track 1 is not audio. The
  shipped default type table is applied only when that count is 16 *and*
  track 1 is not audio, so the disc the original recognises reports 17 TOC
  tracks: 1 data + 16 audio.
- `CDLISTS` keys the per-track type list to the disc's volume serial; it is
  not involved in any command here.

### 1.1 `CDPlay` — `0x4167F0`

| | |
| --- | --- |
| Argument | one integer, argument 1; **absent → 0**; `atoi`, no validation |
| Second argument to the callee | a constant `1` |
| Field | current track (`+0x208`), set to the argument |
| Clamp | **none**: no upper bound, no lower bound |
| Save | no |
| Message | none |
| Local/sim | local audio device only |

It calls the play-one-track routine (`0x4CEB60`) with `(argument, 1)` and
returns. What that routine does:

1. **If the CD-audio flag (`+0x27C`) is clear, it returns at once.** The
   command is a complete no-op with "CD Music" off — it does not even record
   the track. The flag is bit 0 of the music word (`game+0x37F14`, registry
   `musicmode`), set at scene start from that bit and toggled live by the
   MUSIC screen's on/off button.
2. **`0` does not mean "track zero" and does not mean "stop".** It calls
   the mode-dependent chooser (`0x4CDB40`) instead: stop everything if the
   situation is "silence", otherwise advance/pick by the current playback
   mode (nothing new, sequential, random, repeat, situational).
3. Any other value is taken as an ordinal: the current-track field is set
   to it, the data-track offset is added, and an MCI `play cdaudio from n`
   command is issued **immediately** (with a ` to n+1` endpoint when the
   MCI number is below the audio-track count). It is not a "request" that
   waits for the next chooser. It does **not** change the playback mode or
   the situation category; when the track finishes, the normal notify path
   advances by the existing mode.
4. **Above the disc's track count there is no clamp.** The MCI play is
   issued for an out-of-range track and its failure is ignored; the
   current-track field is left at the requested value. On the GOG shim a
   number that names an existing `music/<n>.mp3` may still sound; on a real
   disc MCI refuses it. Either way the command reports nothing.
5. The `1` in the call is **inert**: the play routine never reads its
   second parameter, and the `to` endpoint is always the next ordinal (see
   §7, correction to `TOTALA-EXE-MUSIC.md` §48).

Consequences worth stating because they are the porting trap:

- `+CDPlay 1` plays the **first audio track** (MCI 2, `2.mp3` on GOG), not
  `1.mp3`.
- `+CDPlay 0` is "let the mode choose", so in the default situational mode
  it behaves like a chooser poke, not like a stop; `+CDStop` is the stop.
- `+CDPlay` cannot override "CD Music Off".

### 1.2 `CDStop` — `0x416810`

| | |
| --- | --- |
| Argument | none (any is ignored) |
| Field | current track reset to 1 (or 0 with no disc); play state 0; fade/next timers removed |
| Clamp | n/a |
| Save | no |
| Message | none |
| Local/sim | local audio device only |

Calls the stop routine (`0x4CED40`): MCI `stop cdaudio`, current track to 1,
play state 0, fade step 0, both CD timers removed and set to -1. It does
**not** touch the CD-audio on/off flag or the playback mode, and it has no
enabled-flag gate, so it works even with music off (where there is normally
nothing to stop). Nothing restarts music by itself after it: the chooser is
poked by MCI notifications, timers, a situation change, or a scene
transition — so music stays stopped until one of those, even with music on.

### 1.3 `Sound3D` — `0x416820`

| | |
| --- | --- |
| Argument | none (any is ignored) |
| Field | the sound object's live 3-D flag: set to 1 if it was 0, to 0 if it was 1 |
| Clamp | n/a (a bit) |
| Save | **the save runs in both branches, but the value it writes is not the value that changed** |
| Message | none |
| Local/sim | local mixing only |

What it queries and toggles is the sound object's own 3-D flag, **not** the
"Sound Mode" field (bits 0-2 of the word at `game+0x37F19`).
The command calls the save routine after either branch; the save writes
`Sound Mode` from the unchanged mode field. So:

- the toggle takes effect at once for the rest of the session;
- it is **not persisted**: the next start loads `Sound Mode` and derives the
  3D flag from it (3D only when the stored mode is exactly 2), so the
  console toggle is lost;
- `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 5's "saved either way" is true
  only about the call, not about the value. The register wording should be
  corrected (see §7).

It changes **mixing**, not just a preference. The positional play path
(`0x47F300` area) is: fog/visibility gate first; then, with 3D on, the sound
is positioned relative to the camera centre with min distance = average
viewport tile size × 16 and max = sum of map tile dimensions × 16, played at
a fixed -585; with 3D off, a sound inside the scroll rectangle is played at
-585 and one outside at -1585, a flat 10 dB drop. Both branches share the
same gate and the same fixed in-view attenuation; 3D adds panning and
distance shaping rather than changing the base level.

### 1.4 `MusicMode` — `0x4175E0`

| | |
| --- | --- |
| Argument | one integer, argument 1; **absent → 0**; `atoi`, no validation |
| Field | the **situation category** (`+0x1FC`), an `int` |
| Clamp | **none**; any int is stored |
| Save | **no** (not in the save routine, not in `CDLISTS`) |
| Message | none |
| Local/sim | local music selection only |

It reads argument 1 and hands it straight to the category setter
(`0x4CE690`). **It does not set the playback mode and it does not set the
on/off flag.** This is the central naming trap:

| Saved registry value | Field | Meaning | Values |
| --- | --- | --- | --- |
| `musicmode` | bit 0 of `game+0x37F14` | CD music **on/off** | 0/1, default on |
| `cdmode` | `game+0x37F16` | playback **mode** | 0 nothing new, 1 sequential, 2 random, 3 repeat, 4 situational; default 4 |
| *(none)* | `+0x1FC` | situation **category** (this command) | 0 Building, 1 Battle, 2 Victory, 3 Defeat, 4 silence |

A "category" is therefore a **track type**, the same enum the per-track type
bytes use, plus the value 4 for silence. It is what the situational chooser
filters on and what the battle/peace evaluator writes, not a playback
ordering and not the on/off switch.

**Accepted values and effect.** The field is an `int` and takes anything.
The values with defined meaning are 0-4:

- **4** — the chooser's first branch: stop, reset the current track, kill
  the timers. Immediate silence.
- **2 (Victory) / 3 (Defeat)** — bypass the mode check and go straight to
  the type-matched chooser. These are the only two values that change the
  music when the playback mode is *not* 4. Nothing in gameplay ever sets
  them; this command is the only way a player ever hears the Victory/Defeat
  track types.
- **0 / 1** — recorded always, but only acted on when the playback mode is
  4 (situational). In any other mode the command changes bookkeeping and
  nothing audible.
- **anything else** — stored verbatim. In situational mode the chooser finds
  no track whose type equals it and stops; there is no error.

**What it does when it acts** (setter `0x4CE690`): if the category is
unchanged, return. Otherwise record the new category and, when the mode is 4
or the new category is 2/3, either

- if the old category was 4: restore the volume and call the chooser at
  once, no fade; or
- start a fade-out of the current track (step = -volume/18 every 2 ticks,
  about 36 ticks ≈ 1.2 s) and call the chooser at the end — immediately for
  Battle, after a further 120 ticks ≈ 4 s of silence for Building.

**Interaction with the on/off flag.** The setter itself does not test it. It
still records the category and can still run the fade, but the play calls it
reaches return at once while CD audio is disabled, so nothing sounds.
Re-enabling music later leaves the last category in place for the next
chooser poke.

**Interaction with the evaluator.** The in-game battle/peace evaluator
(`0x494E70`) also writes this category, but only when *its own* next
decision differs from the last decision it recorded. Because a console-set
category is not written into that record, a `+MusicMode 1` can persist
indefinitely (the evaluator computes "back to Building" but compares it with
its own last value of 0 and makes no call), and the next real transition can
be suppressed. The exact resynchronisation is in "Remaining uncertainty".

**Persistence.** None. At scene init the category is set to 0 (Building);
the return to the menus sets it to 4; a CD re-open saves and restores it
around the close/open. A load or restart starts at 0.

### 1.5 `Sing` — `0x4172E0`

| | |
| --- | --- |
| Argument | none (any is ignored) |
| Field | one global int flag, toggled 0 ↔ 1 |
| Clamp | n/a |
| Save | no |
| Message | none |
| Local/sim | local audio choice only |

Not a cheat: ordinary table (mask 1), no gating. It does not play anything
itself. It flips a global that is read only in the unit-speech audio branch
(`0x47FD70`): while set, every unit-speech request that already passes its
own gates has its waveform replaced by `sounds/SING.WAV` — except on the
first game-second of each eight-second block, where `gameTick / 30 & 7` is
zero and `sounds/HONK.WAV` is played instead. Both files are present in the
retail `totala1.hpi`, checked by listing it (§ evidence).

Precisely, it substitutes only the **waveform name**; it does not change the
priority test, the choice count, the caller's enable, the speech flag
(`0x40`, "speech effects"), the volume, the cooldown, or the text branch. So
with Unit Sounds Off, no speech flag, or a failing priority it is silent.
The voice is the **unit whose speech is being played** — the category slot
still supplies the gates and the caption; only the sample changes. It is a
toggle, not a one-shot: the effect lasts until it is typed again, and it is
not stored. `BT-SOUND-PARITY.md` rule 7 already calls it an easter egg and
"not worth porting"; this finding adds the exact mechanism and the asset
names.

### 1.6 Audio summary

| Word | Arg / default | Field | Clamp | Save | Message |
| --- | --- | --- | --- | --- | --- |
| `CDPlay` | int / 0 | current track ordinal; 0 = mode chooser | none | no | none |
| `CDStop` | none | current track 1, play state 0 | n/a | no | none |
| `Sound3D` | none | live 3D flag | n/a | call runs; value not written | none |
| `MusicMode` | int / 0 | situation category 0-4 (+ anything) | none | no | none |
| `Sing` | none | global wave-substitution flag | n/a | no | none |

---

## 2. Display and option setters (#907)

The option words live in three different places and similar-looking bits do
not share a cache or a save effect:

- `game+0x37F06`, 16-bit **visual flags**: bit 1 anti-alias, bit 2 shadows,
  bit 3 vehicle shadows, bit 4 feature shadows, bit 5 shading, bit 6 dithered
  fog, bit 7 (used by the score panel), bit 8 alternate interface.
- `game+0x37F2F`, 16-bit **UI-option flags**: bit 4 no-shake, bit 6 clock,
  bit 10 shoot-all; bits 1-3 are set by the settings load.
- `game+0x14281`, 16-bit **map flags**: bit 0 mapping, bit 1 line of sight,
  bit 2 LOS type.

### 2.1 `Clock` — `0x417300`

| | |
| --- | --- |
| Argument | none (any is ignored) |
| Field | UI-option bit 6 (`game+0x37F2F`) toggled |
| Clamp | one bit |
| Save | **yes** (`clock`) |
| Message | none |
| Local/sim | local HUD |

Draws the game clock at the bottom left of the world view: x = 130,
y = screen height - font height - 34, in the interface font, formatted
`"Game Time : HH:MM:SS"` (the label is translated). The time is the scene
tick counter at 30 ticks/second: hours = ticks / 108000, minutes =
remainder / 1800, seconds = remainder / 30, printed zero-padded to two
digits each. There is no cap and no 24-hour wrap. Nothing is drawn when the
bit is clear. It is a display toggle only.

### 2.2 `Gamma` — `0x417290`

| | |
| --- | --- |
| Argument | one integer, argument 1; **absent → 0**; `atoi` |
| Field | brightness from the argument; the **raw integer** stored in `game+0x37F08` |
| Clamp | **none**, either on the argument or on the raw value |
| Save | **yes** (`Gamma`) |
| Message | none |
| Local/sim | local palette |

It computes `brightness = argument * 0.1` and passes that float to the
palette brightness routine (`0x4BA590`), which stores it in the display
block and re-runs the palette build (`0x4BA200`); then it stores the raw
integer and saves.

The palette build multiplies every 8-bit channel by the brightness and
**clamps each channel only at the top (255)**; there is no floor, so a
negative brightness wraps through the unsigned conversion and is not
"dark", and zero is a black palette. Because the stored value is unclamped
and saved, a `+Gamma 0` or `+Gamma -5` survives a restart; the menu cannot
produce such a value, so there is no way back from the screen except the
console or a hand-edited registry.

**Two different curves share this field.** The options screen writes the
same raw field from its GAMMA slider (max 20, default 12) and computes
brightness as `0.5 + raw/24`; 12 → 1.0, 0 → 0.5, 20 → 1.333. The console
computes `raw * 0.1` instead. So:

- `+Gamma 12` sets brightness **1.2 (120 %)**, not the 1.0 the same stored
  12 means to the menu;
- the next call that re-applies brightness from the menu formula (opening a
  level after the options screen, a scene change, a volume slider) silently
  recomputes 1.0 from the stored 12 — the value is the same, the picture
  changes;
- `+Gamma 20` (the menu's maximum) gives 2.0 from the console, twice the
  menu's brightest.

Also in the load path: a stored gamma of exactly 10 is promoted to 12;
every other value is kept as stored, including out-of-range and negative
ones.

### 2.3 `ScrollSpeed` — `0x416CF0`

| | |
| --- | --- |
| Argument | one integer, argument 1; **absent → 0**; `atoi` then narrowed to an unsigned byte |
| Field | `game+0x1434D`, one byte |
| Clamp | **no minimum**; the narrowing is modulo 256 |
| Save | **yes** (`scrollspeed`) |
| Message | none |
| Local/sim | local camera |

**Unit:** screen pixels added to the camera position per **simulation
tick**, scaled by the frame's tick budget: the per-frame step is
`value * ticksThisFrame`, capped at 128 pixels per frame. The menu slider
runs 1 to 65 and the default is 32, which is also the value written when the
key is missing. `+ScrollSpeed 0` therefore disables edge/keyboard scrolling
entirely (the step is zero and the routine returns); 200 exceeds the menu
maximum and is regularly cut to the 128-pixel per-frame cap; 256 narrows to
0; -1 narrows to 255.

### 2.4 `IFace` — `0x416D20`

| | |
| --- | --- |
| Argument | one integer, argument 1; **absent → 0**; `atoi` |
| Field | `game+0x37EFA`, an `int` |
| Clamp | **none at the command**; the registry load clamps values above 1 down to 1 and keeps negatives |
| Save | **yes** (`Interface Type`) |
| Message | none |
| Local/sim | local input handling |

This is the "Left Click" setting: the options gadget `LEFTCLICK` reads and
writes this value as its stage. **0 = left-click interface (the default),
1 = the alternate scheme (right-click),** and the two engine tests that
consume it split on `== 0` versus `== 1` in different places (order
dispatch, build placement, the default cursor action). Because the tests are
equality tests rather than a boolean, a value such as 2 or -1 is not
normalised: some paths treat it as neither 0 nor 1 and fall through to the
"neither" branch, which is not the same as 1. The command's lack of a range
check is therefore a real way to get an interface value the menu cannot
produce, though the load clamps the *stored* value at the next start.

Note that the engine does not change the control panel art with this value;
it changes how clicks are interpreted and which cursor/order ladder is used.

### 2.5 `ScreenChat` — `0x417130`

| | |
| --- | --- |
| Argument | none (any is ignored) |
| Field | `game+0x37F02`, integer, **exclusive-or with 1** |
| Clamp | none; `xor 1` only normalises 0/1 |
| Save | **yes** (`screenchat`) |
| Message | none |
| Local/sim | local chat HUD |

It toggles the "show all chat" flag used by the in-game message HUD. On the
chat-filter stage that consults it, the HUD normally shows only the message
classes flagged 1, 4 and 8; with this field non-zero it shows every buffered
line. It is not a network setting and sends nothing. The default when the key
is missing is 1 (on). Because the load stores an arbitrary integer and the
command only flips bit 0, a stored 2 becomes 3 (still non-zero) rather than
1 — no visible difference, but it is not a boolean.

`TOTALA-EXE-INTERFACE.md` does not name a menu gadget for it; it is a
console/registry setting.

### 2.6 `ShootAll` — `0x418CA0`

| | |
| --- | --- |
| Argument | none (any is ignored) |
| Field | UI-option bit 10 (`game+0x37F2F`) toggled |
| Clamp | one bit |
| Save | **no** |
| Message | none |
| Local/sim | local option word; effect **not found** |

**No reader of bit 10 exists in the decompiled code.** The bit is toggled
here and nowhere else; the new-game reset clears bits 7, 8 and 9 (the cheat
toggles) but not bit 10; the load does not read a key for it. So in this
build the command flips a bit nothing consults: whatever "shoot all" was
meant to do, the shipped executable does not do it. A port must not invent
an effect for it. (The bit survives a return to the menu within one
process, but not a restart.)

### 2.7 `SwitchAlt` — `0x4165C0`

| | |
| --- | --- |
| Argument | **none → toggle**; with argument 1 → assign it (`atoi`, default 0) |
| Field | visual-flags bit 8 |
| Clamp | one bit (assignment narrows to the low bit) |
| Save | **toggle path: yes. Assign path: no.** |
| Message | none |
| Local/sim | local option word; effect **not found** |

Two behaviours in one handler, selected by `count < 2`. The no-argument form
flips the bit and saves; `+SwitchAlt 0` (or any value) *assigns* it and does
not save, so the value is lost at the next start and the registry keeps the
older one. `+SwitchAlt 2` stores 0. No consumer of bit 8 was found in the
decompiled bodies — only the load, the save and this command touch it — so
its visible effect in this build is **none found**. The existing audit's
"no counterpart" stands, and the reader should not be hunted further without
an executed check.

### 2.8 The render and diagnostic setters

Each line is the command's whole behaviour; all are silent, none writes a
simulation field, and only the ones marked save touch the registry.

| Word | Address | Argument / default | Field | Clamp | Flush cache | Save | Visible effect |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `AntiAlias` | `0x416510` | none | visual bit 1 toggle | one bit | **yes** | **yes** | 2×2 downsample of unit/building sprites; smoother, softer edges |
| `Shadow` | `0x416550` | none | visual bit 2 toggle | one bit | **yes** | **yes** | unit and feature shadows in the cached unit renderer |
| `TShadow` | `0x416630` | none | visual bit 3 toggle | one bit | no | **no** | shadows for mobile units only, under the master above |
| `FShadow` | `0x416660` | none | visual bit 4 toggle | one bit | no | **no** | shadows for features/scenery only, under the master |
| `Shading` | `0x416420` | none | visual bit 5 toggle | one bit | **yes** | **yes** | the shaded rasterizer chain: in the original, buildings and features only |
| `Dither` | `0x416590` | none | visual bit 6 toggle | one bit | no | **yes** | screen-door (dithered) fog instead of blended fog |
| `Light` | `0x4166C0` | three ints, each default 0 | light vector, components × 0.01 | none | **yes** | **no** | model face lighting (a per-face dot product with the vector); the cache flush re-lights cached models |
| `RCache` | `0x416710` | none | none | n/a | **yes** | no | flushes the unit bitmap cache; the next frame rebuilds (a stutter), no lasting state |
| `Contour` | `0x416DB0` | two floats: spacing default 0.0, offset default 0.75 | 16.16 fields, each float × 256 | none | n/a | **no** | terrain contour overlay; zero spacing disables it, negative spacing can keep the loop from terminating |
| `LOSType` | `0x416690` | none | map bit 2 toggle + line-of-sight rebuild | one bit | n/a | **no** | switches between the height-band LOS algorithm (bit set) and the precomputed circle/table LOS (clear) |

Notes that are easy to get wrong:

- `Shadow`, `Shading` and `AntiAlias` flush the unit cache; `Dither`,
  `TShadow` and `FShadow` do not. The bit is read at draw/cache time, so the
  two shadow sub-bits take effect without a flush, while shader/anti-alias
  changes need the cache rebuilt.
- `Light` takes its three components × 0.01 (so `+Light 100 0 0` is a unit
  vector component of 1.0). A missing component is zero; `+Light` alone
  therefore sets a zero vector, not the default light. The vector is used in
  a per-face dot product at draw time, so a zero vector makes models read as
  unlit/black in the lit path rather than restoring any default.
- `Contour` is the only float pair in this set (`atof`); the raw fields are
  16.16 fixed point (value × 256). The offset defaults to 0.75 of a height
  unit while the spacing defaults to 0, i.e. "overlay off" unless a spacing
  is given. RWE has no contour overlay at all.
- `LOSType`'s bit is in the *map* flags, not the visual flags; it does not
  save and it does rebuild sight, so the effect is immediate.

### 2.9 Save and clamp at a glance

The commands that **save**: `Clock`, `Gamma`, `ScrollSpeed`, `IFace`,
`ScreenChat`, `Dither`, `Shading`, `Shadow`, `AntiAlias`, and `SwitchAlt`
without an argument. The ones that **do not**: `ShootAll`, `TShadow`,
`FShadow`, `Light`, `RCache`, `Contour`, `LOSType`, and `SwitchAlt` with an
argument. The non-boolean arguments are `Gamma`, `ScrollSpeed`, `IFace`,
`Light`, `Contour` and `SwitchAlt`'s assign form; `Gamma` and `ScrollSpeed`
are the two with no lower bound, and `IFace` is the one whose out-of-range
value is clamped by the *load* rather than the command.

---

## 3. RWE audit (read-only)

Nothing below was changed, built or run. Line numbers are "about" and refer
to branch `908-audio-option-findings`.

### 3.1 Command surface

- `src/rwe/game/chat_command.cpp`: `parseChatCommand` recognises only
  `sharemetal`, `shareenergy`, `shareall`, `sharemapping`, `shareradar`,
  `setsharemetal`, `setshareenergy` and `give`; everything else is
  `Unknown`. `tokenise` keeps 20 tokens and stops at `#`; `chatAtoi` is a
  saturating `atoi`.
- `src/rwe/game/GameScene_chat.cpp`: `runChatCommand` prints
  `Unknown command: <word>` for any other word. There is no mask, no cheat
  table and no debug table (`BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 2).
- So **none of the twenty-two words here is registered**; each answers
  "Unknown command" today, while the original is silent.

### 3.2 Settings fields on `GameScene`

`src/rwe/game/GameScene.h`:

| RWE field | Type / default | What it is | Where from |
| --- | --- | --- | --- |
| `soundModeSetting` | `SoundMode::Stereo` (2) | Off/Mono/3D stage (stage 2 is named Stereo) | `sound-mode` |
| `unitSpeechSetting` | `UnitSpeechLevel::Full` | Off/Medium/Full | `unit-speech` |
| `musicTrackModeSetting` | `MusicTrackMode::Custom` (3) | Play All/Random/Repeat/Custom | `music-mode` |
| `musicTrackTypes` | vector of ints | per-album track type | `music-track-types` |
| `gammaSetting` | 100 (percent) | brightness percent, used as `gamma/100` in the post shader | `gamma` |
| `scrollSpeedSetting` | 100 (percent) | fraction of `CameraPanSpeed = 1000` world units/s | `scroll-speed` |
| `shadingMode` | `BuildingsOnly` | four stages (RWE widens the two) | `shading-mode` |
| `antiAliasEnabled` | true | 2×2 filter | `anti-alias` |
| `antiAliasUnitsEnabled` | false | RWE-only extension | `anti-alias-units` |
| `shadowsEnabled` | true | master shadow bit | `shadows` |
| `vehicleShadowsEnabled` | true | sub-bit, `rwe.cfg` only | `vehicle-shadows` |

`cameraZoomSetting` and `uiScaleSetting` are RWE additions with no original
counterpart and are not part of this comparison.

There is **no** feature-shadow field, no dithered-fog field and no
alternate-interface bit; `leftClickInterfaceMode` is a `GlobalConfig` bool
(default true), read in `GameScene_commands.cpp` `leftClickMode()`, toggled
only by a debug checkbox (`SceneManager.cpp` about 482) and never written by
`writeGameOptions`. That is the RWE counterpart of `IFace` and it is already
recorded as a gap in `BT-GAME-SETTINGS-PARITY.md` gap 2 — except that its
polarity is inverted from the original's numeric value: RWE true (left
click) is the original's `0`.

### 3.3 Music

- `AudioService` (`src/rwe/AudioService.{h,cpp}`) has a single music track,
  `musicEnabled`, `musicVolumeScale`, `musicGain = 0.3`, `playMusic`,
  `stopMusic`, `musicPlaying`, `getMusicPlaylist`, and no notion of a track
  **number**, a CD, a track type or a situation. `getMusicPlaylist` returns
  every `music/*.mp3`, **sorted as strings**, so the album order is
  `0, 1, 10, 11, 12, 13, 14, 15, 16, 17, 2, 3, ...`, and it includes
  `0.mp3` and `1.mp3` (the duplicates).
- `GameScene` (`GameScene.h` 1172-1206, `GameScene_audio.cpp` 321-517) has
  `MusicSituation { Building, Battle }` only; no Victory/Defeat state.
  `updateMusic` runs the 30-slot battle ring, the same thresholds, the
  10-second lockout, the 4-second Building hold-off and the 1.2-second fade.
  Non-Custom modes walk `allMusicTracks` and ignore the mood. The mood
  evaluator only acts in Custom. RWE also restarts the next track every frame
  while nothing is playing (`shouldStartNextMusicTrack`); the original only
  starts a track on a chooser poke (MCI notify, timer, situation change or
  scene transition). So a `+CDStop` analogue in RWE would last one frame,
  not until the next transition.
- `MusicTrackMode` in RWE is `PlayAll=0, Random=1, Repeat=2, Custom=3`; the
  original's mode is `1..4` plus `0 = nothing new`. **RWE's numbering is the
  original's minus one**, so `music-mode` in `rwe.cfg` is not the same
  integer as registry `cdmode`. RWE has no "off" mode because `music`
  (on/off) is a separate key.
- `SoundMode` in RWE is `Off=0, Mono=1, Stereo=2`. The numbers match the
  original's stored `Sound Mode` (0/1/2), but RWE's stage 2 is named Stereo
  and is the same as Mono in the mixer: only Off changes anything
  (`BT-SOUND-PARITY.md` rule 5; `GameScene_menu.cpp` about 1264). The
  original's stage 2 is the 3D path.
- No track-type value 2/3 (Victory/Defeat) is usable: `splitMusicMoods`
  drops everything that is not Building/Battle, so typing a track Victory in
  `music-track-types` removes it from rotation rather than queuing it.

### 3.4 What already exists that the commands could reuse

- `GameScene_render.cpp` already draws `"Game Time : HH:MM:SS"` from
  `simulation.gameTime / 30`, but on the Space-held stats strip, not as the
  Clock option, and unconditionally.
- `AudioService::setMusicEnabled`, `setSoundEnabled`, `setSoundVolume`,
  `setMusicVolume` are the live switches the audio commands would need.
- `GlobalConfig::writeGameOptions` / `updateConfigFile` are the save path;
  the game writes the whole options page, not a single key, and only on OK.
- `effectsRng` is the presentation-only random source (`GameScene.h` 1170);
  nothing presentation-side may draw from the sim RNG.

---

## 4. Implementation contract

### 4.1 Maps exactly (behaviour RWE can reproduce without a decision)

| Word | Faithful mapping |
| --- | --- |
| `Clock` | a boolean UI option, persisted, drawing the existing game-time string at the original's position; RWE already has the string and the formatter |
| `Sing` | a presentation-only global substitute for `sounds/sing.wav` / `sounds/honk.wav` in the unit-speech audio branch, with the 8-second honk window; no save and no feedback |
| `SwitchAlt`, `ShootAll` | one-bit toggles with no consumer found; they can be accepted as inert words if a port wants the vocabulary, but must not be given invented effects |
| `Dither`, `TShadow`, `FShadow` | bit toggles whose nearest RWE settings are documented in §2.8; the *save* asymmetry (Dither saves, the two shadow sub-bits do not) can be copied if RWE ever has the bits |
| `RCache` | a no-op with a comment, or the cache flush RWE's renderer would need |
| `LOSType` | a rebuild trigger only if RWE grows two LOS algorithms; today a no-op |

### 4.2 Has no RWE counterpart (do not fake parity)

- **CD media.** RWE plays `music/*.mp3` through SDL3_mixer; there is no MCI,
  no TOC, no track count, no disc identity, no `CDLISTS`, and no data track.
  `CDPlay n`, `CDStop` and the on/off flag cannot be reproduced as CD
  operations. The honest mapping is a **track-number command over RWE's
  album** with its own documented numbering, or no command at all.
- **`Sound3D`.** There is no positional mixer to toggle. RWE's positional
  play path ignores its position (`BT-SOUND-PARITY.md` rule 5), Mono and 3D
  are the same stage (`BT-GAME-SETTINGS-PARITY.md` rule 4), and there is
  neither min/max distance nor the -585/-1585 split. A `+Sound3D` that sets a
  flag nothing reads is worse than not having the word; the mixer gap has to
  be fixed first, and that gap is a decision (the code comment calls the
  current behaviour intended).
- **`MusicMode` as a category setter.** RWE has one situation state
  (Building/Battle) and no Victory/Defeat. A `+MusicMode` that accepted 2/3
  would need a new state and the chooser path for it; accepting 0/1 is
  equivalent to forcing the mood. Whether that is exposed at all is a
  design decision.
- **`Gamma` as brightness × 0.1.** RWE's gamma is a shader exponent
  percentage (50-133, 100 = 1.0), not a palette multiplier. Copying the raw
  number straight into the percentage field would make `+Gamma 12` a 12 %
  gamma — a nearly black screen — where the original's `+Gamma 12` is 120 %
  brightness. The faithful *user-facing* result is
  `percent = 50 + 25 × raw / 6` (raw 12 → 100 %).
- **`ScrollSpeed` as pixels/tick.** RWE is percent of 1000 world units/s,
  scaled by wall-clock milliseconds; the original is pixels per simulation
  tick. The units differ by design (RWE's camera is time-based). A console
  mapping has to choose a conversion, not copy the number.
- **`IFace`.** RWE's switch is a bool (`leftClickInterfaceMode`), not an
  int; it exists but is not persisted or surfaced. A console setter must
  decide 0/1 semantics (0 = left click, as the original) and add the config
  key first.
- **`ScreenChat`, `ShootAll`, `Shadow`/`Shading`/`AntiAlias`/`Light`/
  `Contour`/`LOSType`/`Dither`/`TShadow`/`FShadow`.** Several of these have
  no RWE control at all: no chat-filter HUD, no score-panel bit, no
  feature-shadow bit, no dithered fog, no contour overlay, no second LOS
  algorithm, no light-vector command. Registering the words without the
  setting would be a vocabulary with no effect.

### 4.3 Decisions left to the owner

1. **CDPlay numbering.** RWE has no disc. If `+CDPlay n` is offered at all,
   which `n`? Options: (a) the original's ordinal over playable album
   tracks, with the album in **numeric** order and `0.mp3`/`1.mp3` excluded
   (nearest to the original); (b) the sorted `getMusicPlaylist()` index;
   (c) the mp3 stem. They give different tracks today because the playlist
   is string-sorted and includes the duplicates. (a) is the only one that
   keeps `n` meaning what a TA player expects.
2. **Does `+CDPlay` exist without a CD?** It is a CD-media command. A
   "play album track n" command is an RWE extension and should be named and
   documented as such, not presented as `CDPlay` parity.
3. **MusicMode.** Expose the situation at all? If yes: 0/1 only, or add the
   Victory/Defeat states the original never triggers? The latter is a
   visible divergence (the original's endgame silences; it never plays
   them), and RWE's `splitMusicMoods` currently drops those types.
4. **Gamma.** Map the console number through the menu curve
   (`percent = 50 + 25 × raw / 6`) or take a percentage directly? RWE has no
   palette-brightness multiply, so the original's `brightness = raw × 0.1`
   has no direct equivalent; storing the raw number in the percent field is
   wrong either way.
5. **ScrollSpeed.** Persist the original's 1..65 tick value, RWE's
   25..200 percent, or accept both with a documented conversion? The
   existing `rwe.cfg` keys are percentages, so a raw tick number in the
   console would be a second unit for the same setting — the thing to avoid.
6. **Which commands ship at all.** With no console masks in RWE, every word
   registered is available to every player in every game. All of these are
   local presentation and safe in lockstep, but `Sing` and the inert
   `ShootAll`/`SwitchAlt` may be more clutter than they are worth.

### 4.4 If implemented, the invariants

- None of these words may write hashed simulation state, draw from the sim
  RNG, or be included in a command stream. They are local, like the current
  chat line.
- No word here should print a result. The original is silent; RWE's
  "Unknown command" is the only text a player should see for a word it does
  not know.
- Save only where the original saves. If RWE's save is per-key (it is not
  today; it writes the page), the command that saves must still not write a
  key it does not own.
- A command whose field RWE does not have should answer "Unknown command",
  not pretend to succeed.

---

## 5. Remaining uncertainty

All static inferences; **no executed check was run.**

1. **The GOG TOC count.** The arithmetic above requires 17 MCI tracks (1
   data + 16 audio) for the shipped default type table to be applied, and
   the GOG shim's `number of tracks` was not read. A run listing the shim's
   TOC would confirm `CDPlay 1` → `2.mp3` rather than `1.mp3`.
2. **The play routine's endpoint.** The reconstructed body always appends
   `to <ordinal + 1>`; §48 of `TOTALA-EXE-MUSIC.md` says it plays a
   consecutive run of `n` tracks. The two disagree by construction. A
   listening check (cue a Battle track in situational mode and hear whether
   one or two tracks play before the re-roll) settles it. This finding
   records the body's `+1` because the decompilation is exact, and flags the
   prose.
3. **The default table's title alignment.** The type bytes are applied to
   ordinals 1..16, i.e. MCI 2..17 on a mixed-mode disc, so the seven leading
   Battle bytes cover MCI 2..8. `TOTALA-EXE-MUSIC.md` §44's title table
   (Battle 2-7, Building 8-16, 17 outside) is shifted by one against that
   arithmetic, and its footnote that buffer byte 0 types the data track
   cannot hold if the play command adds the data-track offset. RWE's
   name-based default uses six Battle names (2-7), matching the *intent*.
   The most likely explanation is that the shipped table was authored
   against MCI track numbers while the chooser indexes ordinals, i.e. the
   retail default is off by one — a defect, not an intent. An executed check
   (start a battle and hear whether `8.mp3`/Forest Green is treated as
   Battle) settles which the retail disc actually does.
4. **`ShootAll` bit 10 and `SwitchAlt` bit 8 have no reader in the
   decompiled bodies.** Either the build genuinely never uses them (most
   likely: both are legacy option bits) or the consumer is a path this
   reconstruction does not expose. A run setting each bit and watching for a
   difference in unit fire behaviour / selection would settle it. Until
   then, treat "no effect found" as the finding, not as "no effect".
5. **`MusicMode` and the evaluator's resynchronisation.** The evaluator
   compares its new decision to its own last value, so a console-set
   category can stick and suppress the next transition. Whether a later
   real transition re-syncs (or the category stays forever) was not traced
   through every call site; a run typing `+MusicMode 2` in a live battle and
   waiting would answer it.
6. **`Sound3D`'s persistence on a real session.** The body writes the
   unchanged `Sound Mode` field, so the conclusion is that a restart loses
   the toggle. A run toggling it, restarting and listening would confirm
   that nothing else re-derives the 3D flag from the live sound object at
   save time.
7. **`IFace` values other than 0/1.** The equality tests mean some paths
   treat 2 as "neither"; the exact visible result of `+IFace 2` was not
   enumerated across every click handler.
8. **`Contour`'s negative-spacing hang** is recorded from the chapter
   (`BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 4) and is not re-derived here;
   it is a defect not to copy.

---

## 6. Gaps this finding closes

| Rank | Gap | Status here |
| --- | --- | --- |
| 1 | The five audio words had no prose beyond a clause; `MusicMode` read as if it set a mode | settled: §1.1-1.5, including the category/mode distinction |
| 2 | `CDPlay`/`CDStop` had no numbering semantics | settled: ordinal over playable audio tracks, 0 = chooser, no clamp |
| 3 | `Sound3D` "saved either way" was ambiguous | settled: the save runs, the value is not persisted |
| 4 | `Sing` was "an easter egg" with no mechanism | settled: global waveform substitution, honk every 8 s, assets present |
| 5 | The option setters had a list but no units | settled: §2, with the gamma/scroll unit traps |
| 6 | `TOTALA-EXE-MUSIC.md` §44/§48 and the audit's audio clause contain the errors above | corrected in §7 |

## 7. Corrections to existing documents

1. **`BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 5, audio clause.** `Sound3D`
   is not "saved either way" in effect: both branches call the save, but the
   saved key is the unchanged Sound Mode word, so the toggle is lost. Also
   `MusicMode`'s argument is the situation **category**, not a mode; the
   word should say so, and should say it is not persisted.
2. **`TOTALA-EXE-MUSIC.md` §48, last paragraph.** "The `CDPlay <n>` /
   `CDStop` console commands are thin wrappers over play-one-track and stop"
   is right, but play-one-track's second parameter is not read and the `to`
   endpoint is always the next ordinal; §48's earlier description of the
   situational chooser passing a run length does not match the body. Mark
   the run-length statement as the chapter's reading until a listening check
   settles it.
3. **`TOTALA-EXE-MUSIC.md` §44, footnote [1] and the title table.** Buffer
   byte 0 is applied to ordinal 1, which is the first *audio* track (MCI 2
   on a mixed-mode disc), not the data track. The table's "Battle 2-7,
   Building 8-16" split is therefore off by one against the code; see
   uncertainty 3.
4. **`TOTALA-EXE-MUSIC.md` §45 and RWE's `MusicTrackMode`.** The original's
   modes are 1..4 with 0 = "nothing new"; RWE's enum is 0..3. Any comparison
   of `music-mode` with `cdmode` must subtract one.
5. **`BT-GAME-SETTINGS-PARITY.md` rule 3, gamma and scroll.** The
   original's gamma slider range is 0..20 (default 12) and its brightness
   curve is `0.5 + raw/24`; 0..20 maps to 50 %..133 % brightness, which
   matches RWE's 50..133 range. RWE's 50..133 is therefore a *brightness
   percent*, not an arbitrary percentage, and the original's default 12 is
   exactly 100 %. The same rule's scroll range 1..65 and default 32 are
   pixels per simulation tick; RWE's 25..200 percent of 1000 units/s is a
   time-based equivalent, not the same unit.
6. **`BT-GAME-SETTINGS-PARITY.md` gap 2 (`IFace`).** Add the polarity: the
   original's stored 0 is "Left Click", RWE's `leftClickInterfaceMode =
   true` is the same scheme, and the console accepts values outside 0/1 that
   the load then clamps.

No source file was changed. Documentation only.
