# Campaign menus, briefing pages and animated mission artwork parity audit

Spec-room finding, 2026-10-17. Like `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`,
`BT-GRAPHICS-MOVIES-PARITY.md` and `BT-INGAME-INFO-PANEL-PARITY.md`, it allocates no section
number in the main register. It covers one chapter of the reference, `frontend-campaign-menu`:
how the campaign list is discovered and filtered by side, the single-player root screen and its
hidden "any mission" toggle, the two new-game layouts, how a campaign or a single mission is
started, the briefing screen (text pagination, colour markup, blinking words, narration, the
planet table and the scrolling panorama) and the seven setup-slot queries the skirmish screen
uses. This is the finding that implementation work cites for any of them, per `AGENTS.md` and
ADR-0004.

Not repeated: the campaign file format, progress string and the save written between missions are
`BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`; the end-of-mission screen is `BT-FRONTEND-ENDGAME-PARITY.md`;
the skirmish screen that uses the slot queries is `BT-FRONTEND-SKIRMISH-MENU-PARITY.md`; the
disc prompt is `BT-GAME-CD-CHECK-PARITY.md`; the narration's streaming is the sound audit.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\frontend-campaign-menu.md`).
  Nothing else under that tree was opened: no source, no decompilation, and none of its source
  links were followed. Addresses are the ones the chapter names; rules are restated in prose. The
  spec-room discrepancy register was read, read-only; no row concerns this chapter.
- **Evidence class.** A **static reading** by that project: nothing in it was executed. Weaker
  than `TOTALA-EXE*.md`. Disagreements are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No original-game play.
- **The RWE half** is a read-only audit of `src/rwe/MainMenuScene_campaign.cpp`,
  `MainMenuScene.cpp` (`goToSingleMenu`, `messageNow`), `ui/UiTextRegion.*`, `TextureService.cpp`
  (`getGafEntryInternal`) and the shipped campaign files of the retail install, read through
  `tools/hpi.py`'s reader and a directory listing (`camps/` in `ccdata.ccx` and loose in the
  install folder), at the working tree of 2026-10-17. Nothing was built, run or changed. Another
  agent had uncommitted edits in the tree, so every line number is "about".
