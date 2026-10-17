# Smacker movie playback, output surfaces and input handling parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md` and `BT-GAME-SETTINGS-PARITY.md`, it
allocates no section number in the main register. It covers one chapter of the reference,
`graphics-movies` (12 services at `0x47bdf0` to `0x47c790`, plus the frontend adapter that calls
them): how a film is chosen, opened, drawn onto the 640 x 480 game surface, paced, stopped and closed.
The codec itself (`smackw32.dll`) is not in the chapter and is not audited; RWE's own decoder is
compared only for what it must bound. This is the finding that implementation work cites for any of
them, per `AGENTS.md` and ADR-0004.

When a film plays at all (the `PlayMovie` registry value, the campaign endings, the Credits button) is
`BT-GAME-SETTINGS-PARITY.md` rule 6 and `TOTALA-EXE-DATA.md` section 115 and is only referred to here.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\graphics-movies.md`). Nothing else
  under that tree was opened: no source, no decompilation, no source links. Rules are restated in
  prose. The spec-room discrepancy register was read, read-only; no row touches films.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker than
  `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked. Where
  `TOTALA-EXE-EXTERNAL.md` (not read by this project; a cross-check only, see `CLAUDE.md`) agrees with
  the chapter, that is said; it does not raise a statement to a finding.
- **The RWE half** is a read-only audit of `src/rwe/MovieScene.*`, `io/smk/SmkDecoder.*`,
  `MainMenuScene.cpp` (`playMovie`), `GameLaunch.cpp` (the startup logo), `game/GameScene_menu.cpp`
  (`filmsThen` at the campaign end) and `io/malformed_input.test.cpp` at the working tree of
  2026-10-17. Nothing was built, run or changed; line numbers are "about".
- **A census of the five shipped films** (`Data/1.ZRB` to `5.zrb`, headers only, read with a throw-away
  script): all begin `SMK2`; `1.ZRB` 640 x 240, 599 frames; `2.zrb` 640 x 240, 4,058; `3.ZRB`
  640 x 240, 889; `4.ZRB` 640 x 240, 1,841; `5.zrb` **640 x 304**, 4,020. Every film has frame
  rate field -3333 (33.33 ms). Header flags are **2** for films 1 to 4 and **0** for film 5. Audio
  track 0 is present in all five (22,050 Hz, packed, 8-bit, stereo); tracks 1 to 6 are empty.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Choosing and opening a film
3. Where the picture goes: size, position and scanlines
4. Pacing
5. Input and what stops a film
6. Focus, repaint and closing
7. RWE's decoder and malformed input
8. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register | Verdict |
| --- | --- | --- |
| 2 path, missing file, loop | `TOTALA-EXE-DATA.md` section 115 ("The movies": `Data\<name>.zrb`, silently nothing when missing) | missing-file rule agrees; loop and zero length are new (rule 2) |
| 6 draw position, 480 constant | none in the main register; `TOTALA-EXE-EXTERNAL.md` near 660 (films 1 to 4 are 640 x 240 reaching 480 by an alternate-scanline mode, film 5 is 640 x 304 starting at y = 88) | rule 3 |
| 8 stop conditions | none; `ROADMAP.md` says "any key or click skips it, as it skips every film" (a design statement) | CONFLICT, rule 5 |
| 6 pacing, focus | `-EXTERNAL.md` near 662 (fixed 33.33 ms, advance only while the window has focus) | rule 4, 6 |

## 2. Choosing and opening a film

**Reference (sections 2, 3).** The frontend routine builds `Data\<name>.zrb`, asks the archive layer for
its length and **skips the film when the length is zero or the file is absent**; a non-empty file that
the decoder cannot open is fatal. It then fills the game surface black, presents it, hides the cursor
overlay, runs the player to the end, closes it, and **builds a new player again while a flag
(`introMovieLoop`) is still set**; on exit it drains the key queue and blanks the screen. The
constructor opens with flags `0xfe100` (reading sound tracks) and enables movie sound exactly when the
DirectSound object exists; a private or borrowed output surface is chosen by whether the window is
full-screen and whether a DirectDraw object exists, and a mismatch of those two leaves the player with
no output at all.

