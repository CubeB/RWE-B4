# Network game lifecycle, protocol dispatch and shared match state parity audit

Spec-room finding, 2026-10-17. Like `BT-NETWORK-PACKETS-PARITY.md`, `BT-NETWORK-HAPINET-PARITY.md` and
`BT-NETWORK-NET-CHAT-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `network-net-game` (91 annotations in `0x44fd40..0x458160`): the layer above
the DirectPlay wrapper, the packet manager and the condenser -- player identity and the shared
185-byte player record, slot creation, joining and leaving a session, admission and rejection, the
protocol's framing table with its mode gates, the receive loop and its validation, the lobby control
messages (colour, alliance, team, start position, group assignment, loading progress), the live-unit
messages (creation, damage, death, weapon fire, feature actions, script calls, state bits, builder
links, sound, ownership transfer), removal and timeouts, heartbeat and ping, and the economy and
resource-sharing messages. This is the finding that implementation work cites for any of them, per
`AGENTS.md` and ADR-0004.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\network-net-game.md`). Nothing else
  under that tree was opened beyond the sibling chapters audited in the same batch
  (`frontend-multi`, `debug-debug-lib`): no source, no decompilation, and none of the source links in
  the prose were followed. Addresses are the ones the chapter names; rules are restated in prose.
- **Evidence class.** A **static reading** by that project: the chapter says no network session, game
  command or build was executed. Weaker than `TOTALA-EXE*.md` and than `TA-NETWORK.md`'s captures,
  which are an **executed-against-real-TA record and outrank a static reading**. Disagreements are
  marked **CONFLICT** with no winner picked.