- **Documents read first:** `TOTALA-EXE.md` (index, sections 88 and 91),
  `TOTALA-EXE-DATA.md` section 115 (the register's account of these screens),
  `TOTALA-EXE-INTERFACE.md` and the ADRs.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Campaign discovery and the side filter
3. The single-player root, the any-mission toggle and `AllMissions`
4. The new-game layouts and Start
5. Briefing text: markup, blinking words and pages
6. Briefing artwork: the planet table, the panorama and the readouts
7. Briefing activation and narration
8. The setup-slot queries
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| discovery and side filter | `TOTALA-EXE-DATA.md` section 115 step 2 | **MATCHES** (rule 2) |
| single-player root, `DRDEATH` toggle | section 115 step 1 ("vestigial") | agrees; settles an open row of the settings audit (rule 3) |
| new-game layouts, starting a mission | section 115 steps 2 and 3 | **MATCHES**; the chapter does not say which mode is reached (rule 4) |
| briefing text, markup, pages | section 115 step 3 ("paged by `MOREBAR`") | partly implemented (rule 5) |
| briefing artwork, planet table | section 115 step 3 (the table and the readouts) | table **MATCHES**; readouts **MISSING** (rule 6) |
| activation, narration | section 115 | **MATCHES** (rule 7) |
| setup-slot queries | none | rule 8 |

## 2. Campaign discovery and the side filter

**Reference.** The campaign list is every `camps/*.TDF` whose `[HEADER]` key `campaignside`
equals, **case-sensitively**, the selected side's configured name or the literal `ALL`. The side
list for the saved-game consumers is built from the side data with each name's later letters
**lower-cased by adding 0x20 to every byte after the first** (so "ARM" becomes "Arm"; a digit or
punctuation mark would be changed too). The scan copies names into a buffer and advances the
source cursor **only after a file loads**, so a file that fails to load makes every later
iteration retry the same name and skip the rest; a zero-match list is not terminated. The
number of campaign files decides a layout (rule 4); `HasFewerThanThreeCampaignFiles` is a count
of at most two.

**RWE: MATCHES for the retail data; DIFFERS for case.** `campaignNamesForSide`
(`MainMenuScene_campaign.cpp` about 78) lists `camps/*.tdf`, parses each, and keeps it when the
header's side, **upper-cased**, is `ALL` or the side's name (`ARM`, `CORE`). A file that does
not parse is logged and skipped, which removes the original's retry-the-same-name defect.

- **Case: DIFFERS for mod data, MATCHES for retail.** The two shipped campaign files
  (`CAMPS/Arm Campaign.tdf`, `CAMPS/Core Campaign.tdf`, both in `ccdata.ccx`; no other archive of
  the retail install holds a `camps/*.tdf`) write `campaignside=ARM` and `campaignside=CORE`, in capitals, as the
  side data names them. A mod writing `Arm` would be listed by RWE and not by the original.
- **Census, a loose file: UNCLEAR for the original.** The install folder carries a loose
  `camps/EXAMPLE.TDF` (side ARM, two missions pointing at `example.ufo`, with a brief and a WAV
  under `camps/briefs/`) beside the packaged campaigns. RWE reads the install directory as a
  loose layer, so the Arm list holds **two** entries (`Arm Campaign` and `EXAMPLE`; the default selection is
  the first, `Arm Campaign`) where the packaged data alone gives one. Whether the original
  counts, lists or rejects a loose campaign file is the same question as the loose-unit rule of
  `BT-GAME-CD-CHECK-PARITY.md` rule 6 and the chapter does not settle it. The effect on the
  original's count-based layout choice (two files or fewer) is in rule 4.
- **The side names in the list: not applicable.** RWE draws "Arm" and "Core" from literals
  (`refreshCampaignMenu`, about 140-150); the title-casing function exists for the saved-game
  side list, which RWE does not have.

## 3. The single-player root, the any-mission toggle and `AllMissions`

**Reference.** `OpenSingleMenu` loads SINGLE.GUI over `singlebg`, sets the local player's side
and the opposite for the next slot (for side zero or one), selects mission type one, and enables
a hidden **AnyMsn** button when bit zero of the "all missions" flag is set. Typing the seven
characters `DRDEATH` into the screen's key history toggles that bit and the button's visibility
and saves the setting. The buttons: NewCamp and AnyMsn need the campaign disc and request
frontend substates ten and fourteen; Skirmish needs the multiplayer disc and requests eleven;
LoadGame and Options open their screens; PrevMenu goes back.

**Register.** `TOTALA-EXE-DATA.md` section 115: "The hidden `AnyMsn` button (revealed by typing
`DRDEATH`) opens the same screen in the same mode, so in v3.1 it is vestigial."

**RWE.**

- **No `DRDEATH` and no AnyMsn button: MISSING, no effect.** `messageNow` (`MainMenuScene.cpp`
  about 829-870) handles NewCamp, Skirmish, Options and LoadGame; there is no hidden button and
  no typed-history test. Because the register says AnyMsn opens the same screen NewCamp does,
  the toggle changes nothing a player could use.
- **`AllMissions` is answered.** `BT-GAME-SETTINGS-PARITY.md` rule 6 lists `AllMissions` as
  "UNCLEAR: the chapter does not say what reads it". This chapter does: the only reader the
  chapter shows is the single-player menu, where its bit shows or hides AnyMsn, and its only
  writer is the `DRDEATH` toggle. With the register's reading it has no effect on gameplay. **Close that row
  as "reveals a button that opens NewCamp's screen".**
- **Sides on entry: MATCHES in effect.** RWE keeps `campaignSide` on the scene (default 0, Arm)
  and builds the parameters from it (`startCampaignMission`, about 257).
- **The disc gates: not applicable** (`BT-GAME-CD-CHECK-PARITY.md` rule 5).

## 4. The new-game layouts and Start