**RWE.** `MainMenuScene::playMovie` (about 486-515), `filmsThen` (`MovieScene.cpp` about 190-209),
`MovieScene::init`.

- **Absent file: MATCHES.** `playMovie` returns when `readFile` returns nothing and `filmsThen`
  skips to the next film; a data set with no films goes straight to the menu or the game.
- **Zero-length file: DIFFERS.** An empty file is a present, empty vector: `playMovie` and `filmsThen`
  go on, and the decoder constructor throws "SMK file too small" (`SmkDecoder.cpp` about 66). Nothing
  catches it. The original skips a zero-length film like a missing one.
- **Undecodable non-empty file: DIFFERS in spirit.** The original is fatal; RWE also dies (an uncaught
  exception to `main`), but from inside a menu click handler or the end-of-game transition, not from the
  film start. See rule 7.
- **The loop flag: UNCLEAR.** The chapter says the frontend repeats the film while `introMovieLoop`
  is set and that a natural end does not clear it, so a film replays until a character key clears it
  (rule 5); what sets the flag, and which films it applies to (the intro? the startup logo?), is not in
  the chapter. RWE plays every film once. If the intro loops in the original, the INTRO button's film
  is cut short by an RWE end that the original does not have.
- **Black before and after: MATCHES in effect.** `render` fills black; the cursor is the system's.
- **Sound routing: not applicable.** RWE decodes all of track 0 up front, wraps it as a WAV in memory
  and plays it as the music track, stopping it when the scene ends (`MovieScene.cpp` about 40-80);
  the original gives the sound to the decoder's own DirectSound path.
- **The output-surface table: not applicable.** One GL window.

## 3. Where the picture goes: size, position and scanlines

**Reference (section 6).** The decoded frame is written into the 640 x 480 game surface at **x = 0**
and **y = (480 - movie height) >> 1**, at the movie's own pixel size: **no horizontal centring, no
scaling, no clipping, the 480 fixed** (not the current screen height). A film taller than 480 gets a
negative top. The decoder is given a full-surface destination; whatever scaling it applies is set by
the film's own header (see below).

**RWE.** `MovieScene::render` (about 128-150) fits the decoded frame, whatever its pixel size, to **the
largest 4:3 rectangle the window holds**, centred, black elsewhere; the comment says "the movies are
stored anamorphic - 640x240 shown on a 640x480 screen".

- **Films 1 to 4 (640 x 240, header flag 2): DIFFERS in how the missing rows are made.** The Smacker
  header flag bits 1 and 2 are the file's vertical-scale mode; RWE's decoder reads only bit 0 (the ring
  frame, about 120) and ignores them. `TOTALA-EXE-EXTERNAL.md` says these four films reach 480 lines
  in the original **by an alternate-scanline mode that leaves the other rows as whatever the cleared
  surface held** (black), not by duplicating rows. The census agrees that the flag is set on exactly
  these four films. RWE stretches the 240 rows to fill the rectangle, so every row appears twice
  with no dark lines between: a visibly different, softer picture from the original's scanline look.
- **Film 5 (640 x 304, no scale flag): DIFFERS, and the picture is distorted.** The original draws it
  at its own size at y = (480 - 304) >> 1 = **88**, with black bars 88 pixels deep above and below
  (`-EXTERNAL.md` agrees: "starting at y = 88"). RWE stretches 640 x 304 to 4:3, which is a vertical
  stretch of **58 percent** (480 / 304); the credits and everything in them are tall and thin
  compared with the original. The same fit rule applies to every film, so the assumption "all
  films are anamorphic 2:1" is only true of films 1 to 4.
