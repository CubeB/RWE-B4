# Sound definitions, routing, positional audibility and unit speech parity audit

Spec-room finding, 2026-10-17. Like `BT-WEAPONS-WEAPONS-PARITY.md`,
`BT-INGAME-CONTROL-PANEL-PARITY.md` and `TA-SYSTEMS-PARITY.md`, it allocates no section
number in the main register. It covers two chapters of the reference together because they
are one pipeline: `sound-sound-types` (how `allsound.TDF` and `sound.TDF` become the effect
catalogue and the per-category speech choices) and `sound-sound` (how a request becomes a
voice: named and indexed effects, positional audibility, the replicated sound packet, and the
unit-speech queue with its priorities and cooldowns). This is the finding that implementation
work cites for any of them, per `AGENTS.md` and ADR-0004.

The sample mixer, the Windows fallback and the CD player (`sound-samples`, `sound-play-sound`,
`sound-cd-audio`) are not audited here; `TOTALA-EXE-MUSIC.md` holds the music side. The
Options-screen settings that feed this pipeline (`unitchat`, `unitchattext`, `speechfx`,
`fxvol`, Sound Mode) are in `BT-GAME-SETTINGS-PARITY.md` rule 4 and are only referred to
here.

## Source and evidence class

- **Source.** Two prose chapters of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\`, files
  `sound-sound.md` and `sound-sound-types.md`). Nothing else under that tree was opened: no
  source, no decompilation, and none of its source links were followed. Addresses are the ones
  the chapters name; rules are restated in prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; only BT-9
  (water art on a direct hit) touches sound and is not re-raised.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No original-game play, no listening.
  Several of the findings below are about what a player would hear; the Gaps table says which
  need a listening check.
- **The RWE half** is a read-only audit of `src/rwe/io/soundtdf/SoundClass.*`,
  `game/GameMediaDatabase.cpp`, `game/GameScene_audio.cpp`, the notification and selection
  sites in `game/GameScene_commands.cpp`, `AudioService.*` and `GameLaunch.cpp` at the working
  tree of 2026-10-17. Nothing was built, run or changed. Another agent had uncommitted edits in
  the tree, so every line number is "about".
- **A census of the shipped sound data and the unit definitions** (every archive of the retail
  install through `tools/hpi.py`'s reader, throw-away scratch scripts, the TDF model of
  `BT-UTIL-TDF-PARITY.md`): `gamedata/sound.tdf` in `totala1.hpi` (72 categories),
  `ccdata.ccx`, `btdata.ccx` and `rev31.gp3` (120 each), `gamedata/allsound.tdf` (41 names),
  and the `soundcategory` of every `.fbi` in every archive.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Backend, initialisation and teardown (not applicable)
3. The two definition files and the choice arrays
4. Binding a unit to a category (and the retail typos)
5. Positional audibility: the fog gate and the two attenuation levels
6. The message table, priorities, cooldowns and the speech queue
7. Playing an entry: audio, text and the thresholds
8. The replicated sound packet
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| types 1-3 definition files, choices | none (the TDF files are read; the keys are listed in `TOTALA-EXE-DATA.md` for FBIs only) | rule 3 |
| sound 8 message table (23 kinds) | `TOTALA-EXE-ECONOMY.md` "`underattack`, `repair` and `cant`" (the same 23 ids, keys and captions) | keys and captions **MATCH**; the third column is read differently (rule 6, **CONFLICT**) |
| sound 9, 10 queue | the same ECONOMY section: "eight pending voices", dedupe by id, priority order | agrees on the eight and the dedupe; differs on the sort key and misses the cooldowns (rule 6) |
| sound 5, 6 positional audibility | none | rule 5 |
| sound 7 replicated packet | `TA-DEMOS.md` near 445 (`0x13` has no known length) | adds the length (rule 8) |
| sound 11 `PlaySpeech` thresholds | `TOTALA-EXE-INTERFACE.md` near 539 (the Options gadgets) | rule 7 |
| sound 2, 3, 4 backends | none | rule 2 |

## 2. Backend, initialisation and teardown (not applicable)

**Reference (sound 1-4).** DirectSound is requested at 11025 Hz, 16-bit stereo; a failure with
"no driver" falls back to the operating system's `PlaySound` wrappers (two global flags,
`NoDirectSound` and `UseWindowsSound`); a CD device is opened regardless; the speech queue is
built with interval 30 and a second constant 150 that no method reads; `ResetSpeech` clears
the queue and every kind's next-allowed time; teardown restores the mixer volumes before a null
test on the sound object. The effect catalogue holds 255 names at most (a full table returns
zero, which is also a valid first index); the same name is not loaded twice; a backend flag
decides at free time how a handle is released. A positional or named effect is
**not** played at all in the Windows fallback unless it passes gates the chapter lists.

**RWE: not applicable.** RWE plays through SDL3_mixer with a pool of 256 tracks
(`GameLaunch.cpp` about 325), no DirectSound, no Windows fallback, no CD, and keeps no
catalogue limit (`gamedata/ALLSOUND.TDF` is read once, 41 names in the retail file, about
line 328). The chapter's allocation and backend-ordering faults do not exist here. The one
rule worth keeping is the data one: **a name loaded twice is one sound** (RWE's sound bank is
keyed by name).

## 3. The two definition files and the choice arrays

**Reference (types 1-3).** `allsound.TDF` supplies the logical effect names: for each top-level
record, the record name (32-byte limit) maps to the file named by its `sound` field; a record
without one is skipped. `sound.TDF` supplies **categories**: each top-level record is a category
with a 63-character name and up to 23 message slots (slot 0 unused). For each message key the
loader reads the **unsuffixed** key first, then `key1`, `key2`, and so on **until the first
missing number**; a missing unsuffixed key does not prevent `key1`; a gap at `key2` hides
`key3` onwards. For every choice it also reads the caption from the same key plus `text` (so
`selecttext`, `select1text`); a missing caption keeps the audio choice with empty text. A
slot is a count and two parallel arrays of 64-byte records, waveform names and captions. The
23 keys are `select`, `underattack`, `activate`, `deactivate`, `ok`, `arrived`, `cant`,
`unitcomplete`, `build`, `repair`, `working`, `load`, `unload`, `cloak`, `uncloak`, `capture`,
`count5` to `count0` and `canceldestruct`. The two files load independently: a missing
`allsound.TDF` does not stop the categories.

**RWE: MATCHES the shipped data, DIFFERS on the grammar.** `parseSoundClass`
(`SoundClass.cpp`, about 8-28) reads **one key per kind**: for four kinds the **numbered-1**
key (`select1`, `ok1`, `arrived1`, `cant1`), for the rest the **unsuffixed** key. Each is one
optional string.

- **The base keys `select`, `ok`, `arrived`, `cant`: MISSING.** The chapter tries them first.
  RWE never reads them.
- **Choices 2 and up: MISSING.** `select2`, `ok3` and so on are never read, and there is no
  random choice among alternatives (the chapter picks `rand * count / 32768`).
- **`load` and `unload`: MISSING.** Message kinds 12 and 13 have no field, no event and no
  caller. The register's kind table lists them (ECONOMY.md) and does not claim them ported.
- **Captions (`<key>text`, `<key>1text`): MISSING.** RWE prints its own fixed captions where it
  prints any (rule 7).
- **The unsuffixed key for the other kinds: MATCHES.** `underattack`, `activate`,
  `deactivate`, `unitcomplete`, `build`, `repair`, `working`, `cloak`, `uncloak`, `capture`,
  `count5` to `count0` and `canceldestruct` are the original's names.
- **Census, what this changes on retail data: nothing.** In all four copies of `sound.tdf`
  the four multi-choice kinds appear **only** in the numbered-1 form (`select1` in every
  category, `ok1` in 41 of 72 and 76 of 120, `arrived1` in 30 and 63, `cant1` in 41 and 76),
  **no base key and no second choice** occurs, no category sets `load` or `unload`, and **no
  caption key occurs**. So each slot has exactly one choice in the original too, and RWE's reading
  is that choice. A mod that ships `select2` (the intended mechanism for varied
  acknowledgements) would play one voice only in RWE.
- **Gap and suffix rules: MISSING with the above.** Not reachable on retail.
- **Category file independence: MATCHES in effect.** RWE reads `sound.tdf` through the unit
  loader and `allsound` at start; neither blocks the other. A missing file is a fatal error in
  RWE (`GameLaunch.cpp` about 330, `Couldn't read ALLSOUND.TDF`); the original continues with an
  empty catalogue. Both files ship in every install.
