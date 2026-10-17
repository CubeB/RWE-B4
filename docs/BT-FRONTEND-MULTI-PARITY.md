# Multiplayer connections, battle room, alliances and unit restrictions parity audit

Spec-room finding, 2026-10-17. Like `BT-FRONTEND-SKIRMISH-MENU-PARITY.md`, `BT-FRONTEND-FRONTEND-PARITY.md`
and `BT-NETWORK-NET-CHAT-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `frontend-multi` (95 annotations in `0x440d70..0x44ce40`): the multiplayer
front end -- the provider, address, session-browser and new-game screens, the score-service choice,
the map and logo selectors, the battle room (`LOUNGE2.GUI`) with its player rows, rule buttons and
START validation, the alliance and control dialogs, the unit-restriction editor with its `.LST`
files, and the end-of-multiplayer screen. It coordinates GUI and shared match setup; the transport
and the packets belong to `network-hapinet`, `network-packets` and `network-net-game`
(`BT-NETWORK-HAPINET-PARITY.md`, `BT-NETWORK-PACKETS-PARITY.md`, `BT-NETWORK-NET-GAME-PARITY.md`). This
is the finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\frontend-multi.md`). Nothing else
  under that tree was opened beyond the two sibling chapters audited in the same batch
  (`network-net-game`, `debug-debug-lib`): no source, no decompilation, and none of the source links in
  the prose were followed. Addresses are the ones the chapter names; rules are restated in prose.
- **Evidence class.** A **static reading** by that project: the chapter says provider, DLL, COM
  behaviour, assets, translations and malformed files were not executed. Weaker than
  `TOTALA-EXE*.md` and than `TA-NETWORK.md`'s captures. Disagreements are marked **CONFLICT** with no
  winner picked.
- **The RWE half** is a read-only audit of two things, as the task asked. (1) **RWE's own lobby**:
  `MainMenuScene_multiplayer.{h,cpp}` (the four panels, the game list, the battle room, the claim
  protocol), `MultiplayerSetup.cpp`, `MainMenuModel.h`, `MainMenuScene.cpp` `prepareGameParameters`,
  and `GameScene_peers.cpp` for what the lobby hands on. The lobby transport (LAN discovery, STUN,
  invite codes) is RWE-original and is audited only for what it shows a player. (2) **The TA-host
  path**: `TaHostLobbyScene.*`, `net/ta/TaBattleroom.*`, `net/ta/TaHostGame.*`,
  `net/ta/TaHostSession.cpp` and `docs/TA-NETWORK.md`, which is an executed-against-real-TA record
  that outranks a static reading. The launcher is not audited. Nothing was built, run or changed.
  Another agent had uncommitted edits in the tree, so every line number is "about".
- **The lockstep rule.** RWE is lockstep and the original is owner-authoritative. A difference that
  follows from lockstep by design is **DIVERGENCE (documented)** only when `TOTALA-EXE.md` section 88 or
  `docs/compatibility.md` records it; neither document mentions the multiplayer lobby, the battle
  room or lockstep at all (searched 2026-10-17), so every such difference below is marked **UNCLEAR**
  and the reason given. The code comment in `makeBattleroomPanel` (about 1455-1480) is the only place
  most of them are written down.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. Shared state: slots, player types, the match record
3. Providers and the connection choice
4. TCP, serial and modem screens
5. Session discovery and the game list
6. Joining: the preview gates and the final checks
7. The new-game dialog
8. Score service choice (not applicable)
9. Map selection and preview
10. Logo (colour) selection
11. Player rows, sliders and rule stages
12. Observer control, rejection and the control dialog
13. Alliances and the ALLIES dialog
14. Opening and refreshing the battle room
15. The battle room's update loop and the START animation
16. Battle-room actions
17. START validation
18. Unit restrictions
19. `.LST` persistence and the list dialogs
20. The end-of-multiplayer screen
21. The original's defects (not reproduced)
22. The TA-host path, rule by rule
23. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| route into a match, state dispatch | `BT-FRONTEND-FRONTEND-PARITY.md` rule 3 ("not traced here", names the four screens) | rules 3 to 7 |
| the START checks | `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 7 (skirmish half); BT-194 (all-on-one-team refusal, cited in `MainMenuScene.cpp` about 1960) | rule 17 |
| alliances, team icons | `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 4; `BT-GAME-PLAYERS-PARITY.md` rule 1 ("documented alliance divergence") | rule 13 |
| unit restrictions, `.LST` | `BT-NETWORK-UNIT-SYNC-PARITY.md` rule 6 (the wire half) | rules 18, 19 |
| battle room option buttons | `TA-NETWORK.md` "The options byte" | rules 16, 22 |
| score reporting | `BT-GAME-STATISTICS-PARITY.md` | rule 8 |
| end of multiplayer | `BT-FRONTEND-ENDGAME-PARITY.md` rule 7 (branches RWE does not have) | rule 20 |

## 2. Shared state: slots, player types, the match record

**Reference.** The front end uses one global layer stack, one shared mission object, **eleven physical
player slots with ten ordinary playing indices** (the eleventh is a spare that routines reach by
renumbering with sentinel ten; several local views declare ten), connection and provider buffers and
one unit-synchronisation object. Player type: **0 empty, 1 local human, 2 local computer, 3 remote,
4 blocked**; a remote record's kind distinguishes human (1) from computer (2). Active, type, the index
sentinel, the observer bit and the living-unit tests are separate conditions reused in different
combinations. The 185-byte player record and the advertised session carry: resources as unsigned
shorts in **hundreds**, width and height shorts, a max-units short, separate host, ready, observer
and started flags, a commander policy, mapping and line-of-sight modes, a watch permission, a cheat
permission, fixed starts and a closed-admission flag.

- **Ten seats: MATCHES.** `MainMenuModel::players` is a `std::array<PlayerSettings, 10>`
  (`MainMenuModel.h` about 127); the eleventh slot is an array-extent quirk of the original that
  `BT-GAME-PLAYERS-PARITY.md` rule 2 already says not to port.
- **Seat types: DIFFERS.** RWE has Open, Human, Computer and Network (`MainMenuModel.h` about 40-58)
  where the original has empty, local human, local computer, remote and **blocked**. Open and Network
  are the lobby's empty and remote; a **blocked** seat (the host closing a slot so that nobody can take
  it and the session's capacity falls) has no counterpart: an Open seat is always on offer to a joiner
  (`firstOpenSeat`, `MainMenuScene_multiplayer.h` about 118-125). Kind: lobby control; see rule 16.