- **Height 240 at y = (480 - 240) >> 1 = 120 is not what the original does for films 1 to 4: UNCLEAR.**
  Applied literally to a 240-high film the chapter's formula puts the top at 120, which with an
  interlaced mode that writes 480 rows would run off the bottom. The film's reported height is
  presumably the height *after* the scale mode (480, giving top 0) and the 304 film reports 304; the
  chapter does not say. Settled by a spec-room read of the call that opens the film, or by
  `-EXTERNAL.md`'s statement that all five sit at a fixed 33.33 ms and that films 1 to 4 "reach 480".
- **Window scaling: DIVERGENCE (documented in kind).** RWE scales to any window; the original is
  fixed at 640 x 480 within its surface. Not a defect.
- **Horizontal position: MATCHES for 640-wide films** (x = 0 and a centred rectangle coincide when
  the window is 4:3).

## 4. Pacing

**Reference (sections 6, 8).** The loop asks the codec's wait routine whether the next frame is due and
decodes **one frame** per positive answer; there is no skipping or catch-up in the player. After
presenting, the player records the last frame that updated the whole rectangle at the origin (for
repaint) and **stops after the final frame has been shown**, without a further advance.

**RWE.** `MovieScene::update` (about 95-120).

- **Frame period: MATCHES.** The header's frame-rate field is read as the Smacker convention
  (positive: milliseconds; negative: hundredths of a millisecond; zero: ten a second), so -3333 gives
  33.33 ms for all five films.
- **Catch-up: DIFFERS.** RWE computes how many frames the elapsed wall time owes and **decodes all of
  them** before drawing the last, so a slow frame drops the intermediate pictures but keeps the film
  on time. The original shows every frame in turn and runs late instead. The audio is a single
  pre-decoded buffer that runs on its own clock in RWE, so catch-up keeps picture and sound together;
  the original lets the codec pace both.
- **Last frame shown, then stop: MATCHES in effect.** The scene ends when the next decode reports
  the end, one frame period after the last picture.
- **Speed independent of focus: DIFFERS.** See rule 6.

## 5. Input and what stops a film

**Reference (section 8).** The loop stops on exactly two messages: the **system-key message for F4**
(Alt+F4: stop, clear the loop flag, **post quit** and return) and any **character message** (stop, clear the
loop flag, still dispatch it). The chapter says the comment above the function, which talks about
mouse messages, is misleading: **a mouse click does not stop a film**, and a key that does not
produce a character message (Shift, Ctrl, arrows, function keys) does not either; keys such as
Escape, Space and Enter do.

**RWE.** `onKeyDown` and `onMouseDown` both call `finish` (`MovieScene.cpp` about 168-178).

- **DIFFERS, and it is a stated design.** Any key press or any mouse button ends the film in RWE;
  `ROADMAP.md` and `GameLaunch.cpp` say so ("any key or click skips it, as it skips every film").
  Against the chapter, a click or a bare modifier key would not skip. It is a **CONFLICT** of design
  against reference, recorded in `BT-NEW-CONFLICTS.md` batch 9; the player-facing question is
  whether an accidental click on the intro should end it. Neither side is chosen.
- **Loop flag cleared by the keypress: not applicable** (rule 2).
- **Alt+F4: not applicable.** The window's close event is the scene manager's.

## 6. Focus, repaint and closing

**Reference (sections 7, 8, 9).** The frame routine returns without decoding when the window does not
have focus, and the loop keeps polling; there is no pause state and no sound pause (`-EXTERNAL.md` agrees:
"advancing only while the window has focus"). The repaint handler blanks the movie rectangle and, if a
full-frame marker was recorded, seeks to it with sound off and then on again (forcing sound on).
Closing releases the surfaces the player created, not borrowed ones, and is not idempotent.

**RWE.**

- **Focus: DIFFERS.** RWE keeps decoding and playing while the window is in the background (wall-clock
  driven); the original freezes the picture. Because RWE's audio is a free-running buffer, the
  sound also continues. Presentation-only.
- **Repaint: not applicable.** A GL window repaints from the retained frame sprite.
- **Close: MATCHES in effect.** `finish` stops the audio and calls the continuation once (guarded by
  `finished`).