- **The RWE half** is a read-only audit of the TA-compatible path -- `net/ta/` (`TaHostSession`,
  `TaHostGame`, `TaBattleroom`, `TaPacket`, `TaOutboundBatcher`), `TaHostLobbyScene`,
  `game/GameScene_tahost.cpp`, `puppet/TadPuppetDriver.cpp`, `sim/TaLiveSender.*`, `sim/TaWireTape.cpp`,
  `sim/MixedOwnership.cpp`, `io/tad/` and `docs/TA-NETWORK.md`, `docs/TA-DEMOS.md` -- and of the
  places where RWE's own **lockstep** multiplayer does the same job (`sim/ResourceSharing.*`,
  `game/GameScene_peers.cpp`, `game/PlayerCommandService.*`, `MainMenuScene_multiplayer.cpp`). Where
  the original is owner-authoritative and RWE lockstep, a difference that follows from that by design
  is **DIVERGENCE (documented)** only if `TOTALA-EXE.md` section 88 or `docs/compatibility.md` records
  it; neither mentions networking or lockstep (searched 2026-10-17, the one lockstep sentence being
  `TOTALA-EXE-COMMANDS.md` section 124's "consequences of lockstep"), so the rest are marked
  **UNCLEAR** with the reason given. Nothing was built, run or changed. Another agent had uncommitted
  edits in the tree, so every line number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **not applicable**.

## Index

1. Shape and what the register already holds
2. Identity, the shared record, lookups and counts
3. Names, slot creation and the advertised session
4. Initialization, routing and return values
5. The protocol framing table and its mode gates
6. The join worker, leaving and teardown
7. Admission, rejection and the reason texts
8. The receive loop and its validation
9. Lobby and control messages
10. Live units, construction and attachment
11. Weapons, features, scripts, state bits, sound and ownership
12. Directional alliances and numbered teams
13. Colours
14. Chat, heartbeat and the ping clocks
15. Removal, drop and timeout policy
16. Loading, start positions and the startup handshakes
17. Resource sharing
18. Economy snapshots and the view handshake
19. The original's defects (not reproduced)
20. Faults found in RWE
21. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| the 185-byte record, `0x20` | `TA-DEMOS.md` ("`0x20` is 186 bytes, not 192"); `TA-NETWORK.md` "The battleroom" | rule 2 explains the 186 |
| names, session info | `BT-NETWORK-HAPINET-PARITY.md` rules 5 and 8 | rule 3 supplies the 16 + 15 split |
| the framing table | `TA-DEMOS.md` subpacket table; `tad_util.cpp` | rule 5, one **CONFLICT** and four unsized codes |
| admission and reasons | none | rule 7 |
| receive validation | `BT-NETWORK-CONDENSER-STATS-PARITY.md` rule 5 (checksum not read) | rule 8 |
| lobby messages | `TA-NETWORK.md` "The battleroom" | rule 9 |
| live-unit messages | `TA-NETWORK.md` "In game", "Damage and death"; `TA-DEMOS.md` "Tier 2" | rules 10, 11 |
| colours | `TA-NETWORK.md` "A joiner is given its colour" | rule 13 |
| chat | `BT-NETWORK-NET-CHAT-PARITY.md` | rule 14 |
| heartbeat, ping, timeouts | `TA-NETWORK.md` "A silent host is offered for rejection" | rules 14, 15 |
| start positions, handshakes | `TA-NETWORK.md` "Launch" (`0x1e` "every capture says 1") | rule 16, **CONFLICT** on meaning |
| resource sharing | `TOTALA-EXE-COMMANDS.md` section 124 (the full account) | rule 17, **MATCHES** |
| economy snapshots | `TA-DEMOS.md` (`TadResourceStats`, "prefix ... field boundaries are not settled") | rule 18 |

## 2. Identity, the shared record, lookups and counts

**Reference.** Ten ordinary player slots and a spare physical eleventh at sentinel ten. Four
identities that are **not interchangeable**: a DirectPlay id (DPID), a slot index, an alliance (a
numbered team or sentinel five) and `lobbyDataSynced`, a machine-or-group assignment from one to
ten. Types: 1 local human, 2 local computer, 3 remote, 4 blocked, 0 empty; a remote record's kind
splits human from computer; most "local" operations accept both 1 and 2. The player record is
**packed, 185 bytes**: map and password, resolution, DPID, kind, side and logo, host and sharing
bits, memory, lobby rule, readiness and observer flags, password, shared-victory and CD flags, ping,
resources, unit limit, version and map checksum. It is copied **directly into packets**, so native
little-endian integers, IEEE float bit patterns, packed offsets and the 32-bit ABI are part of the
protocol, and padding bytes travel with it. Connection flag bit zero gates most activity; bit two
marks battle context; the wider frontend mode decides which packet modes are accepted. One shared
receive buffer is also the outgoing scratch buffer. The lookup helpers return `-1`, ten or null on
failure and are not complete validators (`GetPlayerDpid` rejects null and empty, `GetSlotDpid` rejects
ten and empty, neither checks an arbitrary index). `PickNewHost` selects the highest unsigned DPID of
the active humans and remotes, sets its host bit and always returns ten. `FindFreePlayerId` returns the
first unused `lobbyDataSynced` value one to ten (zero if exhausted). The counters differ on purpose:
`CountHumanPlayers` active local humans and remote kind-one; `CountComputerPlayers`; `CountLocalComputer`;
`CountRemotePlayers` all type three; `CountActiveAIPlayers` adds a valid index and "surviving or never
created"; `CountCombatPlayers` requires active, valid index, surviving, human and non-observer.

- **The `0x20` is 186 bytes because the record is 185 and the type byte is one: MATCHES, and
  explained.** `TA-DEMOS.md` ("`0x20` is 186 bytes, not 192") records 186 from the corpus and puts the
  192 down to the lobby status packet in the demo header; the chapter's 185-byte record plus the type
  byte is 186, and the 192 the older reference carried is the header form. `tadExpectedSubPacketSize`
  (`io/tad/tad_util.cpp` about 358-372) returns 186 for the wire. This confirms the register's
  correction from the other side.
- **Packed native layout on the wire: MATCHES in effect.** `taBuildPlayerStatus` writes six
  fields over a template and copies the rest (`TaBattleroom.h` about 80-130); the record's contents
  (resolution, memory, ping, resources, unit limit, version) are exactly the ranges the register calls
  "unidentified" (bytes 141-144, 152, 158-169, 174-185, `TA-DEMOS.md` D8). The chapter could name them;
  see gap 12.
- **DPID, slot, team and group are four different numbers: MATCHES.** RWE keeps them apart:
  `TaBattleroomPeer::playerId` (the DPID) is the key everywhere, `team` is the `0x24` byte and the
  group is the `0x22` number (rule 9).
- **Host election, counts, lookups: not applicable.** RWE's TA host is always the host; if it leaves
  the game ends (`TaHostGame::leave`). Its lockstep lobby has a fixed host. A TA joiner promoted to host
  by `RemovePlayer` is not something RWE takes part in.

## 3. Names, slot creation and the advertised session

**Reference.** `SetPlayerName` copies the long name into a **thirty-byte** display name and the short
name into the full name, without guaranteed termination. `AnnouncePlayerLeft` skips when the local
reject reason equals one, otherwise formats the player with **one of eight randomly chosen translated
departure phrases** and posts a category-four line. `AddNetPlayer` reuses a matching slot only when
not already active and remote, otherwise takes the first inactive non-blocked slot as type three; it
returns one in every case (no slot, existing player, name failure, success). On success it assigns the
DPID and last-heard time, increments the count and, in a connected game, broadcasts every local record
and team, sends group-sync requests and flushes. `CreateLocalPlayer` builds the nickname (or `AI:` plus
the name) **truncated at sixteen**, picks the host from the currently flagged host slot, resets the
colour to `0xff`, sets keepalive bit one, join time, **max units one hundred**, resolution and
version, and calls the add-player wrapper with a **21-byte payload**: the password and **fixed protocol
shorts zero and `0x50`**. `BuildGameInfo` updates the local major and minor and the **low four
occupancy bits**, exports four native dwords from the record's memory, rule and resource region and
builds a **32-byte advertised name: sixteen characters of game name plus fifteen of map name, spaces
replacing NULs, terminated at byte 31**. `UpdateNetGameInfo` sets session flag **`0x20` once started**
and never clears it. `CreateNetGame` marks the local host, builds the advertised fields before
resetting the count to zero and ignores the creation result.

- **Session name 16 + 15: DIFFERS (a fault in a real TA's list).** `taSessionName`
  (`TaDirectPlay.cpp` about 621-630) pads the game name to sixteen and appends the **whole** map name;
  neither is cut. The chapter now supplies the exact bound the HAPINET audit asked for
  (`BT-NETWORK-HAPINET-PARITY.md` gap 2): **sixteen plus fifteen, thirty-one characters, terminator at
  byte 31**. A shipped map name of more than fifteen characters, or a game name of more than sixteen,
  puts RWE past the 32-byte field a real TA copies into without a check. Cap both.
- **Host player name: DIFFERS.** `makeNamedPlayer(hostPlayerId, config.gameName, ...)` sends the game
  name uncapped as the host's player name (`TaHostSession.cpp` about 899); the original truncates a
  nickname at sixteen. A joiner shows the host's name in its battleroom.
- **The 21-byte payload and its shorts: MATCHES the capture; the shorts are now explained, and
  `BT-NEW-CONFLICTS.md` item 129 loses its premise.** RWE sends zeros with byte 19 set to `0x50`
  (`TaHostSession.cpp` about 902-903): the shorts are zero and `0x0050`. The chapter says these are the
  **fixed protocol shorts** a host checks in the create-player message (rule 7), distinct from the
  record's own version (major and minor, from `BuildGameInfo` and `CreateLocalPlayer`'s "version"
  field), which is what the unit-sync compatibility rule reads (`BT-NETWORK-UNIT-SYNC-PARITY.md` rule 4).
  Item 129 assumed the 21-byte block carries the version; this chapter indicates it carries a protocol
  marker. Not resolved here.
- **Session flag `0x20` for started: MISSING.** `makeSessionDescription` (`TaHostSession.cpp` about
  866-878) writes the options byte, a constant one and the player count; it never sets a started or
  locked state, so a joining TA's game list shows an RWE game that has launched as open. With rule 7
  (no admission test) a second TA can join a running game. **UNCLEAR** where the bit sits in the four
  dwords: the chapter says flag `0x20` of the session, the list's status reads `Lock 0x8000` and
  `Play 0x10` of an advertised group (`BT-FRONTEND-MULTI-PARITY.md` rule 6); the three are not obviously
  the same field.
- **Occupancy bits: UNCLEAR.** RWE writes the player count into the **low byte** of the first user
  value (`session.user3 = 0x00010000 | (currentPlayers & 0xFF)`); the chapter says "the low four
  occupancy bits" are updated. Equal for ten players or fewer; different if bits four to seven mean
  something else.
- **Departure phrases and the 30-byte name: DIFFERS, cosmetic.** `GameScene_peers.cpp` about 309 prints
  one phrase, "<name> has left the game". The original draws one of eight; the draw is a presentation
  choice and would use the scene's effects generator, never the simulation's.
- **`max units one hundred`, the `AI:` name, join time: not applicable.**

## 4. Initialization, routing and return values

**Reference.** `InitNetConnection` initialises the wrapper defaults, sets the network mode to ten and
the packet manager's parameters to two and one hundred, then initialises the chosen connection only if
manager setup succeeded. `InitLobbiedConnection` returns at once for an existing lobby connection,
otherwise sets mission type three and derives the session name from the lobby, the command line or the
user name, optionally appending `HHMMSS`; **the literal source writes the result to the `game.net`
address, not to the game name**. `SendPacketToPlayer` needs connected mode, a valid active local sender
with no rejection and an active remote type-three destination with no rejection; in packet-manager mode
it returns the queue's result (queued, not delivered), in direct mode it needs the wrapper's zero and
counts bytes; it does not validate the payload length against the type. `BroadcastPacket` needs a valid
local sender and queues on the broadcast channel or sends to DPID zero; **with duplicate group
assignments it sends to one remote per group** using an eleven-entry seen array that is read before
the range is checked and never marks group ten. Per-peer send failures are ignored. `ReportPacketGap`
is empty. `DefaultPacketHandler` and `RejectPacketType2/5/6/7/8` return minus one. `DispatchPacket`
indexes a 45-entry handler array with the first byte, unbounded; the main receive path uses its own
switch.

- **Routing and return semantics: not applicable.** RWE's TA host sends through `TaOutboundBatcher`
  and `TaHostSession::send`, returning whether the peer is known; no send result proves delivery
  (`BT-NETWORK-PACKETS-PARITY.md` rule 8).
- **Duplicate-group broadcast: not applicable, latent.** One joiner only (rule 9).
- **Handler array indexed by the first byte: not applicable.** RWE's walker is a bounded `switch`
  with a default of "unknown" (`tadExpectedSubPacketSize`, `io/tad/tad_util.cpp` about 277-440).
- **Session name from the lobby: not applicable.** RWE names the game itself.

## 5. The protocol framing table and its mode gates

**Reference.** `InitPacketTables` clears sizes one through forty-four, assigns sizes, handlers and
**mode masks** (lobby one, loading two, battle four), stores the sync tick and allocates 8192 receive
bytes. It does not reset every mode and handler entry wholesale, so unassigned entries retain prior or
static content. Zero or unassigned size is significant to the frame parser. The table (decimal / hex,
bytes, mask):

| Type | Bytes | Mask | Meaning |
| --- | ---: | ---: | --- |
| 2 / `02` | 13 | 7 | ping or echo |
| 3 / `03` | **3** | 7 | reserved / default callback; "also raw condenser envelope outside application parsing" |
| 5 / `05` | 65 | 7 | chat |
| 6, 7, 8 | 1 each | 7 | probe, acknowledgement, enter battle |
| 9 / `09` | 23 | 4 | unit creation |
| 10 / `0a` | 7 | 4 | attachment |
| 11 / `0b` | 9 | 4 | unit damage |
| 12 / `0c` | 11 | 4 | unit death |
| 13 / `0d` | 36 | 4 | weapon fire |
| 14 / `0e` | 14 | 4 | projectile impact |
| 15 / `0f` | 6 | 4 | feature damage, burn, death |
| 16 / `10` | 22 | 4 | COB script invocation |
| 17 / `11` | 4 | 4 | unit state bits |
| 18 / `12` | 5 | 4 | builder and completed-unit link |
| 19 / `13` | **18** | 7 | sound |
| 20 / `14` | 24 | 4 | unit ownership transfer |
| 21 / `15` | 1 | 6 | startup ready and vision handshake |
| 22 / `16` | 17 | 4 | resource or map sharing |
| 23, 24 / `17`, `18` | 2 each | 7 | colour request, assignment |
| 25 / `19` | 3 | 7 | speed and pause |
| 26 / `1a` | 14 | 1 | pregame catalogue sync |
| 27 / `1b` | 6 | 7 | rejection |
| 28 / `1c` | **5** | 7 | player drop |
| 29 / `1d` | **9** | unassigned | integrity placeholder |
| 30 / `1e` | 2 | 6 | start position |
| 31 / `1f` | 5 | 6 | start-position acknowledgement |
| 32 / `20` | 186 | 7 | player record |
| 33 / `21` | 10 | 1 | group assignment request |
| 34 / `22` | 6 | 1 | group assignment result |
| 35 / `23` | 14 | 7 | directional alliance |
| 36 / `24` | 6 | 7 | numbered team |
| 37 / `25` | **5** | 1 | no explicit main-switch case |
| 38 / `26` | 41 | 7 | host slot-to-DPID table |
| 39 / `27` | **17** | 7 | executable integrity warning |
| 40 / `28` | 58 | 7 | economy, statistics, view initialisation |
| 41 / `29` | 3 | 7 | view acknowledgement |
| 42 / `2a` | 2 | 7 | load progress |
| 44 / `2c` | 3 minimum | 4 | variable-length unit state block (embedded length) |

Types one, four and forty-three have zero sizes after this initialiser; DirectPlay system messages use
a native dword type and sender zero, independent of this table.

- **Sizes agreed with `tadExpectedSubPacketSize`: MATCHES.** Types `02`, `05`, `06`, `07`, `08`, `09`,
  `0a`, `0b`, `0c`, `0d`, `0e`, `0f`, `10`, `11`, `12`, `14`, `15`, `16`, `17`, `18`, `19`, `1a`,
  `1b`, `1e`, `1f`, `20` (186), `21`, `22`, `23`, `24`, `26` (41), `28`, `29`, `2a` and `2c` (embedded
  16-bit length) have the same length in `io/tad/tad_util.cpp` (about 277-440) and the corpus counts
  in `TA-DEMOS.md`. The count of agreements is the strongest evidence yet for the chapter's table.
- **Type `03`: CONFLICT.** The chapter's table says **3** bytes; `tadExpectedSubPacketSize` says **7**
  (`TadSubPacketCode::Unk03`, about 296-297), and `TA-DEMOS.md` lists it as "`0x03` | 7 | ? " with the
  note that blank-count lengths "are the reference's and are unverified". No corpus occurrence. Neither
  is settled; the chapter's note ("raw condenser envelope outside application parsing") suggests the
  value never appears as an application subpacket, in which case the choice is harmless. See
  `BT-NEW-CONFLICTS.md` Batch 15.
- **Type `13` (sound), `1c` (player drop), `1d`, `25`, `27` (integrity warning): MISSING sizes.**
  The chapter sizes `13` at 18 (`BT-SOUND-PARITY.md` rule 8 already supplies that), `1c` at 5, `1d`
  at 9, `25` at 5 and `27` at 17. `tadExpectedSubPacketSize` has no case for any of them, so each falls
  to the default of zero, is counted in `unknownCodes` and **takes the rest of that bundle with it**
  (`tadSplitSubPackets`: "an unsized code becomes a single subpacket of everything that remains").
  On the live TA path (`TaHostGame.cpp` about 296-315) that discards any `0x2c` that followed. `13`
  and `25` have never appeared in the corpus; **`1c` and `27` are exactly the codes a peer sends when
  somebody drops or when the executable differs**, which no corpus game shows because it did not
  happen. Add the four sizes.
- **Mode masks: MATCHES in effect.** RWE's battleroom handles only the lobby codes (`02`, `20`, `24`,
  `17`, `15`, `1a`) and its in-game path hands everything to the driver, whose switch ignores lobby
  codes (`TadPuppetDriver.cpp` about 1170-1210); a lobby message in game or a battle message in the
  lobby is dropped, which is the chapter's mask in effect.