- **The rule fields in the match record: DIFFERS.** RWE announces four option stages and the
  difficulty (`optionsFromModel`, `MainMenuScene_multiplayer.h` about 40-55: commander, start
  location, mapping, line of sight, difficulty) and each seat's starting metal and energy. It has no
  **max units**, **screen width and height**, **watch permission**, **cheat permission**, **closed
  admission** or **ping limit** field. The omissions are deliberate and written only in a code comment
  (rule 14); lockstep makes a per-machine screen size meaningless and a cheat permission is not
  needed while RWE has no human cheats (`BT-NEW-CONFLICTS.md` item 133). Not recorded in section 88 or
  `docs/compatibility.md`, so **UNCLEAR** as a recorded divergence.
- **Resources in hundreds: MATCHES in effect.** The wire carries metal and energy per seat as whole
  numbers (`DiscoveredSlot::metal`, `energy`); the setup ladder steps in 500s (rule 11).

## 3. Providers and the connection choice

**Reference.** `FillProviderList` replaces an existing `SELPROV.GUI`, uploads the palette, opens a
fresh layer, allocates provider descriptions (1280 bytes), ten GUIDs (160 bytes) and ten connection
descriptors (80 bytes), refreshes multiplayer map discovery, uninitialises the old transport and asks
HAPINET to enumerate providers. The enumeration has no caller-capacity parameter. It then asks
`online.dll` for service links and clones the `SERVICEX` template into dynamically named `SERVICE`
buttons (the clone helper has no gadget-capacity check; the link loop can scan past the reported
count). `HandleSelectProviderClick` selects the provider index, requests substate two and plays a
button sound whatever `SelectConnection` returned; a negative index consumes a stored preference
(TCP/IP for one, IPX two, modem three, serial four, then resets it to zero). A second, **different**
enumeration (`GetServiceProviderIndex`) numbers modem 0, TCP/IP 1, IPX 2, serial 3, other 4 and must
not be confused with the preference mapping. A compound DirectPlay address is built from up to three
elements (the provider GUID, then phone data for a modem, the address string for TCP, or the 20-byte
COM record for serial); it is probed for size and allocated only on the exact buffer-too-small result.

- **One connection, already selected: UNCLEAR (lockstep-by-design, unrecorded).**
  `makeSelectConnectionPanel` (about 1320-1337) builds `SELPROV.GUI` over `selconnect2` with a single
  connection, "Internet TCP/IP", selected; the header comment of `MainMenuScene_multiplayer.h` says
  so. There is no IPX, modem or serial provider and no `SERVICEx` links. Section 88 and
  `compatibility.md` do not record the absence; it is a transport decision (RWE's lockstep transport
  is UDP with STUN and invite codes), not a gameplay one.
- **`online.dll` service links: not applicable.** RWE reports through its launcher and master server
  (`BT-NETWORK-UNIT-SYNC-PARITY.md` rule 7, `BT-GAME-STATISTICS-PARITY.md`).
- **Compound address, preference mapping, descriptor allocation: not applicable.** RWE has one
  transport and no DirectPlay interface (`BT-NETWORK-HAPINET-PARITY.md`). The TA-host path *is* a
  DirectPlay TCP/IP provider on the wire (`net/ta/TaDirectPlay.*`); it answers the enumeration a
  joining TA sends and nothing in this rule is its concern.

## 4. TCP, serial and modem screens

**Reference.** `OpenTcpDialog` opens `TCP.GUI` and fills `ADDRESS` from the command-line override or
up to 128 registry bytes (`TCPADDR`). The click handler uses two address-mode bits: **OK or ADDRESS
activation sets both** (host), **JOIN sets only bit one**; a command-line address takes the connect
path and clears the override. The address is **written back to the registry even when the compound
address could not be built**. The host path blanks the screen; JOIN does not. `PREV` requests state
fifteen. The serial dialog binds four COM ports and six baud rates (115200 down to 9600), restores
`SERPORT` and `SERBAUD` and indexes its table with any non-negative gadget value. The modem dialog
creates a temporary modem DirectPlay interface, enumerates address chunks into a 200-byte name buffer
(a name helper that only advances past the first string, so more than two names overwrite the second
while the count keeps rising), keeps twenty 258-byte saved phone accounts in a 5160-byte registry
block (`MODEMNUMBERS`), moves a selected account to the front on save and builds the address from the
**first** name rather than the selection.

- **TCP address dialog: DIFFERS.** `makeTcpAddressPanel` (about 1339-1371) keeps `TCP.GUI` with OK
  and PREV only (no HOST/JOIN pair; the file's `crdefault=HOST` is mapped to OK) and its prompt
  "Enter TCP address (leave blank to search)" is the original's sentence. A typed address is probed on
  the discovery port 1338 unless it carries one, and an invite code may be pasted instead. The
  persistence of the address between sessions (`TCPADDR`) was not traced: `rememberedNickname`
  survives a visit but is a scene member (`MainMenuScene_multiplayer.cpp` about 2163), not a saved
  setting. **UNCLEAR** whether the address is remembered.
- **Serial and modem dialogs: not applicable.** Hardware transports RWE does not offer. The
  original's defects there (unchecked indices, name overwrite) are not reproduced.

## 5. Session discovery and the game list

**Reference.** `OpenSelectGameDialog` opens `SELGAME.GUI`, allocates **twenty** 84-byte session
descriptions, twenty 185-byte shared-data slots and fifteen 2560-byte packed column buffers, installs
the selection callback on the type-two list gadgets, restores nickname and password, discovers
sessions and focuses the list. A discovery failure closes the layer and says "Invalid TCP/IP Address"
whatever the cause. `ConnectToGame` shows "Updating" (TCP/IP, IPX) or "Connecting" with an Escape
notice, enumerates, and clears all fifteen column buffers. For each session it formats: packed name,
**occupancy over maximum**, a trimmed and translated map name, a **status**, a memory figure, **metal
and energy times one hundred**, **ping limit**, **commander policy**, **map shading** and **command
allowance**. The status is **VER** when the advertised version is below the local one, otherwise
**Lock**, **Play** or **Open** from the advertised flags. No local twenty-session cap is applied to
the formatting.

- **Ten columns: MATCHES in count, DIFFERS in content.** `makeSelectGamePanel` (about 1385-1425)
  keeps the file's columns (`GAMENAME`, `MAPNAME`, `COMMANDER`, `PING`, `LOS`, `FULLMAP`, `METAL`,
  `ENERGY`, `PLAYERS`, `STATUS`; the list at about 1205) and fills them from `gameListRow`
  (about 1056-1133): game name (the label, else "<host>'s game", else the map), map, Cmdr from the
  commander option, **ping "-"** always, LOS as `Prm`/`Tru`/`Cir`, mapping Yes/No, the **host's own
  seat's** metal and energy (the original advertises one rule value; RWE's seats can differ), people
  over non-computer seats, and a status of **Play** (launching), **Open** (an open seat exists) or
  **Full**. The original has no "Full" because its capacity is the player count; RWE derives it from
  seats.