**Reference.** `OpenNewGameMenu` takes one parameter. **Nonzero**: NEWGAME.GUI with the
`playanygame4` background, both lists shown, the campaign list squeezed to y 308 and 48 high and
the mission list 62 high. **Zero**: it counts campaign files; **two or fewer** gives
`newcampaign4x` (a simplified layout: side, difficulty and Start only; Start loads the literal
`Arm Campaign` or `Core Campaign` at mission zero), **more than two** gives `newcampaign4` (a
full layout with the campaign list). The chapter says to interpret the actual parameter and does
not say which parameter the substates ten and fourteen pass. Start plays a sound, requires the
disc, registers archives, resets the progress string, loads the selected campaign and mission,
sets player 0's colour to 0 and player 1's to 1, **saves the settings**, and requests the
briefing (substate sixteen in any-mission mode, fifteen otherwise). Difficulty cycles 0, 1, 2.
Side buttons update the status, the side indicator colour, the global side and both players'
sides and rebuild the lists; the Core branch skips rebuilding in the simplified layout and the
Arm branch does not. The apparent null-test on the campaign list callback still dereferences it.

**RWE: MATCHES the register's reading.** `goToCampaignMenu` (about 110) builds the any-mission
layout only: the exact geometry (`ypos` 308, height 48 and 62), the `playanygame4` background,
both lists, `Side0`/`Side1`/`Arm`/`Core` for the side, the difficulty dial, and a mission list
that refills when a campaign is chosen (`fillCampaignMissions`, about 203). `startCampaignMission`
(about 257) resets the progress to untried, takes the selected mission (default zero) and the
colours 0 and 1 (`openCampaignMission`, about 279). The campaign file name is the list entry.

- **Which parameter the original passes: UNCLEAR in the chapter, settled by the register.**
  `TOTALA-EXE-DATA.md` section 115 says NewCamp calls the builder with 1 (`0x42736B`) and that
  the zero mode "is reachable only from itself and is dead". The chapter's briefing-context table
  (`BT-FRONTEND-FRONTEND-PARITY.md` rule 2) returns to the new-game menu with argument zero or one by the
  context, which the register's statement would make always one. **No conflict is proven**, but
  the chapter's two-layout description is where a reader gets the idea that retail NewCamp shows
  the simplified screen, and with two shipped campaigns it would. The register's reading was
  taken at the call site and is the better evidence. **Original check** (one line): click New
  Campaign in an unmodified install and see whether a campaign list and a mission list appear.
- **The count that decides the layout is moot in RWE** (no zero mode). The loose
  `EXAMPLE.TDF` of rule 2 would push the count to three in the original if loose files count.
- **Saving settings on Start: MISSING.** The original writes side and difficulty so the next
  visit opens with them; RWE keeps them on the `MainMenuScene` object, which dies with the
  process (see `BT-GAME-SETTINGS-CAMPAIGN-PARITY.md`: no `ArmCamp` value exists in the
  retail registry key either way, so nothing is lost by the omission).
- **The side-rebuild asymmetry in the simplified layout: not applicable.**
- **The progress string** starts as 25 `U` for a campaign begun here (`CampaignProgress`
  defaults), matching the chapter's `InitMissionStatus`.

## 5. Briefing text: markup, blinking words and pages

**Reference.** The mission's brief is wrapped to the region and replaced by a copy in which
**`&` toggles a colour span and the next character is its code**; at a newline inside a span the
function closes and re-opens the markup across the CRLF it inserts. The text region is drawn a
page at a time: **lines per page = region height divided by (font height + 2)**; each line is a
label at five pixels in, stepping by font height plus 2, in the side's font (`ARMFONT` or
`COREFONT`) and **base colour**. Spans coloured `R`, `Y` or `G` are drawn through a **blink
word** registered with period 0x5e, duration 1.0 and phase 0.25 (at most **fifteen** per
briefing), using a four-entry colour table **per side** at offsets three, two and one; any other
code uses offset three. The bar under the region reads empty for a single page, `MORE...` when
another page follows and `BACK TO START` on a later last page; a click turns the page (the More
sound) and wraps to the first. A short last page can read past the text's end, and copy lengths
and span lengths are unchecked.

**RWE.**