## 7. RWE's decoder and malformed input

`CLAUDE.md` ("Anything read from a file or a socket is someone else's") applies: films come from the
data folder and can be replaced by a mod. `SmkDecoder` is RWE's own SMK2 decoder, "checked
frame-for-frame and sample-for-sample against FFmpeg over the shipped movies"; it does not use the
original's codec, so no claim is made here about byte equality with it. `malformed_input.test.cpp` has
**one** Smacker case (a frame size whose product wraps). What is bounded: dimensions (4,096 each), every
frame's byte range against the file, palette and audio chunk sizes, the bit reader (it throws at the end
of its slice). What is not:

- **The frame table size is computed in 32 bits.** `tableFrames * 5` (`SmkDecoder.cpp` about 125)
  is an unsigned multiplication that wraps before the length test. A film claiming 858,993,460 frames
  makes the product 4 and passes the check on a 108-byte file; the decoder then reserves a 3.4 GB frame
  table and walks 858 million entries off the end of the buffer. A count of 4,294,967,295 with the
  ring-frame flag makes the table size 0, passes, and the first `decodeNextFrame` indexes an empty
  vector. Either is an out-of-bounds read, a crash in practice.
- **Huffman tree builders recurse once per bit.** `buildTree8Recursive` and `buildTree16Recursive`
  (about 150-190) call themselves for every set bit; the only bound is the tree-block length. A real
  tree has at most a few hundred nodes, so a film whose block is a long run of ones recurses millions
  deep: stack exhaustion, which no exception handler stops. A small node-count cap (the format allows
  256 leaves for the byte trees and 1,024 for the 16-bit ones) removes it.
- **The audio chunk reserves its declared size.** `frameAudio.reserve(...)` takes the unpacked size from
  the chunk's first four bytes before any sample is decoded (about 371 and 393): up to 2^31 sixteen-bit
  samples (4 GB) for a 16-bit chunk or 2^32 samples (8 GB) for an 8-bit one, from a few bytes of file.
- **Nothing catches a decoder exception.** The constructor, the up-front audio pass in `init`, and
  `update` all run uncaught; every `std::runtime_error` the decoder throws ends the process through
  `main`'s "Critical Error" box (on a menu click, or on the way out of a campaign, where the
  continuation scene is built only if the film finishes). A bad film should end that film.

## 8. Gaps to fix

Ranked by what a player or a modder would hit.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Film 5 (640 x 304, the credits) is stretched to 4:3**, 58 percent taller than the original's letterboxed picture at y = 88. | DIFFERS | code: place a film at its own size (centred in a 4:3 rectangle, scaled uniformly), keep the 4:3 fit only as a window scale |
| 2 | **Films 1 to 4 are shown with doubled rows; the original's file flag asks for alternate scanlines.** | DIFFERS (look) | decision after a side-by-side check; RWE ignores the Smacker scale bits |
| 3 | **Decoder faults from a hostile or damaged film**: the 32-bit frame-table product, unbounded tree recursion, huge audio reserves, and no `catch` around a film. | fault | code: widen the product, cap the node counts and the audio size, catch in `MovieScene` and call the continuation; add cases to `malformed_input.test.cpp` |
| 4 | **Zero-length film file is not skipped** and throws. | DIFFERS | code: treat an empty vector as absent |
| 5 | **A click or a bare modifier key skips a film; in the original neither does.** | CONFLICT (design) | maintainer decision |
| 6 | **Catch-up decoding and no focus gating** (original: one frame per tick of the codec's clock, frozen without focus). | DIFFERS (presentation) | none unless a bug report |
| 7 | **What sets `introMovieLoop`** and whether the intro repeats. | UNCLEAR | spec-room read of the setter |
| 8 | **Top offset for the 240-row films** (120 literally; 0 if the reported height is the scaled one). | UNCLEAR | spec-room read of the open call |

No source file was changed.