- **Status VER and Lock: MISSING.** An incompatible build is refused with a message at Join
  (rule 6), not shown in the list; there is no lock flag (rule 16, GAMEOPEN).
- **Ping limit column: MISSING.** The column holds a dash; RWE advertises no limit and measures no
  lobby ping (rule 14).
- **Occupancy and the twenty-session buffer: DIFFERS, harmless.** RWE builds as many rows as it
  hears (`gameListRow` per game, about 1993) with no fixed cap; the original's twenty-entry buffer is
  a defect there.
- **Failure text: DIFFERS.** RWE says "No games found on this network. Press New to host one." (the
  status line, widened at about 1380-1383) rather than "Invalid TCP/IP Address" for every failure.
  A presentation improvement.

## 6. Joining: the preview gates and the final checks

**Reference.** Two layers. The **preview** (`UpdateGameSelection`) copies sixteen advertised bytes
and the 185-byte record into a stack message that includes unrelated stack bytes, and sets the
WATCH and JOIN disable state from: empty list, advertised version below local, and the playing,
watch and lock flags; the password gadget is shown from a bit of the group. The **final checks**
(`HandleSelectGameClick`): UPDATE saves the password and re-enumerates; WATCH, JOINGAME or a
type-two activation copies the selected description into the current game and requires the
version to be **exactly equal** to the local one, so a *newer* game can look eligible in the
preview and fail here. **Lock `0x8000` or playing `0x10` rejects even WATCH.** It validates a
non-empty nickname, copies the password, may mark battle-active from an existing started host state,
and sets the observer bit and requests substate nineteen for WATCH, otherwise clears it and
requests eighteen. `STARTNEW` clears the observer bit, copies the nickname and opens creation.

- **Non-empty nickname: MATCHES.** `joinSelectedGame` (about 2095): "You must enter your name".
- **A started game cannot be joined: MATCHES.** "That game has already started" when the lobby's
  `launching` flag is set (about 2123); the flag is RWE's playing bit.
- **Version must match exactly: MATCHES in effect.** `buildsCompatible(flow.discovery.buildIdentity(),
  game.build)` (about 2130): another build is another simulation, said in a box rather than as a
  desync at tick one. The original compares a version pair; RWE compares the commit
  (`BuildIdentity`, `CLAUDE.md`, "net/"). The game list shows no VER status (rule 5).
- **Password: MATCHES.** `passwordAccepted` (about 1136) compares a hash; "Incorrect password".
- **The map must be present: DIFFERS.** `sceneContext.vfs->readFile("maps/<map>.ota")` (about 2150)
  checks that a file of that name exists. The original's battle room (rule 14) compares the host's
  **map checksum** for hosts at version 1.2 or later and blinks the map name red when it differs. A
  same-named map with different content joins here and plays a different game: in a lockstep game
  that is a desync. The launcher's archive-hash check (`common/archives.ts`) covers launcher-started
  games; a direct LAN join does not pass through it. **Kind: robustness, hashed-state-adjacent.**
- **Watching: MISSING.** `makeSelectGamePanel` removes the `WATCH` button
  (`removeChildrenNamed("WATCH")`, about 1393); there is no observer join. Written as a code
  comment ("Spectating needs a join RWE does not have"); not in section 88. **UNCLEAR** as a recorded
  divergence. RWE's own spectating is replay and demo playback (`CONTEXT.md`, "Playback").
- **Lock flag: MISSING** (rule 16).
- **The preview's stack-byte copy and the exact-equals/newer-eligible asymmetry: not applicable.**
  The original's defects.
- **A seat must be open: MATCHES in spirit.** "The game is full" when `firstOpenSeat` finds none
  (about 2170); the original reports reason five from the host (`BT-NETWORK-NET-GAME-PARITY.md` rule 7).

## 7. The new-game dialog

**Reference.** `OpenNewMultiDialog` restores the nickname from the online configuration or the
Windows user name and sets name limits **sixteen** and password ten. `HandleNewMultiClick` advances
text focus, copies the password, rejects an empty game name or nickname and copies both into shared
**seventeen-byte** fields (so sixteen characters and a terminator) using stack copies that are
unbounded despite the intended limits. It then starts the score-report selection and, if none opens,
requests substate seventeen. `CANCEL` returns to browsing.

- **The two sentences: MATCHES.** `confirmNewGame` (about 2233): "You must enter your name", "You must
  enter a game name", in the original's order.
- **Limits: DIFFERS.** The file's own `maxchars` are used: game name 63, nickname **10**, password
  10 (read from `NEWMULTI.GUI` with `tools/hpi.py`, 2026-10-17). The reference's code sets sixteen
  for the names, which overrides the file. `SELGAME`'s nickname box is clamped to the **wire** bound
  of 20 (`maxPlayerNameLength`, about 1395-1401), so a 15-character name typed on the list is cut to
  ten when it is carried to the new-game box (`rememberedNickname.substr(0, getMaxLength())`, about
  2227). No fault; the three limits (10, 16 and 20) disagree. Kind: presentation.
- **Game name length on the wire: UNCLEAR.** A 63-character label is not obviously within the
  announcement's bounds (`DiscoveredGame::label`); `maxHostNameLength` is 20 and the label's bound
  was not found. Settled by reading `LanDiscovery`'s encoder.
- **Windows user-name default: MISSING.** An empty nickname box stays empty. Cosmetic.

## 8. Score service choice (not applicable)

**Reference.** `InitScoreReporting` runs only for game type three, loads the reporter DLL, opens
`REPORT.GUI` when the service count is positive (result four is an allowed no-report path), and
`HandleReportClick` builds a powers-of-two mask from up to sixteen `CHK` controls without bounding
the opener's count to sixteen.

**RWE: not applicable.** No ladder and no `reporter.dll` (`BT-GAME-STATISTICS-PARITY.md`,
`BT-NETWORK-UNIT-SYNC-PARITY.md` rule 7). Nothing to port.

## 9. Map selection and preview

**Reference.** `ShowSelectedMapInfo` sets the translated name, size and player text and description,
frees a previous `MAPPIC` and scales the radar art. `UpdateMapSelection` loads the selected mission
**immediately into the shared mission object**, so previewing changes mission state before
acceptance; `HandleMapSelectClick` accepts, loads **without checking success**, copies map and
checksum into the local player record, broadcasts, reports event five and updates the session. It
**clears readiness** for inactive slots and for slots that are neither local human nor local
computer. `PREVMENU` reloads the old name and broadcasts. `OpenMultiMapSelector` allocates a 200-byte
old-name buffer before checking that a mission name exists and leaks it on the "no maps" path.