- **Colour markup: MATCHES in effect.** `UiTextRegion::parseMarkup` (`ui/UiTextRegion.cpp` about
  39-70) reads `&Y`, `&R` and `&G` as yellow, red and green and a bare `&` as the base colour.
  Two differences: the colours are **fixed RGB** (255,255,0 / 255,64,64 / 64,255,64) where the
  original's are the **side's own table** (Arm and Core briefings are not coloured alike); and
  the code strips every `*` ("its meaning not decoded", `UiTextRegion.h`), which the chapter does
  not mention.
- **Blinking: MISSING.** The coloured spans are static. In the original they pulse on the
  period above.
- **Pagination: DIFFERS.** `render` (about 125) uses a fixed line height of **15** pixels
  (`LineHeight`) and `region height / 15` lines a page; the original uses the font's height
  plus 2, which depends on the face. Whether the two agree for `ARMFONT` and `COREFONT` was not
  measured. **UNCLEAR**; a page break in a long brief could fall in a different place.
- **The `MORE...` / `BACK TO START` bar: MISSING.** A click on the region turns the page and
  wraps (`nextPage`, about 29-37) with no text on the bar and no More sound.
- **Wrapping: DIFFERS in method, UNCLEAR in result.** RWE wraps at word boundaries to the
  region's width in the face it draws with; the original also wraps in its own function and
  re-wraps for the colour spans. A brief that wraps differently would also paginate differently.
- **The fifteen-blink-word and unchecked-copy limits: not applicable.**

## 6. Briefing artwork: the planet table, the panorama and the readouts

**Reference.** The background is `mbrief` plus the side's name prefix. The planet's name selects
a screen GAF, a panorama sequence and a rotation sequence from a table of fifteen rows
(green planet, archipelago, wet desert, desert, lava, red planet, lunar, metal, lunar2, ice,
lush, slate, water world, acid, crystal), by an **exact, case-sensitive** compare; an unknown
name takes the first row; **Lunar on the Core side becomes Lunar2**. The initial wind is a
random value between the mission's minimum and maximum, and a per-call countdown redraws it by
a random step of `-2..+2`, clamped to the limits, with a countdown of 0 to 62. The SOLARSYSTEM
gadget prints the translated **Wind Speed** and **Gravity** (gravity scaled by 1/112). The
panorama scrolls: all frames are laid in a ring, with one extra first tile and a **side
mask** over it. BT-267 refines the timing: one pixel when the 30Hz wall-clock counter is
strictly past its deadline, then a new deadline at current tick plus two, without catch-up.
Offset and deadline persist across briefing entry; wrapping uses the sum of frame widths. The planet
advances a frame when the millisecond deadline (25 ms) has passed and the tick differs from the
last (the last-tick field is never updated, so the second test is always true).

**RWE.**

- **The table: MATCHES.** `briefingPlanet` (`MainMenuScene_campaign.cpp` about 43-75) holds the
  fifteen rows with the same GAF, panorama and rotation names; the three entries whose case
  differs from the chapter's (`ICEBRIEF`, `Icerotate`, `Lushrotate`, ...) are the GAFs' own
  (`getGafEntryInternal` upper-cases the entry name, `TextureService.cpp` about 100). An unknown
  planet is row one, Lunar becomes Lunar2 for Core. **Case: DIFFERS for mod data**: RWE
  upper-cases the planet's name before the lookup; the original does not.
- **Wind and gravity readouts: MISSING.** The comment at about 376 names them as the
  original's and does not draw them. A mission's `minwindspeed`, `maxwindspeed` and `gravity`
  are not shown. The register's section 115 describes them; the chapter gives the numbers
  (the `-2..+2` step, the 0 to 62 countdown, the 1/112 scale).
- **The panorama: implemented from BT-267 (2026-10-19).** RWE scrolls every native-width
  strip and one extra first strip beneath the side mask. The refined finding establishes
  strict wall-clock deadlines, single-step updates and persistent offset, superseding the
  earlier two-game-tick shorthand. See `agents/factory-death-and-briefing.md` for evidence
  and validation; no original runtime screen comparison has been run.