- **The 45-entry unbounded dispatch: not applicable.** See rule 4.

## 6. The join worker, leaving and teardown

**Reference.** `JoinLobbyGame` borrows the password if required, defaults capacity to ten (an online
configured value above one is clamped to ten), allocates a 28-byte argument block and starts a worker
with a **32 KiB stack**; the worker only calls the create-or-join adapter. The caller waits **forty
seconds**; on timeout it restores the cursor, may toggle display or sleep, and **raises a fatal
error**; non-timeout wait outcomes read the result without checking `WAIT_OBJECT_0`; the thread handle
is not closed; the argument block is freed afterward (unsafe if the worker is still running on the
fatal path). `JoinNetGame` acts only for the local slot, takes the lobby name preference (short, then
long, over the online default), lets an online password update the record by host flags, resets slots
for a lobby connection, clears readiness and colour, copies resolution and resets the count; it then
joins through the worker or by the selected GUID, **calls `CreateLocalPlayer` without checking the
result**, enumerates peers, broadcasts local info, team and group requests, and requests colour zero.
`RemoveLocalPlayers` removes each active local human or computer and shuts the score tables if
connected. `LeaveNetGame` flushes, removes local players, quits DirectPlay, clears the close handler,
sets the endgame bit and calls `QuitApp` with the reject policy; `LeaveNetGameCallback` does the same
without the forced flush. `CloseNetSession` and `ReleasePacketData` do not free all packet-manager pools.

- **RWE as host: leaving MATCHES the observation.** `TaHostGame::leave` (about 440-480) sends the
  commander's `0x0c` with cause 8 and then `DELETEPLAYER` for **both** of the host's ids
  (`sendHostDeleted`, `TaHostSession.cpp` about 854), waiting up to five seconds for it to reach the
  wire. The chapter's "remove each local player" is that pair; `TA-NETWORK.md` ("A player leaving is
  its own quit sequence") recorded it from a capture.
- **Joining a TA host (the worker, the 40-second wait, `JoinNetGame`): not applicable.** RWE does not
  join (`TA-NETWORK.md`, "Open"). If it ever does, the 40-second figure and the enumerate-then-
  broadcast sequence above are the order to follow, and the fatal timeout is not to be copied
  (`BT-DEBUG-DEBUG-LIB-PARITY.md` rule 5).
- **A fatal error on a join timeout: DIFFERS, by design, unrecorded.** See the debug audit.
- **Pools not freed on close: not applicable.**

## 7. Admission, rejection and the reason texts

**Reference.** When a create-player system message arrives at a **local host**, the host first runs
`AddNetPlayer`, then rejects (via `RejectPlayer`) if admission is **closed**, the payload size is not
**21**, the two version shorts are not zero and `0x50`, or a required **case-insensitive password**
does not match; the payload is inspected before a later null check. `GetRejectReasonText`: **3 closed,
4 password, 5 full, 6 disconnected, 7 missing unit, 8 version, 9 watching disallowed, 10 creator
left**, anything else "rejected"; the UI and protocol can store further reasons with no distinct text.
`RejectPlayer` builds type `27` in shared storage: a **local-human** target loops all local players,
broadcasts each id, removes the original target during the loop and stamps reasons; a local computer
removes only itself; a **remote human removes every record sharing its (narrowed) group value** with no
active or type guard on the inner comparison; a remote computer only itself; the original target's
reason is set afterwards. Send results do not roll back removals. "This helper does not itself test
'caller is host'."

- **Admission tests on CREATEPLAYER: MISSING.** `TaHostSession::handleCreatePlayer`
  (`TaHostSession.cpp` about 603-649) accepts any player that completed the handshake: it checks only
  that the session is not full (`peers.size() + 1 >= MaxPlayers`). It never reads the payload's size or
  its two shorts, has no password, and has no closed state: **a second player is admitted after the
  game has launched** (`TaHostGame.cpp` about 247 calls `room->peerJoined` unconditionally). A full
  session is refused by silently dropping the pending handshake (`pending.erase(key); return;`), which
  leaves the joiner waiting; the original sends reason five.
- **The `0x1b` record: MISSING in both directions.** No encoder or decoder for type `27` exists under
  `io/tad/` or `net/ta/`; `TadSubPacketCode::Reject` is only a length (6 bytes, `tad_util.cpp` about
  332). RWE can neither refuse a joiner with a reason nor read one aimed at itself. Layout: type,
  target DPID, reason (inferred from the 6-byte length and the chapter's "embedded target and reason";
  the byte order is not stated).
