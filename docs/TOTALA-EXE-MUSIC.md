# What the original executable does: music and the CD player

Split out of [TOTALA-EXE.md](TOTALA-EXE.md) on 2026-09-21, which had reached
14,000 lines. **The section numbers are the stable identifier and do not**
**change** -- every `§n` reference in the tree, the commit log and the other
documents still resolves. This file holds §42, §43, §44, §45, §46, §47, §48, §49.

The full index across every file, and §88 (where RWE deliberately differs)
and §91 (decoded but not ported), are in [TOTALA-EXE.md](TOTALA-EXE.md).

## Index

42. [The CD player object](#42-the-cd-player-object)
43. [Registry footprint](#43-registry-footprint)
44. [Track types and the CDLISTS table](#44-track-types-and-the-cdlists-table)
45. [The five music modes](#45-the-five-music-modes)
46. [The situation state](#46-the-situation-state)
47. [The battle/peace evaluator, 0x494E70](#47-the-battlepeace-evaluator-0x494e70)
48. [The chooser in situational mode, 0x4CDE96](#48-the-chooser-in-situational-mode-0x4cde96)
49. [What RWE should implement](#49-what-rwe-should-implement)

## 42. The CD player object

The music engine is one C++ object -- the class name `SJE_CdPlayerClass` is in
the binary at file offset 0x109bd0 -- hanging off the global game object:
`[ds:0x511de8 + 0x10]`. Its fields, recovered from the accessors:

| Offset | Meaning |
|---|---|
| `+0x00` | MCI device open flag |
| `+0x14` | aux device id for `auxSetVolume` |
| `+0x20` | target CD volume (0-0xFFFF) |
| `+0x1fc` | music mode, 0-4 (setter `0x4CE7A0`) |
| `+0x200` | number of tracks on the CD, from `status cdaudio number of tracks` |
| `+0x204` | user-selected track (setter `0x4CE580` clamps to track count; the TRACKNUM display / CDNEXT / CDPREV drive this) |
| `+0x208` | track currently playing, from `status cdaudio current track` |
| `+0x20c` | "we started a play" flag |
| `+0x210` | CD identity (volume serial of the disc, read at `0x4CDA00` via `0x4BB190`/`0x4BB260`) |
| `+0x214` | **track type array**: 100 bytes, one per track, indexed by 1-based MCI track number (`type[track]`, byte). Getter `0x4CE7E0`, setter `0x4CE7C0`, bulk-load `0x4CE3E0` (copies `count` bytes from a buffer into `+0x215`, i.e. buffer[0] -> track 1) |
| `+0x278` | **situation state**, 0-4 (setter `0x4CE690`, getter `0x4CE680`) |
| `+0x27c` | music enabled flag (setter `0x4CEDC0`; when cleared, stops the CD) |
| `+0x284` | volume fade step per timer tick (negative during fade-out) |
| `+0x28c` | completion callback, set to `0x490FE0` (the pump, below) |

MCI plumbing: all control goes through `mciSendStringA` with the literal
strings at file 0x109b58-0x109cc8 (`status cdaudio number of tracks`,
`status cdaudio mode` -- whose answer is strcmp'd against `playing` --
`play cdaudio from %i` + ` to %i` + ` notify`, `stop cdaudio`, and so on). A
play command always carries `notify`; the MM_MCINOTIFY handler (0x4CE130
area) refreshes status via 0x4CDA00 and then calls the completion callback,
which is how the playlist advances.

---

## 43. Registry footprint

Everything is stored under **HKEY_CURRENT_USER\Software\Cavedog
Entertainment\Total Annihilation**. The generic accessor at 0x4B6880 does
`RegCreateKeyExA(HKEY_CURRENT_USER, "Software")`, then "Cavedog
Entertainment", then the section name the caller passes ("Total
Annihilation" at 0x5032E8), with KEY_READ (0x20019) or KEY_WRITE
(0x20006) picked by a read/write flag. Note this 3.1-era GOG exe uses **HKCU**,
not the HKLM path older documentation gives -- the constant pushed at
0x4B68B6 is 0x80000001. (GOG's win32.dll reads the same HKCU path.)

The music-related values, from the settings load at 0x4305A2-0x430760 and
the save at 0x4313B3-0x431476:

| Value | Type | Backs | Default when absent |
|---|---|---|---|
| `musicmode` | DWORD | `[game+0x37f14]` bit 0 -- CD music **on/off** (the "CD Music Off/On" toggle) | on |
| `cdmode` | DWORD | `[game+0x37f16]` -- the music **mode**, 0-4 | **4** (situational) |
| `musicvol` | DWORD | `[game+0x37f10]` -- music volume | 0x20 (32) |
| `CDLISTS` | BINARY, 0xAA0 bytes | the per-CD track-type lists (next section) | zeroed |

Despite the names, `musicmode` is the on/off flag and `cdmode` is the mode.
The strings sit together in the file at 0x102b20 (`cdmode`) and 0x102b28
(`musicmode`).

There is also a **dead** routine at 0x42F910 that writes ten DWORD values
named `track0`..`track9` (sprintf of `track%d` at 0x102968) from a 10-byte
type array -- no call site anywhere in the binary. It is the leftover of an
older per-track persistence scheme, superseded by CDLISTS. Do not implement
it.

---

## 44. Track types and the CDLISTS table

The type of each track is one byte, and the values are exactly the cycle
positions of the TRACKTYPE gadget in MUSIC.GUI
(`text=Building|Battle|Victory|Defeat|Unused;`, `stages=5`):

| Value | Label |
|---|---|
| 0 | Building |
| 1 | Battle |
| 2 | Victory |
| 3 | Defeat |
| 4 | Unused |

The GUI handler reads/writes the byte with no translation (0x45C452: gets
`type[selected]` via 0x4CE7E0 and stuffs it straight into the cycle gadget;
0x45C561 writes the cycle stage back via 0x4CE7C0), so cycle index = type
value.

**Constructor default** (0x4CE260): the whole 100-byte array is filled with
`type[i] = (i % 4) + 1`:

```
4ce287:  mov  BYTE PTR [esi+0x214],bl      ; type[0] = 1
4ce28f:  mov  eax,ecx                      ; i
         cdq / xor / sub / and 3 / ...     ; i % 4 (signed)
4ce29d:  inc  al                           ; + 1
4ce29f:  mov  BYTE PTR [ecx+esi*1+0x214],al
4ce2a6:  inc  ecx
4ce2a7:  cmp  ecx,0x64                     ; 100 entries
```

i.e. a cycle of Battle, Victory, Defeat, Unused -- never Building. This is only
the fallback for an unrecognized CD; on such a disc in situational mode the
Building state finds no track and music simply stops.

**CDLISTS** is an MRU list of 20 CDs, 0x88 bytes each (20 x 0x88 = 0xAA0),
living at 0x51E828 and written verbatim as the REG_BINARY value. Entry
layout (from the search loop at 0x4910C0 and the new-entry writer at
0x491190):

| Entry offset | Meaning |
|---|---|
| `+0x00`-`0x1f` | never written by the code I found (junk/padding) |
| `+0x20` | CD identity dword (`[obj+0x210]`, the volume serial) |
| `+0x24` | track types, `type[1..100]` as bytes (buffer[0] = track 1) |

The pump 0x490FE0 runs at init and on every MCI notify. It looks the current
CD id up in the list; on a hit it moves that entry to slot 0 (MRU) and loads
its types into the object via 0x4CE3E0. On a miss it shifts the list down
(dropping the oldest), writes a new slot-0 entry, **and if -- and only if --
the disc looks like the TA game disc, applies the shipped default types**:

- `status cdaudio number of tracks` must answer exactly **16** (0x491148),
- `status cdaudio type track 1` must answer something other than `audio`
  (0x4CE460 -- i.e. track 1 is the data track of a mixed-mode disc).

The default buffer, built on the stack at 0x490FF3-0x491037, is **seven
1-bytes followed by nine 0-bytes**, applied to tracks 1-16:

- tracks 1-7 -> type 1 (**Battle**) -- track 1 being the unplayable data track,
- tracks 8-16 -> type 0 (**Building**),
- **no track defaults to Victory, Defeat, or Unused.**

Saving: 0x490F80 copies `type[1..count]` back into slot 0's +0x24 area and
writes the whole 0xAA0 blob to CDLISTS (write helper 0x42F960, REG_BINARY).
It is called when leaving the music settings screen (0x49173D).

### Mapping to the GOG files

GOG replaces the CD with music/<n>.mp3 plus an MCI shim (win32.dll, a
winmm proxy). Its `play cdaudio from %i` handler parses the track number and
builds the filename as `"music/" + itoa(n) + ".mp3"` with **no offset**
(number stored at its 0x10009030, filename assembled at 0x100013FE), so
<n>.mp3 *is* MCI track n. The shim's own auto-advance wraps in 2..17,
consistent with track 1 being the data track. So the shipped default translates
directly:

| CD track / mp3 | Title | Default type |
|---|---|---|
| 1 | (data track -- typed Battle but not playable audio) | Battle[1] |
| 2 | Brutal Battle | **Battle** |
| 3 | Fire And Ice | **Battle** |
| 4 | Attack!!! | **Battle** |
| 5 | Warpath | **Battle** |
| 6 | The March Unto Death | **Battle** |
| 7 | Ambush in the Passage | **Battle** |
| 8 | Forest Green | **Building** |
| 9 | Death And Decay (file duplicates 0.mp3) | **Building** |
| 10 | Stealth (file duplicates 1.mp3) | **Building** |
| 11 | Licking Wounds | **Building** |
| 12 | Futile Attempt | **Building** |
| 13 | On Throughout the Night | **Building** |
| 14 | Desolation | **Building** |
| 15 | Charred Dreams | **Building** |
| 16 | Where Am I | **Building** |
| 17 | Blood of the Machines | *(not on the 16-track disc the default recognizes; GOG extra)* |

[1] The data track genuinely carries type 1 in the default table (buffer byte 0
is 1 and 0x4CE3E0 maps buffer[0] -> track 1). The random pick below can
therefore land on it; on real hardware MCI just fails to play it and the next
notify re-rolls. An implementation should simply not include it.

The battle/building split lands exactly on the aggressive-sounding titles
(2-7) versus the calm ones (8-16), which is good evidence the alignment is
right. 0.mp3/1.mp3 are duplicates of tracks 9/10 that the original logic
never addresses; 17.mp3 is outside the recognized TOC. Treat 17 as a
Building track if you want it in the rotation (it is a calm track).

---

## 45. The five music modes

`[game+0x37f16]` / registry `cdmode`, copied into the object (+0x1fc) at
scene start. The chooser (0x4CDB40) dispatches on it through the jump table
at 0x4CE00C:

| Mode | Handler | Behaviour when the current track ends (or the chooser is poked) |
|---|---|---|
| 0 | 0x4CDBEF | nothing new is ever started; when the current play finishes, `stop cdaudio` and clean up |
| 1 | 0x4CDCC4 | sequential: next = current+1 (wrap to 1 past the end), and it issues one MCI play from that track **through the last track** |
| 2 | 0x4CDD7B | random: `rand() % count + 1`, play that one track |
| 3 | 0x4CDDFD | repeat: keep the user-selected track (+0x204) playing; if something else is playing, switch to it |
| 4 | 0x4CDE96 | situational, by track type (next sections) |
| - | 0x4CDB5D | before any of that: if the situation state is 4, `stop cdaudio` and reset; if the state is 2 or 3 (Victory/Defeat), jump straight to the type-matched handler regardless of mode |

The TRACKMODE cycle in MUSIC.GUI is `Play All|Random|Repeat|Custom`
(4 stages) and the code maps it as **mode = stage + 1** (0x45D156: the
gadget is set to mode - 1). Mode 0 is not on the dial; it exists for
completeness/off. The out-of-the-box mode is **4, Custom** -- the situational
system is TA's default.

The TRACKTYPE gadget is only enabled when music is on **and** mode is 4
(0x45D239: `test [0x37f14],1` / `cmp [0x37f16],4`).

---

## 46. The situation state

`[obj+0x278]`, values matching the track types: 0 Building, 1 Battle,
2 Victory, 3 Defeat, 4 = silence/stopped. The setter 0x4CE690(newState):

1. If unchanged, return.
2. Save the currently-playing track into `0x51FF20[oldState]` -- a per-state
   resume table that is **written and never read** (only xref is the write at
   0x4CE6AF). Dead.
3. Record the new state.
4. If mode != 4 **and** the new state is not 2/3, stop there -- the state is
   bookkeeping only. Otherwise:
   - if the old state was 4 (silence): restore volume and call the chooser at
     once -- no fade;
   - else start a **fade-out**: step = -volume/18 every 2 ticks (timer set at
     0x4CE770; the imul by 0xC71C71C7 then `sar 2` is signed /18). When
     the fade reaches zero (0x4CE5E0): volume 0, and if the new state is 0
     (Building) arm a one-shot **120-tick** timer before calling the chooser
     (0x4CE64D), otherwise call the chooser immediately.

Ticks here are the game clock: 0x4B6340 returns
`timeGetTime() * R / 1000` where R is the tick rate at `[[0x51FBD0]+0xe8]` --
30 at Normal game speed (inference from the game-speed system; the timer
service 0x4B63F0 counts these down). So the fade is 18 x 2 = 36 ticks,
about **1.2 s**, and Building music resumes **4 s** after the fade ends.

**Who sets the state.** Every call site of 0x4CE690, exhaustively:

| Site | State |
|---|---|
| 0x491477 | 0 at game-scene init (music starts in Building) |
| 0x494F8C | 0 or 1 from the battle/peace evaluator (next section) |
| 0x49848B | 0 on the display-mode-change/resume path |
| 0x4910A9, 0x49EC2E | restores a saved state around CD re-init / disc change |
| 0x41ED7E, 0x426462, 0x460602, 0x491B86 (helper 0x491B60, called from six places), 0x4996BE, 0x49986B | **4** -- menus, frontend screens, and both endgame paths |
| 0x4175F6 | the **MusicMode console command** (table entry at .data file 0x10041C): sets the state to its numeric argument |

The endgame handlers (0x499603 region: `mov edi,4` ... `push edi`) silence the
music when the game ends -- **they do not set Victory (2) or Defeat (3)**.
States 2 and 3 are reachable *only* through the debug console command. The
chooser and the GUI fully support Victory/Defeat track types, but no gameplay
event ever triggers them; the feature was built and never wired up. (This
matches the long-standing community observation that the Victory/Defeat
settings do nothing.)

Also for the record: the string `battlestart` (0x104440) is the name of the
start button in the multiplayer battle room GUI. It has nothing to do with
music.

---

## 47. The battle/peace evaluator, 0x494E70

Runs from the in-game per-frame loop (called at 0x4999A1). Gates:
`[game+0x2a44]` bit 2 must be set (set at mission start
0x4269B0/0x426BA4, cleared by the endgame music-off helper 0x491B60),
not paused (`[game+0x38d75]` bits), and it early-outs unless at least
**30 ticks (about 1 s)** have passed since its last full run (0x51F2F8 holds
the last run time).

State it keeps:

- 0x51E710: a ring of **30 slots**, one per second, cleared at game start
  (0x4919F7). Index at 0x51F2DC. Each run advances the ring and zeroes the
  new slot.
- 0x51F2FC: seconds since the last state change (reset to 0 on a change).
- 0x5091D0: the last state the evaluator chose.

**What feeds the ring** -- AddBattleActivity(n) at 0x494FF0 adds n to the
current slot. Exactly two call sites:

- 0x489DE6, in the weapon-damage application path: **+1** when a unit takes
  a hit and either the attacker's owner or the victim's owner is the local
  player (0x489DC6-0x489DE4, local player index at `[game+0x2a42]`).
- 0x4869EB, in the unit-death path: **+5** when the dying unit's killer
  (`[unit+0xf4]`, the owner of the last unit that damaged it) is the local
  player.

**The decision**, once per second, and only when at least 10 s have passed
since the last change (0x494ED7: `cmp eax,0xa; jle skip`):

- Let sum30 = sum of all 30 slots (last ~30 s), sum5 = sum of the 5 most
  recent slots (last ~5 s).
- Currently Building (state 0) -> **switch to Battle** if
  `sum30 > 50 || sum5 > 30`, **and** the local player's unit count
  (`word [game + 331*p + 0x1ca7]`, the counter the debug overlay labels
  "Total Units", p = local player) is **> 30** (0x494F53). A commander
  skirmish with a handful of units keeps the peace music no matter how hot it
  gets.
- Currently Battle (state 1) -> **revert to Building** if
  `sum30 < 10 && sum5 == 0 && at least 60 s since entering Battle`
  (0x494F69-0x494F82).
- On a change: setState, reset the 10 s / 60 s counter.

The evaluator always runs (in any mode); the setter just ignores its result
unless mode is 4, so flipping to Custom mid-game picks up the current
situation.

---

## 48. The chooser in situational mode, 0x4CDE96

Poked at every MCI notify (track finished), after every fade, and at scene
start. With the state in s:

1. If MCI reports `playing` and `type[currentTrack] == s`, do nothing.
2. Otherwise pick a track: `r = rand() & 0xF`; walk forward from the current
   track, wrapping past the track count to 1, and take the **(r+1)-th track
   whose type equals s** -- a uniform-ish random pick among the tracks of the
   wanted type, with a scan budget of (r+1) x count steps.

```
4cde96:  call rand ; ebx = eax & 0xF
4cdf48:  inc  ecx / wrap to 1                ; walk forward
4cdf54:  mov  al,[ecx+edx*1+0x214]           ; type[track]
4cdf5b:  cmp  eax,ebp / jne                  ; == state?
4cdf5f:  dec  ebx / jle found
4cdf64:  dec  esi / jg loop                  ; budget = (r+1)*count
```

3. From the chosen track, count how many **consecutive** tracks (no wrap)
   share the type, and issue a single `play cdaudio from c to c+n notify`
   (0x4CDF6B-0x4CDF8B, play routine 0x4CEB60). With the default table a
   battle pick plays through the rest of tracks 2-7 in disc order before the
   next notify re-rolls.
4. If the budget runs out with no track of type s on the disc:
   `stop cdaudio` -- silence until the next state change.
5. Restore the target volume (0x4D00D0, an `auxSetVolume` on both channels).

Victory (2) and Defeat (3) route into this same type-matched pick regardless
of mode (checked before the mode dispatch, 0x4CDBC2), which is how they
*would* have played had anything set them.

The `CDPlay <n>` / `CDStop` console commands (0x4167F0/0x416810) are thin
wrappers over play-one-track and stop.

---

## 49. What RWE should implement

A playlist of named tracks, each typed Building/Battle/Victory/Defeat/Unused,
with these defaults (GOG names):

- **Battle**: Brutal Battle, Fire And Ice, Attack!!!, Warpath, The March Unto
  Death, Ambush in the Passage.
- **Building**: Forest Green, Death And Decay, Stealth, Licking Wounds,
  Futile Attempt, On Throughout the Night, Desolation, Charred Dreams,
  Where Am I -- and Blood of the Machines if we want the whole OST in rotation
  (it was not on the disc the original's default table covers).
- **Victory / Defeat**: empty by default, exactly like the original.

Behaviour (all times at Normal speed; internally these are 30 Hz ticks and
scale with game speed in the original):

1. **Start of a battle scene**: state = Building; pick a random Building track
   and play it. Menus and the frontend: no music (the original stops the CD
   there).
2. **Track selection**: uniform random among tracks of the current state's
   type. When a track ends, pick again from the same type. (The original's
   forward-scan from the current track and its consecutive-run playback are
   CD artifacts; random-per-track is the faithful simplification -- note the
   original *can* repeat the same track back-to-back, since the scan can lap.)
3. **Battle detection**, evaluated once per second in-game, not while paused:
   - Keep a 30-slot one-second ring of "battle points": **+1** whenever a unit
     belonging to, or attacked by, the local player takes a weapon hit; **+5**
     whenever the local player's damage kills a unit.
   - Building -> Battle when (points in last 30 s > 50 **or** points in last
     5 s > 30) **and** the local player owns more than 30 units.
   - Battle -> Building when points in last 30 s < 10 **and** last 5 s = 0
     **and** Battle has held for at least 60 s.
   - After any switch, no new switch for 10 s.
4. **On a switch**: fade the current track out over about 1.2 s (18 steps of
   -vol/18 every 2 ticks). Then, entering Building, wait a further 4 s
   (120 ticks) of silence before starting the Building track; entering Battle,
   start the Battle track immediately after the fade.
5. **Game end (win or lose)**: fade out and stop the music. Do **not** play
   the Victory/Defeat types -- the original never does; those states exist only
   behind its debug console command. (If we ever want them: they bypass the
   mode check and play a random track of the matching type.)
6. **Modes**, if we surface them: Off, Play All (album order, wrapping),
   Random (any track), Repeat (one track), Custom (the above). Default Custom.
   The situational evaluator runs regardless of mode but only Custom acts on
   it. If a wanted type has no tracks in Custom: silence until the state
   changes.
7. **Settings that persist**: music on/off, mode, volume, and the per-track
   type list (the original keys the list to the disc identity in CDLISTS;
   RWE has one fixed "disc" and needs just one list).

### Loose ends, labelled

- The per-state resume table (0x51FF20) is dead code in the exe -- the
  original never resumes a track where it left off; every entry into a state
  re-rolls.
- The first 0x20 bytes of a CDLISTS entry are never touched by any code I
  found; unknown, probably unused.
- The "> 30 units" gate reads the counter the debug overlay labels "Total
  Units"; I found its increments (unit creation, 0x486187/0x486322) and
  its use as the live count against the unit limit, but not the decrement
  site -- some computed addressing I did not chase. Its role as "current unit
  count of the local player" is solid from the limit checks and the player-
  elimination checks that read it.
- R = 30 ticks/second for all timings is inferred from the game-speed system
  (Game Speed normal = 30 fps); the arithmetic in 0x4B6340 is exact
  (`ms * R / 1000`), and R at other speed settings scales the music timings
  with it.

---