- **The planet's speed: UNCLEAR.** `UiAnimation` runs the rotation at **15 frames a second**
  (about 380). The original advances at most every 25 ms and, because its last-tick latch is
  never written, once per call after the deadline — roughly 40 a second if the callback runs
  that often, or one per game tick (30) if it is tick-limited. The chapter does not settle it,
  and a faster or slower globe is a matter for a play comparison.
- **The background and fonts: MATCHES.** `mbrief` plus the side's name prefix and the side's
  font by index (about 351-360).

## 7. Briefing activation and narration

**Reference.** Start plays a sound and requires the disc; on success it registers archives,
stops the narration, blanks and requests the game start. SHUTUP plays a sound, then stops the
stream when the button's stage is zero or restarts it at delay 0x3c when not, outside the
wider state six. PrevMenu stops the narration and goes back. TextRegion and MOREBAR turn the
page. The narration is requested for the mission's name slot three, the glamour sound is slot
eight, both with delay 0x3c.

**RWE: MATCHES in effect.** `goToCampaignBriefing` (about 344) starts the narration when the
screen opens (`startNarration`, about 425) and sets SHUTUP to stage one; `campaignBriefingMessage`
(about 441) toggles it by whether a channel is playing rather than by the button's stage;
`messageNow` stops it on PrevMenu; `launchBriefedGame` stops it and starts the game. The delay of
0x3c (sixty units of the stream's clock) is not reproduced; the chapter does not give the unit.
**UNCLEAR, no effect** for a two-second-at-most lead.

## 8. The setup-slot queries

**Reference.** Seven small functions over the 24-byte setup records: count computers (type two),
count humans (type one), find the first open slot, whether every slot is empty (also true for
zero slots), whether a colour is taken (active slots only, skipping one index), find a free colour
(first of 0 to 9 absent from **every configured record, open ones included**, else minus one) and
count the players in an allegiance group (active only).

**RWE: MATCHES, with the difference of rule 3 of the skirmish audit.** `MainMenuModel`
(`MainMenuModel.cpp`) has `isTeamShared` (members of a team), `isColorInUse` and `getFirstFreeColor`.
`isColorInUse` skips open seats, which is the chapter's `IsColorTaken`; `getFirstFreeColor` uses
the same test, where the chapter's free-colour function ignores the open/active distinction. See
`BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 3. Computers and humans are counted inline where
needed (`togglePlayer`, `startBlocker`).

---

## 9. Gaps to fix

Ranked by what a player would notice. All presentation; none touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **No wind and gravity readout on the briefing**; panorama scrolling is implemented from BT-267. The chapter gives the numbers for the readout (random start in the mission's range, a step of -2..+2, countdown 0 to 62, gravity over 112). | MISSING | code: a label pair and a scroll offset in `goToCampaignBriefing`; presentation-side randomness (`effectsRng`) |
| 2 | **Pagination and the MORE bar**: fixed 15-pixel lines against font height plus 2, no `MORE...` / `BACK TO START` text, no More sound. | DIFFERS / MISSING | measure the two faces' heights; then wire the bar |
| 3 | **Colour spans don't blink and are not the side's colours.** | MISSING / DIFFERS | the side colour table is in the GUI/palette data; a blink is a presentation timer |
| 4 | **A loose `camps/EXAMPLE.TDF` in the retail install appears in RWE's Arm list.** Whether the original lists it is the loose-file rule. | UNCLEAR | original check: open the new-campaign screen with the loose file present |
| 5 | **Which new-game layout NewCamp shows in the original** (the chapter's two modes against the register's "argument 1, the other is dead"). | UNCLEAR, chapter against register | original check: click New Campaign; a campaign list means the register is right |
| 6 | **`AllMissions` / `DRDEATH` / AnyMsn**: no effect on gameplay. | MISSING, no consequence | close the settings audit's open row |
| 7 | **Case sensitivity of `campaignside` and `planet`** (mods only). | DIFFERS | none |
| 8 | **Planet animation rate** (15 a second against 25 ms or one per tick). | UNCLEAR | side-by-side play comparison |

No source file was changed.