- **Preview is separate from acceptance: DIVERGENCE (improvement), unrecorded.** RWE reads the OTA
  for the preview only (`setCandidateSelectedMap`) and commits on accept (`commitSelectedMap`,
  `resetCandidateSelectedMap`); `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 6 already says so. In
  a lockstep room it matters more: a preview that changed mission state would change what a guest is
  told. **UNCLEAR** as a recorded divergence (not in section 88).
- **Accepting a map clears readiness: DIFFERS.** In RWE a guest's ready flag is the guest's own
  announced choice (`flow.myReady`, `flow.seatReady`); nothing resets it when the host changes the
  map or any rule (`resetRoom` at about 1760-1776 runs only on entering a room; the only other
  writes are the guest's own toggle at about 2850 and the host reading the claim). The host can
  therefore change the map or the rules **under a guest who had readied** and start at once
  (`startBlocker` asks only that every network seat is ready). The original clears readiness on
  every such change. Player-visible; see gap 3.
- **A host's map the guest lacks: DIFFERS.** The guest is sent back to the game list with "You do not
  have the map <name>" (about 3140-3146). The original keeps the guest in the room, blinks the map
  name red and clears its readiness; either way the guest cannot start.
- **The map list: UNCLEAR.** RWE lists only maps with a multiplayer schema
  (`BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 6, `getMapNames`); the multiplayer selector's filters in
  the original are not stated by this chapter either ("refreshes maps").

## 10. Logo (colour) selection

**Reference.** `OpenLogoSelectDialog` allocates a 32-byte layout, an animation-pointer list and
copies of the logo sequences. A candidate `j` is **excluded if any player whose type is neither 0 nor
4 has a colour greater than or equal to `j`**, without testing `active`. The comparison is `>=`, not
equality, and the chapter says it "can suppress every index up to a used high logo". Available
indices are stored in the first 24 bytes of the layout without a separate bound. `HandleLogoSelectClick`
writes the chosen byte to the local colour, marks the record dirty and requests the colour (rule
`BT-NETWORK-NET-GAME-PARITY.md` rule 13 gives the host's final acceptance).

- **Candidate rule: DIFFERS, probably a fault of the reference's reading.** `ensureFreeColour`
  (about 583) and `colourHeldByAnotherSeat` (about 570) exclude a colour only when another seat
  **holds exactly that colour**, and `isColorInUse` counts non-open seats. The `>=` reading would
  leave a player with colour 9 present unable to choose anything. It is more likely a decompilation
  artefact (an inverted comparison, or a monotone index scan) than the intended rule, and the host's
  assignment routine, not this dialog, decides what is accepted. **UNCLEAR**; settled by a play-test
  of the logo picker with a high colour in use.
- **A dialog for logos: DIFFERS.** RWE cycles the colour on the row button
  (`BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 4) rather than opening a picker. Presentation.

## 11. Player rows, sliders and rule stages

**Reference.** `BuildPlayerSlotGadgets` resets the gadget count to the saved baseline and clones the
ordered template set for **ten rows**, replacing each template name's last character by the player
digit and offsetting y by **twenty** per row; position in the template decides permissions (local
name becomes text, others get `0x8000`; non-local logo and button state read-only; a selected
local/CPU control hidden). `SwapPlayerSlots` swaps complete player records, clears the source and
renumbers indices zero through ten using ten for invalid; `CompactActivePlayerSlots` keeps live and
blocked positions and moves later live players into holes. Sliders: **max units** is the slider plus
**twenty** (host or no host) and copied from the host's value otherwise, written to the local short
and broadcast when host; **metal and energy** round slider values down to hundreds, store units of
one hundred and for a local host broadcast and update the session. Binding a named slider invokes its
callback even for a missing index, so opening a slider can mutate the record and broadcast.
`UpdateBattleRoomFlags` maps host or local values to the button stages: commander direct, **mapping
inverted**, line of sight two when LOS is false and otherwise the inversion of the LOS type,
watching, cheating and fixed-location direct, **game-open inverted** (stored as closed).

- **Ten rows with per-seat controls: MATCHES in shape.** `makeBattleroomPanel` clones the
  template set per seat, 10 seats, naming controls `PLAYER3`, `PLAYER3_side` and so on
  (`parseSeatMessage`, `SeatControls`, `seatControls`, `MainMenuScene_multiplayer.h` about 205-260).
  What each role may touch is a function of role and seat type, as in the original.
- **Slot swap and compaction: MISSING, by design.** A seat's number is its position in the table
  and the host fills the first open one (rule 16); there is no compaction.
- **Resource slider: DIFFERS.** RWE's ladder is 21 stops: 200, 500, then steps of 500 to 10000
  (`resourceStopValue`, about 787-795; the same ladder as the setup screen's plus and minus,
  `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 5). The original's slider reaches **10001** at hundreds
  of resolution. The set of attainable amounts differs (700 or 1300 exist there and not here); the
  endpoints agree. Kind: presentation of a rule value.
- **Max units slider: MISSING.** Removed (`MAXUNITS`, `MAXUNITSTEXT`, about 1504); the simulation
  takes no per-game unit cap as a setting (`AiSeat::unitLimit` reads the map's `maxunits`, a map
  property). **UNCLEAR** as a recorded divergence. A unit cap is a rule both machines must share; in
  lockstep it would be a hashed setting, and no one has asked for it.
- **Stage mapping: MATCHES by design of its own.** `optionStageForLabel` and friends
  (`MainMenuScene_multiplayer.h` about 280-330) turn a button's stage into the model's **by its
  label**, because LOUNGE2's stage order differs from SKIRMISH's; the mapping inversions the chapter
  lists (mapping, game-open) are the same fact seen from the other side.

## 12. Observer control, rejection and the control dialog

**Reference.** `OpenRejectDialog` saves a global target, opens `YESORNO` and formats the name;
`HandleRejectChoice` YES calls `RejectPlayer(target DPID, reason one)` with **no local target bound or
host check in the handler itself**. `OpenControlDialog` refuses an observer local player, opens
`CONTROL` and lists other players. `HandleControlDialogClick`: player rows open the reject dialog,
`WATCHING` toggles permission and broadcasts, and `OK` updates the session and **rejects active remote
observers with reason nine** if permission is now off. It does not handle `GAMEOPEN` there.

- **Reject a player: DIFFERS.** The RWE host removes a guest by clicking the guest's name button
  (`SeatControls::name`: "the host opens a seat, or makes it a computer, or removes a guest"); the
  guest is put in `removedTokens` so it is not re-admitted (`updateHostedLobby`, about 2880-2920).
  There is no yes-or-no confirmation and no reason code. The original's reasons (closed, password,
  full, disconnected, missing unit, version, watching disallowed, creator left) are read by the
  kicked player (`BT-NETWORK-NET-GAME-PARITY.md` rule 7); RWE's guest sees a lost claim.