- **Duplicate category names: MATCHES in effect.** None in the retail files; RWE's block map
  keeps the last, and the chapter's linear scan finds the first, so a mod that repeats one
  gets the other (`BT-UTIL-TDF-PARITY.md` rule 5).

## 4. Binding a unit to a category (and the retail typos)

**Reference (types 5).** The unit loader reads `soundcategory`, searches the category names
**for a symbolic value, or uses its numeric conversion path**, and stores a short index on the
definition. Later speech reads that index and the message kind to select a slot. The chapter does
not say whether the name comparison is case-sensitive, and does not say what an **unmatched**
name does. The numeric conversion of a non-numeric word is zero.

**RWE.** `UnitDefinition.soundCategory` is the raw string
(`io/fbi/io.cpp` about 25, `LoadingScene_util.cpp` about 601); `getSoundClassOrDefault`
looks it up in `soundClassMap`, an `unordered_map` keyed by the exact string
(`GameMediaDatabase.cpp` about 160-185), and returns an **empty class** (every key absent, so
the unit is silent) on a miss.

- **Case: DIFFERS inside RWE, UNCLEAR for the original.** Every other TDF key and block name in
  RWE is compared case-insensitively; this map is exact. **Census: one shipped unit,
  `corvoyr.fbi`, writes `COR_Kbot` where the category is `COR_KBOT`** (three archive copies:
  `ccdata.ccx`, `btdata.ccx`, `rev31.gp3`). In RWE its select, acknowledgement and other
  voices are **silent**. If the original compares case-insensitively (the TDF reader does, and
  nothing suggests this search is different) the unit speaks. This is a one-line difference and
  the only retail mismatch of case alone.
