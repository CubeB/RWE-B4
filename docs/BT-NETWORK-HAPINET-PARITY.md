# DirectPlay transport wrapper, sessions, player records and enumeration parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GAME-DATA-FILES-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `network-hapinet` (44 services from `0x4c9530` to `0x4caa40`): the
thin wrapper the game uses over Microsoft DirectPlay and DirectPlay Lobby -- send and receive,
creating, joining and leaving a session, enumerating sessions and providers, adding and
removing players, the lobbied-connection path, and a few report stubs. This is the finding that
implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

**Scope.** RWE does not call DirectPlay. Its TA-compatible networking **re-implements the
protocol on the wire** (`net/ta/TaDirectPlay.*`, `TaHostSession.*`), so what is audited is what
the wrapper's calls make DirectPlay put on the network and what a real `TotalA.exe` joiner
therefore sees and expects. RWE's own lockstep transport (`net/`, `LanDiscovery`, `Stun`,
`NatTraversal`) is RWE-original and is **not** audited against the reference.
`docs/TA-NETWORK.md`, "DirectPlay", is an **executed-against-real-TA record and outranks a
static reading**; every disagreement with it is a **CONFLICT**, not a correction.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\network-hapinet.md`). Nothing
  else under that tree was opened beyond the sibling network chapters: no source, no
  decompilation, and none of its source links were followed. The chapter describes the COM
  calls and numeric flags "without claiming externally executed provider behavior". The
  spec-room discrepancy register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was
  read, read-only; no row concerns this layer.
- **Evidence class.** A **static reading** by that project: nothing was executed. Weaker than
  `TOTALA-EXE*.md` and than `TA-NETWORK.md`'s captures.
- **The RWE half** is a read-only audit of `net/ta/TaDirectPlay.*`, `net/ta/TaHostSession.*`,
  `net/ta/TaHostGame.cpp`, `net/ta/TaBattleroom.cpp` and `docs/TA-NETWORK.md`, at the working
  tree of 2026-10-17. Nothing was built, run or changed. Another agent had uncommitted edits
  in the tree, so line numbers are "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Result conventions and the empty trace (not applicable)
3. Guaranteed and ordinary sends
4. Defaults, and the number of players
5. Creating a game: the session description
6. Updating the description, joining, leaving
7. Enumerating games
8. Adding a player: names and the 21-byte data block
9. Providers, connections, the lobby and the report stubs (not applicable)
10. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| send / receive, guaranteed flag | `TA-NETWORK.md`: battleroom over TCP, game over UDP | rule 3 |
| create / update / join / quit | `TA-NETWORK.md` "Joining" table, "SESSIONDESCCHANGED", "DELETEPLAYER" | rule 5, 6 |
| session user values | `TA-NETWORK.md`: "`dwUser1` high byte is the host's options byte" | rule 5 (the other three, and the player count) |
| enumeration | `TA-NETWORK.md`: ENUMSESSIONS by UDP broadcast to 47624 | rule 7 |
| add player, initial data | `TA-NETWORK.md` "Open": "the name field is four characters wide ... nothing says whether DirectPlay would take a longer one" | rule 8 answers it |
| providers, lobby, report stubs | none | rule 9 |

## 2. Result conventions and the empty trace (not applicable)

**Reference.** There is no uniform success value: send and receive return the DirectPlay result
(zero is success); the session and player helpers convert success to one; enumeration returns a
count or minus one; interface creation exposes the raw result. The trace function does nothing,
so none of the "trace" calls writes a log. A table gives English text for 28 DirectPlay error
codes and a fallback.

**RWE: not applicable.** RWE has no DirectPlay interface to wrap; `TaHostSession` reports
success as a bool or as a callback. The error text table is a UI nicety of the original's.

## 3. Guaranteed and ordinary sends

**Reference.** A process-wide switch decides whether an ordinary send is guaranteed; the setup
routine turns it **off**, the lobbied-connection path turns it **on**, and a separate
"send guaranteed" entry point always asks for the guaranteed form. The wrapper adds no header,
queue or retransmission of its own; a missing interface returns the "no interface" code.
DirectPlay over its TCP/IP provider sends a guaranteed message over a stream connection and an
ordinary one as a datagram.

**RWE: MATCHES the observation.** `TA-NETWORK.md` records the same split: the battleroom,
unit sync and launch go over TCP, and in game the `0x2c` stream and pings go over UDP. RWE
mirrors it with `TaTransport::Tcp` for everything before the launch and `Udp` after
(`TaBattleroom.cpp`, `moveToUdp`). The consequence the chapter makes explicit and the register
did not: **in-game traffic is unreliable by design** (the ordinary send), which is why a lost
packet is never retransmitted by the wrapper and why `network-packets` has an empty gap callback
(`BT-NETWORK-PACKETS-PARITY.md` rules 2 and 7). RWE's loss tolerance (jitter buffer, bounded
gap wait) fits.

## 4. Defaults, and the number of players

**Reference.** The defaults routine sets the maximum number of players to **sixteen** and the
enumeration timeout to **1500** milliseconds. The create routine takes the configured maximum
into the description it opens.

**RWE: MATCHES the capture, UNCLEAR against the default.** The host advertises **ten**
(`TaSessionDescription::maxPlayers{10}`, `TaHostSession::MaxPlayers`, `makeSessionDescription`
about 866-876) and counts itself as one, so at most nine joiners are accepted
(`TaHostSession.cpp` about 615). Ten is what the captures show and what TA's ten-slot player
array holds; the sixteen is the wrapper's default, which the caller overrides. Not a
disagreement in effect. The 1500 ms timeout is the *joiner's* wait for replies and needs nothing
from a host.

## 5. Creating a game: the session description

**Reference.** The create routine requires the queried interface, copies **sixteen bytes** of
the session name into the network object (without forcing a terminator), zeros the local
description and fills it: size **80**, **flags zero**, the configured maximum, the application
GUID, a pointer to the name, the password pointer (null for a normal game) and **four caller
user values**. It opens the session with the "create" flag, then reads the actual description
back and copies the session **instance GUID** into the local record. The return value follows
Open alone. The enumeration callback (the joiner's side of the same record) later copies the
name into a **32-byte** field with no length check, the four user values, the instance GUID and
the **maximum** players, and **does not copy the current-player count**.

**RWE: MATCHES the record; two differences matter.**

- **Size, flags, application GUID, maximum: MATCHES.** `TaSessionDescription` is 80 bytes, flags
  zero, the application GUID `{99797420-F5F5-11CF-9827-00A0241496C8}`; no password flag
  (`0x400`) is ever set. The wrapper's "password required" test is the raw `0x400` bit, so a
  host that never sets it joins freely, as RWE's does.
- **User values: MATCHES, with a naming trap.** The description carries four 32-bit user values.
  `TaSessionDescription` names its fields by their offsets (`user3`, `user4`, `reserved3`,
  `reserved4`) while `TA-NETWORK.md` calls the options field the high byte of `dwUser1`: these
  are the same dword (`user3` in the struct is DirectPlay's first user value; `reserved3` and
  `reserved4`, `0x000A000A` and `0x010300FA`, are its third and fourth, equal to the game
  settings in bytes 158-169 of the `0x20` status). Value one holds the options byte on top, a
  constant one, and the **current player count in the low byte**
  (`makeSessionDescription`: `0x00010000 | players`). That low byte is the only way a joiner
  learns how many players are in a game from the enumeration, because the callback does not
  copy the count, so a host that did not keep it current would show a stale list
  entry. RWE recomputes it from the peer map on every description. MATCHES.
- **Instance GUID: DIFFERS, latent.** A real create reads a **fresh** instance GUID from
  DirectPlay for every session. RWE sends the constant from the configuration
  (`TaHostConfig::instanceGuid`, the bytes of the captured session, `TaHostSession.h` about
  120) and `TaHostGame` never overrides it. Joining is by session instance GUID in the wrapper's
  join call, so **two RWE hosts on one network are the same session** to a TA, and a host that
  is restarted is indistinguishable from the one before it (a joiner's cached list entry points
  at a dead game). Fix: generate sixteen random bytes per run.
- **Session name length: UNCLEAR, a possible overflow in a real TA.** The name is the game name
  padded to 16 characters then the map name (`taSessionName`, `TaDirectPlay.cpp` about 621-630).
  The padding is right (the create routine copies sixteen bytes), but a game name longer than 16
  is not cut, and a long map name takes the total past the 32-byte field the joiner's callback
  fills with an unbounded copy. A name of 32 characters or more would overflow a real TA's list
  entry. Cap the total at 31 and the game name at 16.

## 6. Updating the description, joining, leaving

**Reference.** The update routine copies the short name, rewrites size, name and the four user
values and sets the session description with flags zero; it preserves other fields. The join
routine zeros the description, fills size, application GUID and the **requested instance GUID**
and opens with the "join" flag; on success it reads back the negotiated description, updates
the instance GUID and copies the session name. No password is set. Leaving a game is a **close**
of the session, which does not release the COM objects (a separate uninitialise does).

**RWE.**

- **Update: MATCHES.** `TaHostSession::sendSessionDescChanged` sends the description again
  whenever an option changes and twice at launch (`TA-NETWORK.md`).
- **Join: not implemented.** `TA-NETWORK.md` "Open" lists joining a TA host as future work; the
  requested-instance-GUID rule above is the part it will need (the joiner must present the GUID
  the enumeration reply carried).
- **Leaving: MATCHES.** A player leaves by `DELETEPLAYER` for both of its ids
  (`sendHostDeleted`, `TaHostSession.h` about 183), which is what the wrapper's destroy-player
  call sends before the session close.

## 7. Enumerating games

**Reference.** The enumeration call uses flags `0x81` and the stored timeout and, while the
result is the DirectPlay "pending" code, repeats the call, pumps one window message and pops
one game key; Escape cancels and any other key is discarded. A final result of exactly zero
returns the count; anything else, including Escape with results already collected, returns
minus one. The callback appends a record of 84 bytes (name, four user values, GUID, maximum)
and skips timed-out entries.

**RWE: MATCHES the host's half.** A host answers the UDP broadcast to **47624** with a TCP
reply carrying the description and name (`TA-NETWORK.md`, "Joining"; `TaHostSession` enum
acceptors). The loop, the Escape handling and the 84-byte record are the joiner's and need
nothing from a host. Note that the joiner's list is built from **user values and the instance
GUID only** (rule 5), so the fidelity of those two decides how a game looks in TA's list.

## 8. Adding a player: names and the 21-byte data block

**Reference.** The add-player call creates a player with a short and a long name and an initial
data block of **21 zero bytes**: a **seventeen-byte name field** (a sixteen-byte bounded copy,
no guaranteed terminator) followed by **two 16-bit values for the version**. The wrapper does
not compare the versions and does not reject an incompatible client; the higher layers read them
(`BT-NETWORK-UNIT-SYNC-PARITY.md` rule 5 uses the remote version to decide whether the script
checksum is compared).

**RWE.**

- **Answers an open question in `TA-NETWORK.md`.** "The name field is four characters wide
  because both captures have a four-character name. Nothing says whether DirectPlay would take a
  longer one." The wrapper's block says the field is **17 bytes with a 16-character bounded
  copy**, so names up to sixteen characters fit and a longer one is cut without a terminator.
- **The host's own record: MATCHES the captured shape, DIFFERS in content.** RWE sends 21 bytes of
  zeros with byte 19 set to `0x50` (`TaHostSession.cpp` about 902-903), copied from the capture.
  Byte 19 is in the *second* 16-bit value (bytes 19 and 20), so the pair reads as
  zero and `0x0050`. The reference's version rule ("at least 2, or 1 with a minor of at least 2")
  cannot be applied to that pair without knowing which short is the major, and neither source
  says: the capture's real host would, by the rule, report a version that makes a TA
  authority skip the script-checksum compare. **UNCLEAR**; settle by a spec-room read of where
  the version shorts are written, since it decides whether RWE joining a TA host needs the
  second checksum.
- **The host player's name is the game name**: `makeNamedPlayer(hostPlayerId, config.gameName,
  ...)` (`TaHostSession.cpp` about 899) and, unlike the session name, with no cap. A joiner
  shows it as the host's name in the battleroom. A real host shows its nickname. Cosmetic;
  cap at 16.
- **A joiner's name** is kept as sent and displayed by RWE (`peerName`); the RWE side has its own
  bounds, the TA side's 16 does not apply to what RWE receives.

## 9. Providers, connections, the lobby and the report stubs (not applicable)

**Reference.** Two provider enumerators exist with different output schemas and filters (one
skips four configured provider GUIDs and allocates opaque connection records the caller must
free; the other lists versions); the lobbied path creates a lobby interface, loads connection
settings, optionally replaces the password (only when the argument is nonzero) and connects.
Four report entry points are stubs: the online-flags query returns zero, the game report and
chat report return one without sending, and the callback registration stores two addresses it
never calls. So an "enabled HAPINET route" proves nothing about delivery.

**RWE: not applicable.** RWE speaks one provider (TCP/IP) on fixed ports, needs no lobby
interface and reports through its own launcher and master server. The password rule matters
only if a password game is ever hosted: the `0x400` bit and a join that sets no password mean a
**normal TA join cannot enter a password game**, so RWE should never set the bit for a game a TA
is meant to join.

---

## 10. Gaps to fix

Ranked by what could hurt a TA game. None touches the simulation.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **The session instance GUID is a constant**: two RWE hosts (or a restarted one) are one session to a TA. | DIFFERS (latent) | code: sixteen random bytes per run in `TaHostGame` |
| 2 | **Session name, game name and host player name are uncapped.** A real TA copies the session name into a 32-byte field unbounded and the player name into a 17-byte field. | UNCLEAR (possible overflow in TA) | code: game name at most 16, session name at most 31, player name at most 16 |
| 3 | **Which of the two version shorts is the major, and what a real host sends** decides whether joining a TA host needs the script checksum. | UNCLEAR | spec-room read of the version write |
| 4 | **`TaSessionDescription` names `dwUser1` as `user3`.** Misleading, not wrong. | documentation | rename the fields or comment the mapping |
| 5 | **In-game traffic is unreliable by the wrapper's design**; `TA-NETWORK.md` does not say so. | documentation | one sentence in `TA-NETWORK.md` "DirectPlay" |
| 6 | **Maximum players: 10 on the wire, 16 in the wrapper's default.** | MATCHES the capture | none |

No source file was changed.