- **Observers and the control dialog: MISSING.** There are no observers (rule 6) and no in-game
  CONTROL (`BT-INGAME-DIALOGS-PARITY.md` rule 5).
- **A kick the handler does not authorise: not applicable.** In lockstep the host alone edits the
  lobby; a guest cannot send a reject.

## 13. Alliances and the ALLIES dialog

**Reference.** `ArePlayersAllied` compares alliance numbers **except sentinel five, which always means
no team**. `CountPlayersInAlliance` excludes five, requires active live types and index not ten, and
in battle also requires a non-zero unit count or none ever created. `AreAllPlayersInOneAlliance`
compares each team zero through four with the computer-plus-human total (an empty total can satisfy
zero equals zero). `SyncMutualAlliances` sets allied/alliedBy and the shared-victory flag for found
teammates including self and clears that flag when the counted alliance has fewer than two; it does not
first clear old relations. `CyclePlayerAlliance` clears the old team relations, advances **modulo six**,
broadcasts, synchronises and refreshes icons; `RefreshTeamIcons` maps a count of zero to stage ten, a
singleton to `alliance * 2 + 1`, a larger team to `alliance * 2`. In game, `OpenAlliesDialog`
(`ALLIES.GUI`) lets each player flip an **outgoing** relation to any other player and announce it as a
category-four chat line; `VICTORY` copies the button's low bit into the shared-victory flag and
broadcasts on change (`HandleAlliesClick`, `RebuildAllyList`, `RefreshAlliesScreen`).

- **Teams 0 to 4 and "none": MATCHES.** Six states, five meaning none: `teamIndex` is an optional
  integer with values 0 to 4 (`applyClaimChoice`, about 616-622, "anything outside the five teams is
  no team"); `BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 4.
- **Team icons: MATCHES except the lone-team frame.** `face = team * 2 + (shared ? 0 : 1)`, ten for
  none (about 2480-2491) -- the chapter's formula, with the lone team-five frame the skirmish audit
  already flags as UNCLEAR.
- **Symmetric, fixed-at-start alliances: UNCLEAR (lockstep-by-design, unrecorded).** The original's
  relations are **directional** and editable in play (the ALLIES dialog); RWE's are the lobby's team
  id, symmetric and fixed (`arePlayersAllied`, `GameSimulation_visibility.cpp` about 180-189).
  `BT-GAME-PLAYERS-PARITY.md` rule 1 and `BT-ORDERS-ORDER-DISPATCH-PARITY.md` rule 2 call this "the
  register's documented alliance divergence", but a search of `TOTALA-EXE.md` section 88 and
  `docs/compatibility.md` (2026-10-17) finds no entry about alliances. See `BT-NEW-CONFLICTS.md`
  Batch 15.
- **ALLIES dialog, shared-victory flag, announcements: MISSING.** `BT-INGAME-DIALOGS-PARITY.md` rule 5.
- **One-alliance test: MATCHES in effect.** `allOnOneTeamProblem` (`MultiplayerSetup.cpp` about 80)
  treats a seat on no team as a group of its own, so only seats that all name the same team trigger
  it; this is the chapter's "sentinel five means no team", and the same reading as `TA-NETWORK.md`
  ("The team a battleroom sends is 5").

## 14. Opening and refreshing the battle room

**Reference.** `OpenBattleRoom` resets room and player-id latches, stores the configured resolution and
CD bit, opens `LOUNGE2`, and captures its template count. The host initialises max units from the
configured limit; online configuration can override lock, resources, unit cap, commander rule, LOS,
cheat and watch permission, starts and mapping. Settings controls are grayed for non-host or locked
rooms. It allocates a 2560-byte chat buffer, creates the unit-sync object, installs the START
sync/ready gates, binds resource sliders to maximum **10001** and the unit count to maximum minus
twenty, refreshes map discovery, and (online host) may load a configured map. **No named map is a
fatal error.** `RefreshBattleRoomRows`: moves the chat read index by at most one when output exceeds
the visible line capacity; reflects rules and map; reloads the map presentation when it changed;
**checks that terrain exists and, for a host at version 1.2 or later, compares the host's map
checksum**, blinking the map colour and **clearing readiness and broadcasting** when the translated
name differs (legacy hosts accept existing terrain without a checksum); mirrors local readiness into
every local human and computer; **rejects computers when the commander policy is two**; removes
observer bits when the host disallows watching; synchronises team relations and icons; and rebuilds
ten rows. A row shows CD, logo, side or observer stage, two-way alliance, team, resolution, ping,
memory tier and ready; unused and blocked rows hide identity and status. **Only remote humans show a
numeric ping** (and an optional `:s` from unit sync); computers show `n/a`; the memory colours warn
against the map's tier even in the `n/a` branch. The host lowers the advertised **ping limit** to the
minimum displayed remote-human ping and never raises it when conditions worsen.

- **Rows: MATCHES in shape, DIFFERS in columns.** Per seat RWE shows the name button, side, colour,
  team and ready (`makeBattleroomPanel`, `refreshBattleroom`, about 2440-2520). Removed on purpose:
  `RESx`, `MEMx`, `CDx` (resolution, memory tier and CD present) because "nothing is known about
  another machine's" (the code comment at about 1473-1480); `SYNCHING`, `battlestart` (no unit-check
  step); `CHEATING`, `WATCHING`, `GAMEOPEN`, `MAXUNITS`, `RESTRICTIONS` (rules 11, 12, 16, 18). The
  comment is the only record. **UNCLEAR** as recorded divergence.
- **Ping column: MISSING.** RWE's ping label is a dash shown for any seat with a machine behind it
  ("No measurement exists on this transport", about 2517-2522). The F9 overlay measures round trips
  in game (`docs/NETWORK-OVERLAY.md`), not in the lobby. No ping limit is advertised or lowered.
- **Computers rejected by commander policy two: DIFFERS.** Policy two is the Deathmatch rule
  (`BT-GAME-VICTORY-PARITY.md`, "`CheckAlliedVictory` returns false whenever the commander-death mode
  is 2"); RWE has no Deathmatch stage -- `optionStageForLabel` maps the label to nothing and the
  screen builds the button without it (`MainMenuScene_multiplayer.h` about 275-285) -- so the
  question of computers in a Deathmatch cannot arise.
- **Map checksum comparison and readiness reset: DIFFERS** (rules 6 and 9).
- **"No named map" is fatal: not applicable.** RWE cannot reach the room without a map
  (`startBlocker`: "Select a map").
- **Chat read index, 2560-byte buffer: not applicable.** `ChatTranscript` keeps 200 lines
  (`maxKept`) and is not a ring (`BT-NETWORK-NET-CHAT-PARITY.md` is the HUD ring).

## 15. The battle room's update loop and the START animation

**Reference.** `UpdateBattleRoom` increments the room's tick **once per callback** (it is not wall
time), handles packets, marks the room dirty on changes or a bad map, and leaves on local rejection.
Dirty work builds or compacts slots and republishes the player count. Clients on `LOUNGE` copy the
host's mission and sliders; clients on `VIEWMAP` reload a changed host map. The host queries ready and
synced, animates `battlestart` and toggles START and SYNCHING. The animation advances one frame at its
deadline, adds four timer units, stops incrementing at frame eight and plays a panel sound at frame
four; ready starts frame one and pulses the START colour, **but the routine does not invoke START when
the animation finishes**, and losing ready does not reset the frame. After drawing the versions under
the logos it calls `ProcessSync` and, on a strictly passed heartbeat deadline, refreshes CD-present and
sends the heartbeat, setting the next deadline to `GetTicks + 60` -- **two seconds** at the usual
thirty ticks a second, not a minute as the source comment says.