- **Reason texts: not applicable.** RWE's guest sees "The game is full" or a lost claim
  (`BT-FRONTEND-MULTI-PARITY.md` rules 6 and 12).
- **Group removal on a remote human: not applicable.** One joiner, no groups (rule 9).

## 8. The receive loop and its validation

**Reference.** `ReceiveNetPacket` returns zero if not connected, otherwise calls the packet manager or
raw HAPINET; success counts application bytes; no-message (`0x887700be`) and other errors return zero;
buffer-too-small (`0x8877001e`) reallocates and retries **with no null check**; the successful length is
local while `recvPacketSize` keeps the capacity, so dispatch is given no separate length for each field.
`HandleNetPackets` resets ten counters, drains until receive fails or type eight ends the pass, then
checks duplicate groups and timeouts, and **caches the receive-buffer pointer before the loop**, so a
reallocation leaves it stale; scratch writes during a switch can alter the retained input bytes. **Sender
zero is DirectPlay system traffic**, processed only for an active local-human recipient: destroy-player
(type five) rejects host or local appropriately before battle; create-player (three) runs
rule 7; set-data `0x102` copies **185 bytes** into the found record, checks forbidden observers and
creates or destroys an otherwise temporary player; set-name `0x103` copies thirty bytes unterminated;
set-session `0x104` copies settings for a non-host; embedded native pointers are trusted. For
application traffic `CommandAllowed` **indexes the mode table before checking the command byte**, and the
later range rejection tests `byte <= 1 AND byte >= 45`, which is impossible. It rejects local senders,
unknown or ineligible players and an invalid recipient and updates the accepted remote's last-heard time
and message count. **It does not authenticate payload DPIDs or unit ownership against the sender or
bound every payload field**; each consumer has its own guards.

- **Unknown sender dropped: MATCHES.** `TaHostSession::handleAppData` (about 522-536) discards a
  message whose `from` is not a registered peer.
- **Sender identity is the DPID in the message: MATCHES the original's weakness, and it is a fault
  here.** Like the original, RWE binds a message to a player by the id **inside** the message and never
  against the source address of the datagram or connection. See rule 20.
- **Per-consumer guards: MATCHES in spirit.** The driver checks ownership for the one case that
  matters (a death is honoured only from the owner, `TA-NETWORK.md` "Damage and death"); malformed
  bundles cost that message (`handleTcpBytes`, `handleUdp` catch and drop; `MaxBufferedBytes` bounds a
  stream). The checksum is parsed and **not read** on the live path (`BT-NETWORK-CONDENSER-STATS-PARITY.md`
  rule 5): the original rejects the packet; RWE applies it.
- **Impossible range check, stale cached pointer, shared scratch buffer: not applicable.** The
  original's defects; RWE has none of the structures.

## 9. Lobby and control messages

**Reference** (receive actions). `32` copies the embedded-DPID record into an active remote and checks
duplicate groups. `38` copies **ten DPIDs** into the host-slot table with no explicit host-sender test.
`23` lets the local host choose or approve a requested colour; `24` installs the recipient colour and
republishes local info, teams and group requests. `35` resolves embedded endpoints, updates local
directional relations through `SetAlliance`, refreshes the lobby and allies UI and writes the sender's
outgoing ally byte. `36` assigns the embedded player's numbered team. `27` calls `RejectPlayer`. `28`
announces the disconnection, runs the drop, remove and rebroadcast logic, and sets failure and endgame
flags when the dropped id is the first local human; **the drop helper overwrites the shared buffer its
caller reads**. `30` stores the recipient's start position and sends `31` with the recipient's DPID; `31`
marks the embedded target's acknowledgement. `05` posts incoming category eight only for a local-human
recipient. `39` formats an integrity warning and posts it **twelve times**; it does not itself reject
or repair an executable. `29` calls a packet-manager integrity no-op when enabled. `06` replies with `07`
from the first active local sender; `07` sets keepalive flag zero; `08` sets the battle bit and ends the
receive pass. `25` uses byte one to choose **`SetGameSpeed(byte two)` without rebroadcast** or direct
pause-bit assignment. `26` calls unit sync only for an active local-human recipient. `42` stores the
sender's progress. **Group requests `33`**, handled only by a local host: an unflagged request
broadcasts an existing non-zero group; a flagged request inherits the referenced controller's group or
allocates the first free one, installs it and broadcasts `34`; `34` installs it on the embedded
player. `SendLobbySyncRequests` visits valid players with group zero: local humans request an independent
group, computers the local controller's group, remotes ask about their existing assignment; a local host
simply assigns. **The group groups machine-owned players; numbered alliances are separate.**

- **`32` read: MATCHES.** `taParsePlayerStatus` reads side (150), colour (151), state (156) and the
  map name (bytes 1-32); the rest of the record is ignored (rule 2).