- **Names that are not a category at all: UNCLEAR, and the largest question in this audit.**
  Four spellings in the shipped FBIs do not name any category in any shipped `sound.tdf`:
  **`NONE`** (the six walls and teeth: `armdrag`, `cordrag`, `armfort`, `corfort`, `armfdrag`,
  `corfdrag`), **`CORE_KBOT`** (`corspy`, `corfast`, `corfhlt`), **`CORE_MEX`** (`cormex`, the
  retail Core Metal Extractor, in `totala1.hpi`) and **`COR_TANK`** (`corsent`). The last three are
  evidently typos for the real categories `COR_KBOT`, `COR_MEX` and `CORE_TANK` (the shipped
  category names are themselves inconsistent about `COR` and `CORE`, and the FBIs got it wrong
  in both directions). RWE gives all of them
  the empty class: **silence**. The original's "symbolic value or numeric conversion path"
  suggests an unmatched word goes through the numeric conversion and becomes **index zero**,
  which is the **first category in `sound.tdf`**, `ARM_KBOT`. If so, the Core Metal Extractor
  and the Freaker would be heard acknowledging with the Arm kbot's voice, and walls (`NONE`)
  likewise. If instead the search returns a "none" sentinel and the speech code treats it as
  silence, RWE is right. The chapter does not decide. **Settled by a listening check** (select a
  Core Metal Extractor in TA: silence, or an Arm kbot voice) **or a spec-room read of the
  binding loop**. The affected retail units are common (`cormex` is in every Core opening), so
  this is the item to settle first.
- **Numeric `soundcategory`: MISSING.** The original accepts a number as an index into the
  category list. No shipped FBI uses one; RWE would look the digits up as a name and find
  nothing.

## 5. Positional audibility: the fog gate and the two attenuation levels