- **START enabled by the same condition, no animation: DIFFERS.** `refreshBattleroom` enables Start
  when `role == Host && !blocker && !launching` (about 2582-2585) and the help line says why not. There
  is no frame animation and no SYNCHING overlay. Presentation.
- **Heartbeat interval: MATCHES in effect for the TA host.** `TaBattleroomConfig::keepaliveInterval`
  and `pingInterval` are 2000 ms (`TaBattleroom.h` about 190-193), the chapter's sixty display ticks
  (`BT-NETWORK-NET-GAME-PARITY.md` rule 14). RWE's own lobby announces on its discovery cadence.
- **Start does not follow the animation: not applicable.** Consistent with RWE, where Start is a
  click (`beginHostedLaunch`, about 2860).

## 16. Battle-room actions

**Reference** (`HandleBattleRoomClick`). `LOGO`: a local human or computer requests the next colour and
broadcasts. `PLAYER`: the host can **block an empty slot** (type four, id minus one, session capacity
reduced), unblock it, remove an aged local computer, prompt remote rejection, or **add a local
computer if the session is open, the commander rule is not two and no local computer exists**; "the
branch sequence can unblock then create in the same activation". `SIDE`: an observer becomes side zero,
otherwise the side increments and wraps; a wrapping local human may become an **observer** when the
host permits. `ALLY`: toggle the local outgoing alliance; leaving a same-numbered team breaks team
relations and sets sentinel five; the sound test has retained operator precedence so the outgoing byte
alone effectively selects the sound. `TEAMICONS`: cycle the team and broadcast it again after the
helper's own broadcast. `RES`: cycle the advertised resolution. `READY`: a local human **checks the
map and its checksum**, copies the gadget status into ready and mirrors it across local players;
failure resets the gadget only. `PREVMENU`: rejects every local human and computer with reason two and
requests state three. `MESSAGE`: the exact case-insensitive text `+syncerr` shows sync status;
otherwise sends lobby chat and **immediately flushes the packet-manager queues** when enabled.
`COMMANDER` cycles **zero or two**; `LOSTYPE` cycles true elevation, circular and no LOS through two
bits; `WATCHING` toggles permission and removes the local observer state if disabled; `CHEATING` and
`FIXEDLOC` toggle; `MAPPING` is the inverse selected stage; `GAMEOPEN` stores the inverse stage as
closed. Rule buttons broadcast and update the session description. `RESTRICTIONS` opens the nested
editor; `MAP` opens selection for the host and a read-only view for clients.

- **Rule buttons (commander, fixed location, mapping, line of sight): MATCHES.** `battleroomOptions()`
  (`MainMenuScene_multiplayer.cpp` about 776) names exactly these four and `nextOptionStage` advances
  the stage in the button's own order; `optionsFromModel` carries them. The Deathmatch stage is
  dropped (rule 14).
- **Add a computer: DIFFERS (superset).** The host cycles a seat Open, then Computer (Original TA, then
  RWE AI), then back (`cycleComputerSeat`, about 498-514), and may have any number of computers; the
  original allows **one local computer per machine** (the rule "no local computer exists"). The
  engine choice is RWE's own (ADR-0005).
- **Block a slot, unblock: MISSING.** See rule 2.
- **Side: DIFFERS.** A guest chooses Arm or Core for itself (`GuestChoice`); a third side, and the
  **observer** stage, do not exist (`BT-GAME-SIDE-DATA-PARITY.md` records the two-side limit; observers have no counterpart).
- **Ally and team buttons: MATCHES.** One control: the team (`SeatControls::team`), host for itself
  and computers, a guest for its own seat; the original's two controls (an outgoing alliance and a
  team) collapse into one because RWE's relations are symmetric (rule 13).
- **`RES`, `MEMx`, `CDx`: not applicable.**
- **`READY`: DIFFERS.** The guest toggles `myReady`, announced in its next claim (about 2850); nothing
  checks the map or its content at that moment (rule 6), and a host that changes the map leaves it
  set (rule 9).
- **`PREVMENU` rejects everyone local: DIFFERS.** A guest leaving simply stops claiming; the host's
  roster drops the seat after a timeout (`flow.hostSeenAt`, `claimedAt`); a host leaving ends the
  lobby announcement. No reject reasons are exchanged.
- **`+syncerr` and the immediate queue flush: MISSING, no action.** A developer command (the sync
  status text is `network-unit-sync` rule 5); RWE's lobby chat travels in the next announcement
  (`ChatTranscript`, `takeClaimChat`).
- **`GAMEOPEN`, `CHEATING`, `WATCHING`: MISSING.** Written only in the code comment at about 1463-1467:
  "RWE has no cheat codes, no spectator join, and a started game is already closed". The closed flag is
  equivalent to `launching` (rule 6). **UNCLEAR** as a recorded divergence.
- **`COMMANDER` cycles zero or two: UNCLEAR.** The chapter says the commander button cycles two values,
  zero and two; `TA-NETWORK.md`'s options byte has *three* named states (Game Ends, Deathmatch,
  Continues; "neither of the first two is Continues"). RWE's host-lobby button has two (game ends,
  continues). Which of the chapter's values is Continues is not stated (`BT-NEW-CONFLICTS.md` Batch 15).

## 17. START validation

**Reference.** START counts disc-bearing local humans and remote human-kind peers: **at least one** is
required, **two** for more than three humans and **three** for more than six; it **rejects a team that
contains the full computer-plus-human total**; checks that a mission name exists (missing: calls the
guarded map selector); if watchers are disallowed, **rejects remote observers with reason nine**;
requests substate seventeen; sets started; updates the session; copies local LOS, commander,
fixed-start and mapping into the runtime configuration; saves; **sets difficulty two**. "Ready and
catalogue synchronization are principally UI enable/visibility gates computed elsewhere; this callback
does not re-run both checks before its START branch." It does not load terrain or spawn commanders.