- **`38`, the host's slot table: MATCHES the capture; the meaning is now known, and multi-joiner is
  UNCLEAR.** `taBuildIdent(hostId, peerId)` is `26` followed by two DPIDs and **32 zero bytes**
  (`TaBattleroom.h` about 175; 1 + 8 + 32 = 41, the chapter's length). The register called the 32 bytes
  "empty in every capture"; the chapter says the record is a **ten-slot DPID table** the host broadcasts
  at every heartbeat. RWE sends `[host, joiner]` in the first two slots to each peer, which is the
  full table only for one joiner. With two or more joiners each would need the whole table in slot
  order; slot order is not specified in the chapter beyond "copies ten DPIDs".
- **`23`, `24`, colour: rule 13. `35` and `36` in game: rule 12.**
- **`27`, `28`, `39`, `29`, `25`: MISSING or partial.** `27` and `28` are not decoded (rules 5 and 7);
  `39` is unsized (rule 5) and the integrity warning, which the original only posts, is lost with the
  bundle; `29` is a no-op with no mode mask. `25` is read in game and applied as RWE's own clock hook
  (`GameScene_tahost.cpp` about 292-312), reading two bytes as 256ths of normal; the chapter confirms
  the **three-byte shape with a first byte that picks the speed or the pause branch**, as `BT-178`
  (resolved: subtype and value) already says. The text in `TA-NETWORK.md` ("RWE's own-clock mode")
  and the encoder `tadEncodeSpeed(u16)` are the stale part, already tracked.
- **`06` / `07` / `08`: MATCHES in effect.** RWE sends a `07` with every keepalive and `08` to start the
  launch, but **does not answer a joiner's `06` with a `07`** (`handleAppData` has no `Probe` case);
  the periodic `07` makes the joiner hear it anyway, and `TA-NETWORK.md` records that a silent host is
  offered for rejection, not that a missing reply is.
- **Start positions `30`, `31`: rule 16.**
- **Chat `05` and the `0x27` warning: rule 14 and rule 5.**
- **Groups `33`, `34`: DIFFERS.** `TaBattleroom::grantColour` sends the numbering `22 <id> <n>` once,
  unprompted, right after the first colour grant, "the host being 1 and joiners from 2"
  (`TaBattleroom.cpp` about 494-502); the chapter's `34` is the host's *answer* to a joiner's `33`
  request, and the value is the first free group. RWE never handles `33` (the default case). Same
  numbers for one joiner. **Latent defects:** the `numbersSent_` latch means a second joiner is never
  numbered, and the loop numbers only the peers present at the first grant. The `22` is the
  group-assignment result, not a player number; `taBuildPlayerNumber`'s name and comment ("its 1-based
  number in the game") are the stale part.

## 10. Live units, construction and attachment

**Reference.** `SendNewUnit` broadcasts `09` with a **definition index, a numeric unit id, three fixed
coordinates and three angles**; there is no name string or model. Receive of `09` calls
`CreateUnitFromPacket` with the sender slot and of `2c` calls `ReceiveUnitStates` with the sender;
definition agreement was established separately by unit sync. `10`, `11` and `12` delegate attachment,
damage and death. `BroadcastBuilderLink` sends `12` with the completed unit's id then the builder's;
the receiver calls `FinishConstruction(builder, constructed)`; the packet carries references, not
wireframe vertices or a build percentage.

- **`09` build started: MATCHES.** `tadEncodeBuildStarted` / `applyBuildStarted` carry the type index,
  unit id, position and rotation (`TaBattleroom::sendGameStart`, `TadPuppetDriver.cpp` about 1172). The
  index is a 1-based load-order position, so **the data set decides it** (`LaunchParams` comment); the
  chapter says the same, "definition agreement is established separately", which RWE's echo of unit sync
  does not do (`BT-NETWORK-UNIT-SYNC-PARITY.md` gap 1).
- **`0a` attachment: MISSING.** Neither side. A transport carrying a TA unit, or RWE's unit loaded by a
  TA transport, is invisible to the other machine. The driver's switch has no case; the sender never emits
  one (`TaLiveSender.h`, "the mandatory set").
- **`12` builder link: MATCHES.** `tadEncodeBuildFinished` / `applyBuildFinished`.
- **`2c` receive: MATCHES the register** (applied on arrival; `BT-NETWORK-PACKETS-PARITY.md` rule 7).

## 11. Weapons, features, scripts, state bits, sound and ownership

**Reference.** Types `0d` and `0e` delegate weapon fire and projectile impact to the weapons module.
**Type `0f`** maps subtype **`0xfd` to a non-violent feature removal, `0xfe` to burning, `0xff` to a
violent removal**, and any other value to a weapon-definition index for `DamageFeature` at unsigned-short
cell x and z. The four script senders broadcast a fixed **22-byte** `10`: unit id, script index, byte
argument count and four integers; the name variants look the script up even before the connected guard;
none validates the count against four. Receive looks up the numeric id and, **if the live flag
`0x10000000` is set**, starts the script by index; **a zero id yields null and a dereference**. Type `11`
applies all requested bits through `SetStateBits(mask, true)` then the complement with `false` for a
live record, without a generation handle. Type `13` plays a sound globally if a flag is non-zero,
otherwise positionally at the vector at byte six. Type `14` resolves a live unit and the embedded new
owner's DPID and transfers **only when the new owner is an active local human or computer**; a missing
owner yields null passed to `IsConnected`, which dereferences it.

- **`0d` weapon fire, `0b` damage, `0c` death: MATCHES** (`TA-NETWORK.md` "Damage and death").
  Owner-authoritative death, true health on the next full-state record, `0b` applied to the owner's own
  copy through `applyIncomingDamage`.
- **`0e` projectile impact: MISSING.** Neither sent nor applied. Area damage is a `0b` per victim
  from the attacker, so the effect on health is covered; the impact's visual (explosion at the point)
  is not sent to the peer.
- **`0f` feature action: MISSING both ways, and shared map state diverges.** RWE does not emit `0f`
  when a feature is reclaimed, burned or destroyed on its side, and does not apply one received. A
  wreck RWE reclaims stays on the TA player's map, and one the TA player reclaims stays on RWE's. The
  corpus has 229,523 of them (`TA-DEMOS.md` table). `TA-NETWORK.md` records the quit burst as
  deliberately not sent (an undecoded payload); the chapter now supplies the subtype meanings
  (`0xfd`, `0xfe`, `0xff`, else a weapon index), which is most of the payload. Feature state is
  one-directional: TA decides what exists on its machine, RWE on its own.
- **`10` script call: DIFFERS, recorded.** RWE applies an incoming one (`applyScriptCall`) and sends
  none (TA-DEMOS D9: "no inventory of the `0x456200` call sites"). The chapter gives the other half of
  the answer: **four sender entry points** (`0x456110`, `0x456190`, `0x456200`, `0x456290`), name and
  index variants, with and without arguments; a fixed 22 bytes. `0x456200` is therefore one of four,
  and the inventory D9 lacks is the callers of these four.
- **`11` unit state bits: MISSING on receive.** RWE sends one at launch (`taBuildUnitStateWord`, the
  commander's, `TaBattleroom.cpp` about 210) and ignores every incoming one. A TA unit the player has
  set to hold fire, or a building switched off, is not reflected on RWE's copy; the chapter says the
  receiver applies the mask and the complement, so a full state is carried in four bytes.
- **`13` sound: MISSING and unsized** (rule 5). Never seen in the corpus.
- **`14` give unit: MISSING.** Neither sent nor applied; units are not transferable between a TA player
  and RWE, and a TA `14` aimed at RWE's player would be dropped. `TOTALA-EXE-COMMANDS.md` does not cover
  it; the chapter notes the receiver transfers only to an active **local** player, which would be RWE's.
- **Null dereferences on a zero id, on a missing owner: not applicable.** The driver bounds its lookups
  (`removeRemotePlayer` has a malformed-input test; a script call naming no unit is not traced).
  **UNCLEAR** whether `applyScriptCall` and `applyDamage` check the unit id and the live state before
  use; `io/malformed_input.test.cpp` is the place for the case.

## 12. Directional alliances and numbered teams

**Reference.** `SetAlliance` resolves embedded source and destination and changes outgoing and incoming
relation bytes according to which endpoint is local; computers, remote computer destinations and the
force flag can make the relation reciprocal. A remote destination gets type `23` with endpoint DPIDs,
the value and the force flag; the call reports multiplayer event four. `SendAlliance` always builds the
message without the local mutation. `BroadcastAllyTeam` sends type `24` with DPID and team. "A
directional alliance, a reciprocal alliance, a numbered team and the shared-victory bit remain separate
states."

- **Numbered team `24`: MATCHES.** RWE reads a joiner's team (`TaBattleroom.cpp` about 408) and
  sends the host's (`taBuildTeam`).
- **Directional `23`: MISSING; UNCLEAR whether needed.** The corpus has 5,638 of them at game start,
  sent by TA after the teams are known. RWE neither sends nor reads one. Whether a TA treats the absence
  of a `23` from RWE as "not allied" while the `24` says "same team" is exactly the interaction the
  chapter describes (the team is separate from the relation) and was not tested. The register has no
  entry; `BT-GAME-PLAYERS-PARITY.md` rule 1 notes RWE's relation is the symmetric lobby team.
- **Symmetric, fixed alliances in RWE: UNCLEAR (lockstep-by-design, unrecorded).** See
  `BT-FRONTEND-MULTI-PARITY.md` rule 13.

## 13. Colours

**Reference.** `IsColorFree` rejects a requested colour **outside zero to nine or `0xff`**, builds a
ten-byte owner table from non-zero types **except the requesting DPID** and returns vacancy; an existing
colour other than `0xff` is used as an index without checking it is below ten. `AssignPlayerColor`
cycles at most ten candidates, wrapping candidates at ten or above to zero and excluding colours of
other non-empty, non-blocked players; negative starting candidates are not lower-clamped; on exhaustion
the outgoing byte can be left uninitialised. A local target is written directly; a remote send updates
the host's cached copy only in packet-manager mode with a non-zero send result. `RequestPlayerColor`
lets the local host accept a free colour or run the assignment, otherwise writes `23` and sends it to the
host or broadcasts it when there is no host, flushing queues; "a missing host does not synchronously
produce a valid color."

- **Host grants the asked colour if free, otherwise the next free one: MATCHES.** `taGrantColour`
  (`TaBattleroom.cpp` about 168-180): start at the wanted colour (or zero if it is not below ten --
  the chapter's wrap), scan ten candidates, skip taken ones, `0xff` when none. The taken set is the
  host's own colour plus every other peer's (`grantColour`, about 467-485), the chapter's "except the
  requesting DPID". The reply is a directed `18` (`queueReply`), a host-only answer to a `17` that
  is accepted only while the room is **Waiting** (about 396-405), as the chapter makes it a host action.
- **Exhaustion: MATCHES, better.** `0xff` is returned and **nothing is sent** (`grantColour` returns),
  which leaves the joiner without a colour; the chapter's exhaustion path leaves the outgoing byte
  uninitialised. With ten colours and nine joiners it cannot happen.
- **Host colour in the taken set: MATCHES.** `taken{config.colour}`.
- **The lobby's colour rule (a seat's colour held by another seat is refused): MATCHES in effect.**
  `applyClaimChoice` and `ensureFreeColour` (`MainMenuScene_multiplayer.cpp` about 583-640): the host
  decides and the guest is told by the next announcement. The logo picker's candidate rule is
  `BT-FRONTEND-MULTI-PARITY.md` rule 10.

## 14. Chat, heartbeat and the ping clocks

**Reference.** `SendChatPacket` stores a 65-byte `05` with at most 64 text bytes and no terminator; a
leading plus or mode zero **broadcasts**, mode three iterates the selected recipients, other modes
target remote outgoing allies (one) or non-allies (two); it uses the **first type-one DPID**, not an
explicitly passed speaker; with no sends its return value is uninitialised. `SendNetHeartbeat` runs only
after `now - stored > 60` display ticks (**two seconds**), advances the stored deadline by 60 (so late
calls catch up one interval each), and for each local player flushes, sends a `02` with
`GetTickCount` milliseconds and echo zero **with guaranteed delivery temporarily disabled**, flushes and
restores the policy, sends a probe `06`, requests a missing colour and, for the host, broadcasts the
ten-slot `26`; it then republishes local info, team and group requests and marks the lobby dirty.
`HandlePing` stamps the first echo with `GetTickCount`, sends it from the recipient's transport DPID to
the original target and temporarily disables guaranteed packets likewise; an echoed packet whose target
is a local playable slot updates the actual sender's ping to **current milliseconds minus the send tick**;
it does not subtract the remote's processing time; lookup failures reach the spare slot. Queue flushing
reduces local delay but does not establish synchronised clocks.

- **Heartbeat: MATCHES, and explains the capture.** `TaBattleroomConfig::keepaliveInterval` and
  `pingInterval` are 2000 ms. The keepalive sends `07`, then `06`, the `26` table and the `20` and `24`
  records (`sendKeepalive`, about 584-610): that is the chapter's heartbeat (probe `06`, host table `26`,
  republished info and team) with the `07` that a peer would send as the probe's reply. The register's
  observation, "a `07` and a `06 26 20 24` status about every two seconds", is the chapter's heartbeat
  seen from outside.
- **Ping: MATCHES.** A reply echoes the requester's tick and adds the responder's; the requester reads
  the difference (`TaPinger`); `TA-NETWORK.md` records the same ("only believable if it echoes a tick the
  requester sent"). The chapter adds that neither side subtracts processing time.
- **"Guaranteed delivery temporarily disabled" around pings: UNCLEAR.** The register says pings and the
  heartbeat go over TCP in the battleroom and UDP in game (`BT-NETWORK-HAPINET-PARITY.md` rule 3). The
  chapter's flush-and-disable pattern would put the ping on the **unguaranteed** path even in the lobby.
  `TaPinger` frames its own uncompressed 20-byte packet and sends it on the same transport as the rest.
  A capture comparison would settle it.
- **No in-game ping, early flush on ping receipt: MATCHES the register** (the heartbeat belongs to the
  lobby, and a ping received in game flushes the queue, `TA-NETWORK.md` "Why a TA flushes a packet
  early"); the chapter's `HandlePing` is the flush's other half: it "flushes" to reduce local delay.
- **Chat `05`: MISSING** both ways (`BT-NETWORK-NET-CHAT-PARITY.md` rule 6); the battleroom adds a third
  case, a TA joiner's lobby chat (`BT-FRONTEND-MULTI-PARITY.md` rule 22).
- **Chat recipient (first type-one DPID, not the speaker): not applicable.**

## 15. Removal, drop and timeout policy

**Reference.** `BroadcastPlayerLeft` builds `28` in scratch storage, removes immediately for a remote or
when loading has not begun or has finished, then broadcasts from the local player. `RemovePlayer`
requires an active type one, two or three and a valid index, clears local players' relation bytes for the
slot and **kills its units**; before battle it removes the local DirectPlay player and clears slot, type,
activity, DPID and group; **in battle it retains local records but clears remotes**; it decrements the
count, clears CD-present and outgoing alliance bytes and reports event three; **in battle a removed host
makes the highest unsigned active human or remote DPID the host**, without clearing a prior host bit;
repeated removal of a retained local record is not idempotent. `CheckPlayerTimeouts` is skipped when UI
option bit zero is set; pause bit zero refreshes a global baseline instead; a remote is **timed out when
`now - max(lastHeard, baseline) > timeoutSecs * 30`** ticks. It opens a prompt **only if all timed-out
remotes belong to one group**; mixed groups suppress or close the prompt, "avoiding immediately blaming
one peer when the local machine may have lost connectivity". `UpdateTimeoutDialog` refreshes every two
ticks, shows `timeoutSecs + 120 - elapsed / 30` and **at that extended deadline rejects with reason six**
and closes; the display uses `lastHeard` directly, unlike the pause-adjusted trigger. The dialog has a
chat box and an immediate Reject button. `SendProbe` merely sends `06`.

- **A leaving TA joiner's units die: MATCHES.** `removeRemotePlayer` (`sim/MixedOwnership.cpp` about
  115-152) kills the commander with the quit cause and quietly kills every other unit of the leaver,
  then marks the player dead -- the chapter's "kills its units" and `TA-NETWORK.md`'s quit sequence.
- **A dropped RWE lockstep peer's units stay: CONFLICT (design against reference).** The lockstep drop
  (`GameScene_peers.cpp` about 296-325, `PlayerDroppedCommand`) leaves "their units exactly as they were,
  with the orders and the fire mode they had", and the roadmap says "which is what the original does"
  (`docs/ROADMAP.md` about 460). The chapter says the original **kills** them. No `TOTALA-EXE*.md`
  finding backs either; the roadmap line is a design statement with no register behind it. See
  `BT-NEW-CONFLICTS.md` Batch 15.
- **Timeout figures: DIFFERS, lockstep-by-design, unrecorded.** The lockstep drop is **automatic after
  `dropTimeoutSeconds` (default 10, 0 = never)** of a peer being waited on (`GlobalConfig.h` about 234,
  `updatePeerLiveness`), with a countdown banner ("WAITING FOR ...", "DROPPING IN n") and no player
  decision. The original asks the player (a prompt with Reject), after `timeoutSecs * 30` ticks plus a
  further 120 seconds before it rejects on its own, and offers it only when the silent peers share one
  machine group. RWE's "mixed groups" case does not exist: it drops the quietest by a deterministic
  rule (`droppingPlayerFor`). UNCLEAR as a recorded divergence: not in section 88 or `compatibility.md`.
- **The TA-host path has no silence timeout: MISSING.** A joiner that vanishes without `DELETEPLAYER`
  (power cut, crash, router) is never dropped: `TaHostSession` has an idle timer for *unaccepted* TCP
  connections only (`incomingIdleTimeout`, about 128) and `GameScene_tahost.cpp` handles `PeerLeft` and
  nothing else. The game continues against a frozen puppet for ever. The original would prompt and,
  after the extended deadline, reject. Kind: robustness; fault 4 in rule 20.
- **Pause adjusts the baseline: not applicable.** A paused own-clock game keeps both clocks.
- **Host succession, retained local records: not applicable.**

## 16. Loading, start positions and the startup handshakes

**Reference.** `SendLoadProgress` averages six byte stage percentages with integer division, writes
each local player's progress and broadcasts type `2a`; it does not transmit per-stage detail.
`AssignStartPositions` checks host locality through the spare-aware slot helper; a local host builds a
once-only ten-slot array: **fixed starts assign consecutive positions (from zero) to non-observer valid
participants**, **random starts shuffle the candidate positions**, with the two-player shuffle
conditional on an extra random bit; everyone else gets minus one, sent as byte `255`. The host waits for
each active remote's `shareVisionReady` **and** `startPosAssignAck`; a client waits only for
`shareVisionReady`. The host **sends `1e` repeatedly until each slot acknowledges** (`1f` identifies the
recipient). A non-host always broadcasts `15` from its local participants; the host broadcasts `15` only
when its all-peer condition is satisfied; the receiver of `15` records `shareVisionReady` **only after the
loading flag bit two is set**. The function can return false while messages make progress and the load
coordinator calls it again.

- **`1e` is the start position, not "the game starts": CONFLICT (register wording against
  reference).** `TA-NETWORK.md` ("Launch") and `taBuildStart` / `TadSubPacketCode::Start1e` call it "the
  game starts. Every capture says 1" and `TA-DEMOS.md` lists `0x1e` as "start". The chapter says type
  `30` is the **recipient's start-position index** and `31` its acknowledgement. The value `1` in
  every capture then reads as "your start position is index 1" -- the joiner's, with the host at index
  zero, which is also where `TaHostLobbyScene::computeLaunchParams` places RWE's own commander
  (`findStartPosition(..., 1)`, the first position, about 195). The three agree for a fixed-start,
  two-player game. They stop agreeing for a random start or a third player.
- **Start positions on an RWE host: DIFFERS.** RWE takes position one for itself and tells the joiner
  `1` always; there is no random start (`--start-location` does not exist on the host), and the
  position index is not derived from the number of participants. The joiner's own game decides where
  its commander is built from that index and sends the `09` (rule 10), so a map where position index one
  is not the joiner's intended one (or a joiner on a slot other than the second) gets the wrong spot.
- **Waiting for the acknowledgement: DIFFERS.** RWE sends `1e` once, with the last loading-ladder step
  (`sendLoadingStep`, about 735), and never reads `1f` (`Unk1f` is only a length). The original resends
  until acknowledged. A lost `1e` on TCP is not a case (TCP), so this is robustness only on UDP, which the
  launch does not use for it.
- **The `15` handshake: MATCHES in effect.** RWE waits for the joiner's TCP `15` while Launching
  (`joinerLoaded_`, `TaBattleroom.cpp` about 420-425) with a bounded wait of ten ladder steps
  (`maxLoadedWaits`), then sends its own `15` on UDP (`moveToUdp`). The chapter's condition ("the host
  broadcasts `15` only when its all-peer condition is satisfied") is the same wait; the loading-flag
  gate on the receiver corresponds to RWE accepting `15` only in the Launching state. The timeout is
  RWE's (the original has none, see rule 15).
- **Progress `2a`: MATCHES.** The ladder `00, 26, 36, 64` (`TaBattleroomConfig::loadingProgress`) is
  the recorded host's; the chapter's percentage is the average of six stages, which four captured steps
  are consistent with. RWE does not read the joiner's `2a`.
- **Random shuffle with an extra bit (two players): UNCLEAR, lockstep-by-design.** RWE's lockstep
  deal is a Fisher-Yates from the simulation's generator (`dealStartPositions`, `GameSimulation.cpp`
  about 2641-2660), the same on every peer; the original's host decides and tells the others, so the
  random function need not match. The "extra random bit" is not in `TOTALA-EXE*.md`.
- **Fixed starts, consecutive positions: DIFFERS** (`BT-FRONTEND-MULTI-PARITY.md` rule 17, gap 6). RWE
  deals the *seat number* as the position (the occupied slot numbers as the position set), the original
  gives participants positions in order. Earlier audits (Batch 11) record that the start-slot deal
  "uses the occupied slot numbers as the position set, sparse sets included, as the reference does";
  that was the skirmish loading chapter, this is the network host's assignment. Whether the two agree
  for a game started from the lobby is not settled by either chapter.

## 17. Resource sharing

**Reference.** `SendShareMetal` emits type `22` subtype one, `SendShareEnergy` subtype two; both need
active, valid-index, surviving-or-never-created endpoints and carry the two DPIDs and the amount's raw
dword (normally float bits). `SendShareMapInfo` subtype three rejects only sentinel-ten arguments and
carries zero. On receive, subtype one calls the provisionally named `TransferMetal`, **which actually
credits energy**, and subtype two `TransferEnergy`, which credits metal; the names agree between send
and receive and are reversed relative to the fields. The map subtype calls `ShareMapInfo`.
`UpdateResourceSharing` acts only when connected. At **exact game-tick multiples of 60** (two seconds) it
chooses a destination separately for metal and energy: a **remote human** with an **outgoing alliance**,
active, valid index, surviving or never created. **Each qualifying candidate with less stock than the
donor replaces the previous one**, so it picks the last qualifying slot, "not the globally neediest peer
despite the source comment". Metal sends **one third** of the surplus above `shareMetal`; energy
**one half**; each is capped by the recipient's capacity minus stock, without lower-clamping the gap. The
helper is called with the spend-and-broadcast flag set. At multiples of **450** (fifteen seconds),
with `shareMapping` enabled, subtype three goes to qualifying allies. Repeated calls on the same tick
repeat transfers; the schedule is the caller's responsibility.

- **The automatic pass: MATCHES `TOTALA-EXE-COMMANDS.md` section 124 and the chapter.** The register's
  account (every two seconds, last qualifying slot, one third and one half of the surplus over the
  reserve, capped at free storage, the crossed routine names) is the chapter's, point for point.
  `runAutomaticSharing` (`sim/ResourceSharing.cpp` about 211-240) runs when `gameTime % 60 == 0`
  (`GameTime(2 * SimTicksPerSecond)`), metal with divisor 3 and energy with divisor 2, donors being
  living humans; the last qualifying recipient wins (`runAutomaticSharingFor`, about 69-125).
- **The capacity gap is clamped: DIVERGENCE (hardening), partly recorded.** The chapter says the cap is
  "without lower-clamping that capacity gap", so a recipient above its storage yields a negative
  transfer; RWE takes `min(offer, free)` and returns unless `offer > 0` (about 105-120). The register
  records hardenings of the gift but not this one (`TOTALA-EXE-COMMANDS.md`, "What RWE does with this").
  Harmless and deterministic; worth one more line there.
- **Recipient must be a remote human with an outgoing alliance: DIFFERS, lockstep-by-design.** RWE
  tests `type == Human`, alive, `simulatesLocally`, and `arePlayersAllied` -- the symmetric team
  (rule 12). In lockstep every human is "remote" to every other machine and simulated identically, so
  this is the equivalent test. A **computer** is never a recipient in either.
- **Mapping every 450 ticks: MISSING, recorded.** `TOTALA-EXE-COMMANDS.md`: "Mapping, radar and
  line-of-sight carry no state (RWE's teams share sight and radar unconditionally, the [open]
  above)".
- **`22` over the wire: MISSING, recorded.** "A gift to or from a player simulated on another machine
  (a TA peer) is not applied, since RWE does not speak the `0x16` packet." The chapter confirms the layout
  the register gives (17 bytes: the type, a 4-byte subtype, two DPIDs, the amount).
- **Donors only in a network game: UNCLEAR.** The register says the pass runs "only in a network
  game"; RWE runs it in every game (`GameSimulation.cpp` about 5391), but a skirmish has one human, so
  there is never a recipient.

## 18. Economy snapshots and the view handshake

**Reference.** `SendPlayerEconomy` builds a **58-byte** `28` from local non-rejected players: **four
widened short kill and loss counters**, four raw resource and storage float dwords and six cumulative
doubles narrowed to floats, plus a **share flag**; a specified non-rejected target is attempted
directly, otherwise only non-rejected remote **human-kind** peers receive it. It is "an
observation/initialization message, not a resource transfer or shared deterministic economy
simulation". `HandlePlayerEconomy` rejects a null or rejected sender and **copies the fields only when no
active local player already has `shareVision` for that slot**; counter dwords are read as low shorts and
the float totals widened back to doubles, "so the wire round trip loses double precision". A non-zero
share flag **sets the local `shareVision`**, sends type `29` with `ackLos` one and `ackMapping` from the
existing `shareLos`, and may reply with its own flagged snapshot unless victory or LOS conditions stop it.
`29` records the recipient's `shareLos` and optionally `shareMapping`; "acknowledgments do not necessarily
mean the names' everyday visual interpretation". `BroadcastPendingViewState` loops local participants
that have created units and remote targets missing the share tables, sends flagged snapshots, returns
zero whenever it attempted a send, sleeps 250 ms even when idle, and treats remote computers as
`shareVision` participants though the ordinary broadcast picks humans.

- **The record's shape: MATCHES; the prefix is now explained and the register's guess is CONFLICT.**
  `TadResourceStats` has a 17-byte `prefix` described as "mostly zero. Two small values, one around
  offset 1 and one around offset 5, grow over the course of a game and are shaped like 16.16 fractions;
  ... even the field boundaries are not settled" (`io/tad/tad_events.h` about 314-326). The chapter's
  layout is: the type byte, then (by its list order) a **flag**, then **four 32-bit kill and loss
  counters**, then four floats of stock and storage and six cumulative floats: 1 + 17 + 16 + 24 = 58
  is the record's length, and 17 = one flag byte + sixteen bytes of counters. A kill and loss counter that grows over a
  game read as a 16.16 fraction is exactly the register's "two values that grow". The value read as byte
  0 -- "1 only in a game's closing seconds" -- would then be the **share flag** that is set when a
  snapshot is the flagged reply. Not verified; see `BT-NEW-CONFLICTS.md` Batch 15.
- **What RWE sends: DIFFERS.** `resourceStatsFor` (`TaWireTape.cpp` about 690-707) fills the four stored and storage
  floats and two triples and leaves the prefix zero (`TadResourceStats stats{}`): **no kill or loss
  counters and no share flag**. A TA player's score screen shows RWE's side with zero kills and losses,
  and RWE never raises the view handshake. `TA-NETWORK.md` "Open" ("what the joiner then showed for the
  host's economy was not looked at") is still open; the chapter says what to look at.
- **`29` view acknowledgement: MISSING.** RWE neither sends nor reads it. The chapter ties allied vision
  to the flagged `28` and the `29` acknowledgements ("per-slot handshake tables with separate map and LOS
  work elsewhere"). For RWE's own side vision is by team (`BT-GAME-PLAYERS-PARITY.md`); whether a TA ally
  shares its vision *with* RWE without the handshake is **UNCLEAR**, and is the likeliest reason an
  allied TA unit might not show on RWE's map.
- **Copy only when no local `shareVision`: UNCLEAR.** `applyResourceStats` (`TadPuppetDriver.cpp` about
  1190) applies the sender's economy to the puppet player unconditionally; the chapter does not apply
  it if the local machine already receives that player's vision through the share tables.
- **Counter dwords read as low shorts, doubles lose precision: not applicable.**
- **`BroadcastPendingViewState`'s 250 ms sleep: not applicable.**

## 19. The original's defects (not reproduced)

The chapter's last paragraphs and many of its sentences list defects: the shared receive-and-scratch
buffer overwritten while dispatching, the cached stale receive pointer, the impossible `<= 1 && >= 45`
range test, the 45-entry handler array indexed by the first byte, the unvalidated `CommandAllowed` index,
the fatal error on a join timeout with a freed argument block, unterminated name copies, null dereferences
on a zero script id or a missing owner, the duplicate-group seen array read before its range check, an
`AddNetPlayer` that returns one in every case, uninitialised return values, group-wide removal with no
guard, bare unchecked allocations, the "first type-one DPID" chat speaker, and the 250 ms sleep.

**RWE: not applicable.** None is reproduced, and the rule they illustrate -- everything from the network
is someone else's -- is `docs/SECURITY-AUDIT.md`'s. The one place RWE is no better than the original is
rule 8: a message is attributed to a player by the id **in** the message. See rule 20.

## 20. Faults found in RWE

These came from reading the TA-host path against the rules above. None is a hashed-state fault (an
own-clock game has no hash) and none was run; each is a trace by reading.

1. **Any datagram can remove the joiner from a running game.** `TaHostSession::handleDeletePlayer`
   erases the named peer **without checking who sent the message** (`TaHostSession.cpp` about 651-668),
   it is reachable over UDP (`handleUdp` -> `handleSystemMessage`), and the ids are small and
   predictable (the host's are constants `0x08D90E76`/`0x08D90E77`, the joiner's count down in pairs).
   `onPeerLeft` then pushes a `PeerLeft` item and `pollTaHostInbound` calls
   `removeRemotePlayer(simulation, link.remote)`, which **kills the commander and every unit of the
   real joiner**. The original takes the same message through DirectPlay's own session layer and the
   chapter does not say it authenticates the sender either, so this is a hardening gap and not a
   deviation; but here one forged packet ends the game for the TA player. Same class
   as `docs/SECURITY-AUDIT.md` (the lockstep side is covered there, "anyone who can spoof a peer's
   packets", line 91; the TA-host path is not mentioned).
2. **`PeerLeft` removes the joiner whoever left.** `GameScene::pollTaHostInbound`
   (`GameScene_tahost.cpp` about 263-267) does not compare `item.peer` with `link.peer`: it removes
   `link.remote` for **any** peer's departure. With fault 3 an intruder who joins and leaves also removes
   the real player.
3. **A peer can join after the game has launched, and every peer's packets are treated as the
   joiner's.** `handleCreatePlayer` has no closed state (rule 7) and the in-game `onAppData` stamps
   `item.sender = JoinerSender` on **every** peer's data (`TaHostGame.cpp` about 296-315). A second
   machine that performs the handshake (no password, nothing to check) can send `0x2c`, `0x0c` and
   `0x0b` records as the joiner. The session is also advertised as open (rule 3).
4. **A silent joiner is never dropped** (rule 15). The game runs for ever against a frozen puppet.
5. **Unsized live codes swallow the rest of a bundle** (rule 5): `0x1c`, `0x27`, `0x1d`, `0x25`, `0x13`.
   A peer that sends a player-drop or an integrity warning loses the `0x2c` behind it.
6. **A second joiner cannot be numbered or told the slot table** (rules 9 and 16: the `numbersSent_`
   latch, the `[host, peer]` table). Latent while one joiner is hosted.
7. **A full session drops the pending handshake silently** (rule 7); the joiner waits.
8. **Uncapped session and host-player names** (rule 3; also `BT-NETWORK-HAPINET-PARITY.md` gap 2).

## 21. Gaps to fix

Ranked by what could hurt a TA game or its player. None touches the simulation's hashed state.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **A forged `DELETEPLAYER` (or any non-peer departure) kills the joiner's whole side**, and a peer can join a running game. Bind system and application messages to the registered peer's address, ignore a `DELETEPLAYER` not from its own player, compare `item.peer` in `pollTaHostInbound`, and refuse `CREATEPLAYER` once launched. | fault (security, TA host) | code; add cases to `TaHostSession.test.cpp` and `io/malformed_input.test.cpp` |
| 2 | **No admission tests or reasons.** Closed, size 21, shorts zero and `0x50`, password, full; no `0x1b` encoder. The session never shows started (flag `0x20`). | MISSING | code: a reject encoder and the four tests; spec-room read of where "started" sits in the four user dwords |
| 3 | **Four codes the chapter sizes are unsized in the live walker** (`13` = 18, `1c` = 5, `1d` = 9, `25` = 5, `27` = 17). | DIFFERS (robustness) | code: five cases in `tadExpectedSubPacketSize`; one decides `03` |
| 4 | **No silence timeout on the TA-host path.** | MISSING | code: drop a joiner after a configurable silence (the original's trigger is `timeoutSecs * 30` ticks) |
| 5 | **`0x1e` is a start-position index, not "the game starts".** RWE always sends 1, has no random start, and does not wait for the `0x1f`. | CONFLICT (register wording) | spec-room read; then derive the index from the participant order and resend until acknowledged |
| 6 | **The commander bit of the options byte** (`0x08` by default) says Game Ends while the host plays Continues (`BT-FRONTEND-MULTI-PARITY.md` gap 2). | CONFLICT | spec-room read of bits `0x08`, `0x10` |
| 7 | **Feature actions (`0f`), state bits (`11`), impact (`0e`), attachment (`0a`) and ownership transfer (`14`) are neither sent nor applied.** Shared map state (wrecks, reclaim, fire) diverges between the two machines. | MISSING | decision; `0f` first, since the subtypes are now known (`fd`, `fe`, `ff`, else weapon index) |
| 8 | **The `28` carries no kill and loss counters and no share flag**, so a TA scoreboard shows zero for RWE, and the view handshake never starts. | DIFFERS | code: fill the 17-byte prefix as the chapter lays it out (flag, four counters); then a capture |
| 9 | **A dropped lockstep peer's units stay on the map; the chapter says the original kills them.** The roadmap's "which is what the original does" has no register behind it. | CONFLICT (design) | play-test in TA (leave a game with units) |
| 10 | **The automatic-drop and timeout numbers are RWE's** (10 seconds, automatic, no prompt; the original's `timeoutSecs * 30` ticks plus 120 seconds and a prompt). Not in section 88. | UNCLEAR (unrecorded divergence) | add a line to section 88 |
| 11 | **The session name is not capped at 16 + 15** and the host player's name is the game name uncapped. | DIFFERS | code: cap both (extends `BT-NETWORK-HAPINET-PARITY.md` gap 2) |
| 12 | **`taBuildPlayerNumber` is the group-assignment result** (`22`), and the host slot table (`26`) holds ten DPIDs. Names and comments in `TaBattleroom.h` and `TA-NETWORK.md` ("numbers every player") are stale. | documentation | edit the wording; keep the capture facts |
| 13 | **Directional alliances (`23`) are neither sent nor read.** | UNCLEAR | a capture of a team game between RWE and a TA |
| 14 | **The `0x19` payload is three bytes with a subtype** (BT-178), and RWE reads two. | stale (already tracked) | none new |

No source file was changed.