**Reference (sound 4, 5, 6).** A named or indexed effect needs a nonzero effects volume, at
least one of the low three bits of the sound-mode flags, and a working backend; effects volume
is a **gate**, not a scale (the argument is a fixed -585, hundredths of a decibel). The
positional entry adds two more tests. First, the position must be **inside the map** (a
terrain-cell query). Second, **map knowledge**: with the "mapped" game option set (bit 1 of
the map flags) the current player's **explored** byte at that cell must be nonzero; otherwise
the shared **visibility mask** must contain the player's bit. The cell is the world position
divided by 32 with a projection correction of half the height subtracted from Z. A sound at an
unexplored (or unseen) place is simply **not played**. With 3D enabled the sound is positioned
relative to the camera centre, with minimum distance the average viewport tile size times 16
and maximum the sum of map tile dimensions times 16, and played at -585; **without 3D, a sound
inside the scroll rectangle plays at -585 and a sound outside it at -1585**: a flat 10 dB
drop (amplitude to about a third), not a curve. "Ship gun and explosion sounds take the same
gate; there is no unit-type exception."

**Register.** No mention of any of this. `TOTALA-EXE-WEAPONS.md` and `-RENDER.md` do not
describe the audibility rules.

**RWE: DIFFERS (documented as a decision, not as a divergence).** `playSoundAt`
(`GameScene_audio.cpp` about 559) takes a position and **ignores it** ("FIXME: should play on a
position-aware channel"): it plays the sample on the next free track and adjusts a shared
volume for the number of sounds playing. `doProjectileImpact` says in so many words
(`GameScene_commands.cpp` about 1957) that "the sound is not gated: it is played on a channel
with no position in it, so it is already the whole map's noise". The weapon start sound is
likewise ungated (about 1575). Consequences against the chapter:

- **A fight in the fog is audible.** Gunfire and explosions on unexplored or unseen ground are
  heard in full. In the original they are silent.
- **There is no distance or off-screen attenuation.** A battle across the map is as loud as one
  on the view. In the original it is about 10 dB quieter (non-3D) or distance-shaped (3D).
- **Sound Mode "Mono" and "3D" are the same** (`BT-GAME-SETTINGS-PARITY.md` rule 4), so the
  3D branch has nothing to select.
- **The eight-voice cap** in the original (the sample mixer's channels) is not described by
  these chapters. RWE limits copies of one sample and scales a shared volume instead
  (`AudioService.h` near 30, issue #58); that is a separate mechanism with a different
  audible result and is out of scope here.

This is a real behavioural gap that a player notices (map awareness through sound), and it
also leaks information: a player can hear where an enemy is fighting under fog. It does not
touch the simulation. The two fixes are independent and both small: gate the call with the
same explored-or-visible test the renderer already has (`positionIsVisibleToLocalPlayer`
exists, about line 1580 of `GameScene_commands.cpp`), and add the two-level volume by comparing the
position with the view rectangle. Whether to take the leak as a feature (the comment says it
was chosen) is the maintainer's decision; the audit records only that the original does not.

## 6. The message table, priorities, cooldowns and the speech queue

**Reference (sound 8, 9, 10).** The 24-record table (index 0 unused) carries, per kind, a sound
key, a **priority** and a **repeat delay in seconds** and a default caption:

| Kind | Key | Priority | Delay (s) | Caption |
| ---: | --- | ---: | ---: | --- |
| 1 | select | 10 | 0 | |
| 2 | underattack | 9 | 20 | Under Attack |
| 3, 4 | activate, deactivate | 4 | 2 | |
| 5 | ok | 5 | 1 | |
| 6 | arrived | 3 | 4 | Arrived |
| 7 | cant | 8 | 1 | Cannot Comply |
| 8 | unitcomplete | 3 | 3 | Nanolathe Complete |
| 9 | build | 4 | 2 | |
| 10 | repair | 3 | 1 | |
| 11 | working | 2 | 1 | |
| 12, 13 | load, unload | 7 | 1 | |
| 14, 15 | cloak, uncloak | 7 | 1 | Cloaked, Visible |
| 16 | capture | 4 | 1 | |
| 17-22 | count5 to count0 | 10 | 0 | five to zero |
| 23 | canceldestruct | 10 | 0 | Self destruct terminated |

The next-allowed time is **per kind, global**: not per unit, player or waveform.
`EnqueueSpeech` rejects a kind before its next-allowed frame, **rejects a kind already queued
whichever unit it was for**, allows eight live entries (a nine-entry array), and at eight
**plays the lowest-position entry as text only** to make room. Insertion keeps **descending
priority**, earlier first among equals. `PlayNextSpeech` handles **only the front entry** per
call: if the current tick has reached the last audio frame plus the interval of 30 ticks it
plays audio and text, **otherwise it plays text only**, and in both cases removes the entry. A
request is therefore consumed during the interval rather than kept. The next-allowed time is
set only **when the audio branch is taken** (delay times 30), even if the backend then fails;
a text-only service sets nothing.

**Register.** `TOTALA-EXE-ECONOMY.md` "`underattack`, `repair` and `cant`" lists the same
23 kinds, keys and captions (the table at `0x5086F0`) and records the same eight-slot queue
(`0x47FAD0`, 17-byte entries), the dedupe by id and the head-drop. It says entries "are kept in
priority order from **the table's third dword** (`underattack` 0x14, `cant` 1, `select` 0)".

- **CONFLICT: which column is the priority.** The register's three example values, 0x14 (20),
  1 and 0, are exactly the chapter's **delay** column for `underattack` (20), `cant` (1) and
  `select` (0). The chapter's priority column is the *second* value (select 10, underattack 9,
  cant 8). So the same two numbers are read as a sort key by the register and as a repeat delay
  by the chapter. The two orderings differ in kind: by the register `underattack` outranks
  everything and `select` is last; by the chapter `select` and the countdown outrank
  `underattack`, which outranks `cant`. The same data also decides whether a twenty-second
  cooldown on "Under Attack" exists at all. Neither source says how it knows. **A spec-room
  re-read of the sort comparison in the enqueue routine and of what sets the next-allowed frame
  in the play routine settles it**, and the answer also settles the register's own claim.
  Kind: register.
- **RWE: MISSING (queue, priority, cooldown).** RWE has one reserved audio channel
  (`UnitSelectChannel`, channel 0) and `playSoundIfFree` drops a sound when that channel is
  busy (`AudioService.cpp` about 412). The register's section says this "comes to nearly the
  same thing"; against the chapter it differs in four ways:
  - **No priority.** Whatever starts first holds the channel to its end. A long acknowledgement
    delays or drops an "Under Attack" or a countdown word; in the original the higher
    priority entry sits ahead in the queue.
  - **No per-kind cooldown.** The "Under Attack" voice and its console line fire **on every
    qualifying damage event**: `UnitDamagedEvent` is raised per hit
    (`GameSimulation.cpp` about 3609), and the handler prints "Name: Under Attack" every time
    the damaged unit is off screen (`GameScene_commands.cpp` about 1719). The voice is limited
    only by the channel being busy; the **text line is limited by nothing** (the console keeps
    eight lines for five seconds, so a unit under steady fire fills it). The original sets a
    twenty-second cooldown after the voice is played (if the chapter's column is right) and
    rejects a kind already queued, so the line appears a few times a minute, not several a
    second.
  - **No separation of text from audio.** In the original the line appears whenever the entry is
    serviced and the text threshold passes, with or without the voice. RWE prints from the event
    handler, independently of whether the voice played or the setting is Off.
  - **Selection and acknowledgement bypass the setting.** See rule 7.
- **Admission: MATCHES.** The voice is for the local player's unit only (`playNotificationSound`
  tests the player; the underattack path tests the cause and ownership rules and the
  not-in-view condition of `0x47F850`, all recorded in the register and implemented).
- **Kind coverage: RWE plays kinds 1 to 11 and 14 to 23** (`GameScene_commands.cpp`
  about 1587-1850, the self-destruct code in `GameScene_audio.cpp`), **never 12 and 13**
  (`load`, `unload`), as rule 3 says.

## 7. Playing an entry: audio, text and the thresholds

**Reference (sound 11).** `PlaySpeech` picks the unit definition's category, the slot for the
kind and a random choice, then decides audio and text **independently**. **Audio** requires
priority **strictly greater than ten minus `unitchat`**, a positive choice count, the caller's
enable, and the sound-flags bit `0x40`; the waveform name is the first array's entry
(unless a "sing" command substitutes `sing`, or `honk` on every eighth game-second); the file is
`sounds/<name>.WAV`, played at -585. **Text** requires priority strictly greater than ten
minus `unitchattext`, the caller's enable and a nonempty text (the entry's own, else the
choice's caption); the printed line is the unit definition's name, a colon and the caption
through the game's message routine.

**Register.** `TOTALA-EXE-INTERFACE.md` near 539 lists the gadgets (`SPEECH` "Unit Sounds"
Off, Medium, Full; `UNITCHAT` "Unit Text Acks" Off, Medium, Full) and RWE comments that "which
sounds sit in medium is inference: the original's own split has not been read out of the
binary" (`GameScene_audio.cpp` about 125). **The thresholds are the missing piece, and the
chapter supplies them.**

**RWE: DIFFERS in the Medium stage, and Off does not mean Off.**

- **What Medium means: DIFFERS, on the natural reading.** The registry defaults are `unitchat`
  10 and `unitchattext` 5, which read as "Full" and "Medium" on stage values 10, 5, 0 (a
  guess from the two defaults, not a recorded mapping). If Medium is
  five, a voice passes when its priority exceeds **five**, so the original's Medium keeps
  **select (10), underattack (9), cant (8), load, unload, cloak, uncloak (7), the countdown
  words and the self-destruct cancel (10)** and drops **ok (5), activate, deactivate, build,
  capture (4), arrived, unitcomplete, repair (3) and working (2)**. RWE's Medium
  (`playUnitNotificationSound`, `GameScene_audio.cpp` about 120-150) is the nearly opposite set: it
  drops **select, ok and arrived** and keeps all the rest. So in RWE's Medium a player loses
  the selection voice the original keeps, and keeps "Nanolathe Complete", the activation
  clicks, build and repair chatter that the original silences. The stage values and the mapping
  from gadget to key are unconfirmed (hence a **listening check**, or a spec-room read of the
  options page's write at the `RT` page), but the direction of the discrepancy does not depend on
  the exact value: any threshold between 3 and 9 keeps `select` and drops `working`.
- **Off: DIFFERS inside RWE.** `playUnitNotificationSound` returns for Off, but the selection
  and acknowledgement voices are played by other paths that **never consult the setting**:
  `replaceUnitSelection`, `selectAdditionalUnit` (`GameScene_commands.cpp` about 2812, 2846) and
  the order acknowledgement (about 312, 346) call `getSound` and `playUiSound` directly. So with
  Unit Sounds Off, **every click on a unit and every order still speaks**; only the
  notifications (under attack, complete, build, repair, count) are silenced. The function's own
  comment says "Off silences the chatter entirely". This is a plain bug independent of the
  original, and the most noticeable of the sound findings after rule 5.
- **The text threshold: MISSING.** The `UNITCHAT` and text-line gadgets are disabled
  (`GameScene_menu.cpp` about 743); RWE's captions print whenever the event fires, with no
  setting at all, including when Unit Sounds is Off. The original gates them separately.
- **The sound flag `0x40` and `speechfx`: MISSING.** There is no separate switch for unit
  speech on the sound-flags word; Sound Mode Off silences it with everything else.
- **"Sing"/"honk": MISSING, console command easter egg.** Not worth porting.
- **Caption wording: MATCHES.** "Under Attack", "Arrived", "Cannot Comply", "Nanolathe Complete",
  "Cloaked", "Visible", "Self destruct terminated" and the countdown words are the table's;
  RWE builds the line as the unit's name, a colon and a space and the caption
  (`printConsole`), the chapter's format. The chapter's 100-byte formatting buffer, which a long
  name could overflow, has no counterpart.
- **Random alternative: MISSING** (rule 3): there is only ever one choice per slot.

## 8. The replicated sound packet

**Reference (sound 7).** Both send paths use an **18-byte** record of type `0x13`: a type byte,
a flag byte, a 32-bit sound index and twelve more bytes. The positional sender fills the last
bytes from the position; the nonpositional one leaves them uninitialised. **Both set the flag to
one.** The receiver treats **any nonzero flag** as "play by index, nonpositional" and only flag
zero as "play at the position", so a positional send is received as nonpositional and the
sender's fog and camera tests never apply on the far side; the positional sender also
broadcasts **before** rejecting its own local position. Receivers pass zero to suppress a
rebroadcast. The index is into the shared sound registry, so the peers need compatible assets.

**Register.** `TA-DEMOS.md` near 445 records that the reference omits `0x13` from its size
table, so its length is unknown, and that it "did not appear once in 12.2 million subpackets".
The chapter supplies the length, **18 bytes including the type byte**, and the reason it does
not appear in recordings: it is sent only when the caller asks for replication (the `0` and
`1` replication argument), which the game's own effect calls apparently almost never do.

**RWE: not applicable / UNCLEAR.** RWE's native network game is lockstep and sends commands;
each peer plays its own sounds, so no sound packet exists. The TA-host path (`net/ta/`,
`TaHostGame`) reconstructs gameplay from a joining TA's records. Whether an incoming `0x13`
from a real TA is skipped by length, because the length was unknown, was not traced; the
length from this chapter closes the hole in `TA-DEMOS.md` ("The length of `0x13`") and is
worth recording there. It cannot be decoded as a sound without the peer's registry indices
matching RWE's, which they do not. The positional-versus-nonpositional receiver flaw is
irrelevant to RWE.

---

## 9. Gaps to fix

Ranked by what a player would notice. None touches the simulation (sound is presentation;
the sound path never feeds back into a tick).

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **With Unit Sounds Off, selecting a unit and giving an order still speak.** The selection and acknowledgement paths bypass the setting. | DIFFERS (a bug) | code: route those calls through `playUnitNotificationSound`'s gate (or one shared gate) |
| 2 | **Impact and weapon sounds ignore the fog and distance**: a battle on unexplored or unseen ground is heard in full, and nothing is quieter off screen. The original gates by explored-or-visible map knowledge and drops about 10 dB outside the view. | DIFFERS | decision (the code comment calls it intended), then two small changes |
| 3 | **Unit-speech Medium is not the original's**: RWE drops select/ok/arrived and keeps the chatter; the original (on stage values 10, 5, 0) keeps select and the warnings and drops the chatter. | DIFFERS, UNCLEAR on stage values | listening check of Medium, or spec-room read of the options write |
| 4 | **Retail category names that match nothing** (`CORE_MEX` on the Core Metal Extractor, `CORE_KBOT` on three units, `COR_TANK`, and `NONE` on six walls) leave those units silent in RWE; the original may give them category zero's voice. | UNCLEAR, affects common units | a listening check (select a Core Metal Extractor) or spec-room read of the category binding |
| 5 | **`COR_Kbot` on `corvoyr.fbi`** is silent in RWE because the category map is case-sensitive; every other name match in RWE is not. | DIFFERS (likely a bug) | code: fold case in `soundClassMap` |
| 6 | **No "Under Attack" cooldown or queue**: the line prints on every hit on an unseen unit; the original's per-kind cooldown (20 seconds if the chapter's column is right) and queue limit it to a few a minute. | DIFFERS, plus a **CONFLICT** with the register on which column is priority | code: a per-kind next-allowed tick; spec-room re-read of the sort key first |
| 7 | **Only one choice per slot**: the unsuffixed and numbered-2+ keys, `load`, `unload` and the caption keys are not read. No retail data uses them. | MISSING, mod data only | extend `parseSoundClass` to read the chapter's key sequence and pick by a presentation-side random draw (`effectsRng`, never the sim's) |
| 8 | **Text acknowledgements have no setting**; the `UNITCHAT`, text-delay and line-count gadgets are disabled. | MISSING | wire the gadget; keep the console constants |
| 9 | **Numeric `soundcategory`** and the exact-case match against an index list. | MISSING, no retail use | none |
| 10 | **`0x13` length of 18 bytes** is not in `TA-DEMOS.md`; the receiver flag discrepancy does not apply to RWE. | documentation | add one line to `TA-DEMOS.md` |

No source file was changed.