- **Disc count rule: not applicable.** `BT-GAME-CD-CHECK-PARITY.md`; the GOG install has no disc.
- **All players on one team refused: MATCHES.** `prepareGameParameters` runs `allOnOneTeamProblem`
  (`MainMenuScene.cpp` about 1960): "Can not start game with all players on the same team." The
  skirmish audit (`BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 7) found the skirmish path lacking this;
  the multiplayer path has it (BT-194).
- **At least one other person: DIFFERS.** `startBlocker` returns "Waiting for another player to join"
  when no seat is Network (`MainMenuScene_multiplayer.cpp` about 531-560); the original accepts one
  human alone in the room (with no computers and no team, nothing triggers a refusal). A solo game is
  RWE's skirmish screen.
- **Every network seat ready: MATCHES the UI gate, DIFFERS from the callback.** `startBlocker`
  returns "Waiting for <name> to be ready"; the original gates the button in the UI and the callback
  itself does not re-check.
- **Map chosen: MATCHES.** "Select a map" (the original opens the selector).
- **Start positions: DIFFERS.** `startPositionProblem` requires a position for each occupied seat's
  *slot number* (`BT-FRONTEND-SKIRMISH-MENU-PARITY.md` rule 7); the original deals positions to the
  participants in order (`BT-NETWORK-NET-GAME-PARITY.md` rule 16).
- **Watchers rejected, difficulty set to two, runtime copy: not applicable / by design.** There are
  no watchers; RWE's difficulty is an AI profile the host announces (`optionsFromModel`, `difficulty`).

## 18. Unit restrictions

**Reference.** `OpenUnitRestrictions` opens `RESTRICT2` and allocates shared flags, per-item frame
records (24 bytes), packed text budgeted 32 bytes per definition, **98-byte restriction rows** (82-byte
names), portrait pointers and old counts; it skips definition zero and flag-two bit fifteen, formats
internal name, translated description and costs into each row, queries unit sync and represents
unlimited as **101**. Rows are sorted case-sensitively; old counts stay in definition order for
cancellation. Twelve `SLIDER` controls (maximum 101) use themselves as callback data; a separate scroll
slider has maximum 210; `UpdateUnitSliders` invokes every slider callback, so merely **refreshing a
client's view can call the setter with its current value**. Only the host sees enabled Load, Save and
Reset, "but some handlers rely on this UI restriction rather than repeating authority checks". Counts
above 100 map to minus one ("No Limit"). Reset sets 100, or zero for definitions with a flag bit (a
finite limit, not unlimited). **Cancel** iterates the original definitions and restores old counts
where formerly unlimited was saved as **101**, so it does not restore the unlimited sentinel. Row
limits are edited **live before OK**. Cleanup (OK or Cancel) walks the rows and calls `DisallowUnit`
for zero limits or `AllowUnit` otherwise. Portraits are loaded one row index per call, two ticks apart.

**RWE: MISSING, by design.** `RESTRICTIONS` is removed from `LOUNGE2` (about 1504). There is no
build-restriction editor, no per-type limit and no catalogue to negotiate (`BT-NETWORK-UNIT-SYNC-PARITY.md`
rule 6). The TA-host path sends permission one and limit minus one for every id (`taBuildUnitSyncEcho`,
`TaBattleroom.cpp` about 215-250), the unrestricted default. The original's defects there (Cancel
leaves 101, live edits, the host-only check living in the UI) are not reproduced because the feature
does not exist. Kind: feature, multiplayer only; a decision, not a gap in conformance.

## 19. `.LST` persistence and the list dialogs

**Reference.** The `SaveGame`-named functions here handle restriction `.LST` files, not battle
snapshots. A file is a native little-endian count then pairs of definition FBI checksum and limit; the
save writes `unitDefCount - 1` as the count and skips definitions it cannot find, so it can write fewer
pairs than it declared; **neither side checks `fopen`, `fread`, `fwrite`, the count's range or a
truncated file**. `ListSaveGameFiles` enumerates `*.LST`, allocates names and descriptions at 256
bytes each and rebuilds without freeing the old arrays; the save and load dialogs strip extensions by
scanning backward for a dot without a start bound, and the load dialog's Cancel returns without freeing
the arrays or clearing the game flag. Neither workflow validates transactionally.

**RWE: not applicable.** No restrictions, so no `.LST`. A parser for the format, if one were ever
written, falls under the project rule that every count from a file is checked before it sizes
anything (`docs/SECURITY-AUDIT.md`) and would need a case in `io/malformed_input.test.cpp`. The
original's unchecked count is exactly the class of fault that audit lists.

## 20. The end-of-multiplayer screen

**Reference.** `OpenEndMultiScreen` blanks, loads `Mission02WinBW` and its palette (no fallback if
missing), draws and frees the surface, flips, opens `ENDMULTI` and labels **Victory** or **Failure**
from the endgame bit four. `OK` requests frontend state two and wider game mode one. Cleanup leaves the
network game **only when lobby UI flag four is set**; the outcome bit is a different field.

**RWE: MISSING.** A finished network game returns through the same end-of-game flow as a skirmish
(`BT-FRONTEND-ENDGAME-PARITY.md` rule 7 lists multiplayer among "branches RWE does not have"); there is
no `ENDMULTI` screen and no `Mission02WinBW` picture. The player sees the ordinary victory or defeat
banner and the statistics. Presentation; whether the black-and-white picture is worth a screen is a
decision.

## 21. The original's defects (not reproduced)

The chapter's last section and many of its sentences record defects: unbounded `strcpy` and
`sprintf` into fixed stack buffers (name fields, pipe-state labels, restriction rows), allocations used
before a null check (descriptor arrays, the 2560-byte chat buffer, the 200-byte old-map name), leaks on
the "no maps" and load-cancel paths, handlers that call a setter or broadcast when only refreshing,
unbounded indices (serial port and baud, `SERVICE` suffix, `SelectConnection`), a column count taken
from the session object rather than the returned count, a modem name overwrite and a stack-byte copy
in the preview.

**RWE: not applicable.** None is reproduced. Two lessons carry over: (1) the host in a lobby must bound
**everything a guest announces** (name, seat, side, colour, team, option values, chat), which RWE's
announcement parser does with fixed maxima (`LanDiscovery::maxOptionNameLength` 32,
`maxOptionValueLength` 64, `maxHostNameLength` 20, `maxChatTextLength` 80) and
`applyOptionsToModel`'s "a value out of its range is ignored"; (2) a screen that applies a refreshed
value through the same setter as an edit can write back what it only meant to show (the sliders), which
RWE avoids by computing the model from the announcement and never echoing it (`applyLobbyToModel`).

## 22. The TA-host path, rule by rule

A real `TotalA.exe` joins an RWE host (`rwe --ta-host`); the chapter's battle room is therefore also
the *joiner's* screen, and the RWE host stands in for the host's. What RWE's host supplies is
`TaHostLobbyScene` (an ImGui panel, not `LOUNGE2`), `TaBattleroom` and `TaHostSession`. Rules the
joiner's TA applies to what RWE sent are marked.

- **Rule buttons set on the command line, not in the room: DIFFERS.** `--side`, `--colour`, `--team`
  and `--options` set the host's own half and byte 157 once (`docs/TA-NETWORK.md`, "Hosting a game");
  there is no way to change the line of sight, mapping or the commander rule while a joiner is in the
  room, and the session description is re-sent only at launch (`sendSessionDescChanged`, twice).
  Settings-changed notices (`TA-NETWORK.md`: "the host sends SESSIONDESCCHANGED again whenever an
  option changes") never happen after the first.
- **The commander rule the host advertises and the one it plays: CONFLICT (code comment against
  reference and register).** The default options byte is `0x4F`, which sets `0x08`; the register names
  `0x08` and `0x10` "Game Ends and Deathmatch; neither is Continues" (`TA-NETWORK.md`, "The options
  byte"). `TaHostLobbyScene::startLoading` then sets `CommanderDeathMode::GameContinues` with the
  comment "TA has no commander-death rule: a player whose commander dies keeps playing" (about 265).
  The reference's battle room has a commander rule with at least two states and the victory chapter
  uses it (`BT-GAME-VICTORY-PARITY.md`), so a joiner shown "Game Ends" while the host plays
  "Continues" would see the two machines disagree about when the game is over. Which bit is which is
  not settled by either source; see `BT-NEW-CONFLICTS.md` Batch 15.
- **Mapping and line of sight from the byte: MATCHES.** `startLoading` maps bits `0x06` to the three
  LOS modes and `0x01` to unmapped (about 270-285), as the register's table has them.
- **Ready: DIFFERS, documented.** The host has no ready button; its state byte carries `0x20` as soon
  as someone has joined (`TA-NETWORK.md`; `TaBattleroom::grantColour`). The joiner's own READY is
  read (bit `0x20` of its state, `onReady`), and Launch does not wait for it unless `--auto-launch`
  (`autoLaunchIfReady`). The chapter's START does not re-check ready either (rule 17).
- **Team rule: MATCHES.** `launchRefusal` refuses when every player is on one real team
  (`taEveryPlayerOnOneTeam`; no peers: "nobody has joined"), and a team of 5 counts for nothing.
  The chapter's test is the same ("team five means no team").
- **Humans required: DIFFERS.** `launchRefusal` requires at least one peer; the chapter requires at
  least one human, which the host itself is.
- **Map: MATCHES the host's duty, not the joiner's check.** The checksum at bytes 170-173 is what a
  joiner looks up (`TaPlayerStatusMapChecksumOffset`, `TaHostLobbyScene::init`), the chapter's "host
  map checksum for version 1.2 or later". The reverse check (RWE comparing a joiner's map) is
  unnecessary: a joiner's `0x20` keeps naming its own last map (`TA-NETWORK.md`).
- **Battleroom chat: MISSING.** `TaBattleroom::handleAppData` has no `0x05` case; a TA joiner's lobby
  chat is dropped and RWE's host has no chat box in the panel. The chapter's `MESSAGE` sends lobby
  chat and flushes at once. See `BT-NETWORK-NET-CHAT-PARITY.md` rule 6 for the in-game half.
- **Reject, block and kick: MISSING.** The panel has Launch only.
- **Row details the joiner shows (CD, memory, ping, resolution): UNCLEAR.** What a TA joiner displays
  for RWE's row depends on the 185-byte record's fields RWE copies from a template
  (`TaBattleroom.h` about 100-130, "the fields a real host would fill in are not all identified").
  The chapter names the record's contents (resolution, memory, lobby flags, ping, resources, unit
  limit, version, map checksum) and could name the unidentified ranges; not attempted here.

## 23. Gaps to fix

Ranked by what a player or a TA peer would notice. Only item 7 touches hashed state, and only by
making a lobby change a game's inputs.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A host can change the map or any rule under a guest who has readied and start at once.** Readiness is the guest's own announced flag and is never cleared by a change; the original clears it on every map change and on a differing map name. | DIFFERS (player-visible, lockstep-adjacent) | code: clear `seatReady`/`myReady` for every network seat when the map, a rule option or a seat's controller changes; say so on the help line |
| 2 | **The RWE TA host advertises the options byte's commander bit (`0x08` by default) but plays Continues.** A joining TA is told Game Ends. | CONFLICT (code comment against register and reference) | spec-room read of the bit meanings; then either play what the byte says or send the byte for Continues |
| 3 | **Lobby changes after a join are not announced to a TA joiner.** The options byte can only be set on the command line and the description is re-sent at launch only; there is no battleroom chat and no ready button. | MISSING | code: re-send `SESSIONDESCCHANGED` and the status when an option changes; a chat case in `handleAppData` |
| 4 | **The map check is presence by name, not content, on a direct LAN join.** Same-named maps with different data join and desync. | DIFFERS (robustness) | code: send the map's checksum in the announcement and compare on join (the TA host already computes one, `taMapChecksum`) |
| 5 | **No blocked seat, no kick reasons, no observers.** The host cannot close a seat, and a removed guest is told nothing. | MISSING | decision (the multiplayer lobby is RWE-original) |
| 6 | **Fixed start positions follow the seat number.** The original deals positions to participants in order; RWE refuses a map with fewer positions than the highest occupied seat number (`BT-NETWORK-NET-GAME-PARITY.md` rule 16). | DIFFERS | settle with a play-test: three humans in seats 0, 1 and 5 on a four-position map |
| 7 | **No ping in the lobby, no ping limit.** The column is a dash. | MISSING | decision |
| 8 | **Missing rule fields: max units, watch, cheat, closed, screen size, restrictions, Deathmatch.** Removed with a code comment; none is in section 88 or `compatibility.md`. | UNCLEAR as a recorded divergence | add one paragraph to section 88 and `compatibility.md` ("the multiplayer lobby") |
| 9 | **The three name limits disagree** (NEWMULTI file 10, SELGAME 20, the reference's code 16). | DIFFERS, cosmetic | pick one; the wire bound is 20 |
| 10 | **A guest who lacks the host's new map is sent to the list** rather than kept in the room with the map name flagged. | DIFFERS | decision |
| 11 | **No `ENDMULTI` screen.** | MISSING, presentation | decision |
| 12 | **The reference's logo-candidate rule (`>=`) is probably a decompilation artefact.** | UNCLEAR | play-test the picker with a high colour in use |

No source file was changed.
