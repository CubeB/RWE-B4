# Total Annihilation on the network

What a real `TotalA.exe` sends and expects when it plays over DirectPlay, and what that means for
RWE taking part in such a game. Established on 2026-09-26 by the spike in issue #386, with the
tools in [`tools/ta-net/`](../tools/ta-net/README.md): two copies of the GOG executable
(MD5 `8e74a1dffa1f5988624c52048f5b20cd`, the one every `TOTALA-EXE.md` finding was read from)
captured playing each other, and then one of them joining and playing against a host that is a
Python script.

This document is the wire and the behaviour behind it. The subpacket table and the payload
decodes are shared with demos and live in [`TA-DEMOS.md`](TA-DEMOS.md), which this document
does not repeat.

## The short version

- **TA is owner-authoritative, not lockstep.** Each machine simulates its own player's units and
  broadcasts their state, and a remote unit is whatever its owner last said it was. RWE's
  lockstep mode cannot talk to it; a second mode that owns some units and puppets the rest can.
- **Live traffic is the demo format in a DirectPlay envelope.** Every packet of three captures,
  both directions, decrypts, decompresses and walks with the transforms and subpacket table that
  `src/rwe/io/tad/` already has: no checksum failure and no unsized subpacket.
- **A program that is not TA can host a game a real TA joins and plays.** The fake host
  answered the DirectPlay handshake, held a battleroom, passed unit sync, launched, and then kept
  its own commander in the game, standing and walking, from `0x2c` records it built itself.
- **RWE can host one from its own fields rather than a recording.** `TaBattleroom` builds the
  battleroom, echoes unit sync and runs the launch, and `ta_host_probe --battleroom` is it in
  front of a socket. Checked against the scripted joiner of #430, which is not TA either: all nine
  of its checks pass over 60 seconds of play.
- **Hosting needs no unit checksum.** Unit sync is the joiner sending its CRCs to the host; a
  host that echoes the joiner's ids back is accepted. The content-derived checksum that two passes
  failed to reproduce (TA-DEMOS D7) matters only for joining a TA host.
- **The owner decides health and death.** An attacker's machine reports damage and shows it,
  but a unit dies only when its owner says so, and the owner's next full-state record overwrites
  the attacker's tally.
- **The full-state record is authoritative in the other direction too.** Each tick's `0x2c` ends
  with the full state of one of the sender's unit slots; a record saying a slot is empty deletes
  whatever the receiver has there. A peer must describe every unit it owns, correctly, once a
  cycle.

## Test bed

GOG TA under Proton on Linux, launched as a non-Steam game.

- **Proton's built-in DirectPlay can host but cannot be found.** TA binds TCP 2300 and UDP 2350,
  but nothing listens on port 47624, where every joiner sends its EnumSessions request, so no one
  can see the game. `protontricks <appid> directplay` installs Microsoft's DirectPlay into the
  prefix, and with it the `dplaysvr` helper that owns 47624. Any Linux player who wants to play
  against RWE this way needs the same step.
- **TA refuses to run twice in one prefix.** It imports `CreateSemaphoreA`/`OpenSemaphoreA`
  beside `EnumWindows` and `SetForegroundWindow`, and a second launch brings the first window
  forward and exits. Named objects are per wineserver, so a copy of the prefix is a second
  machine as far as TA can tell. The second instance takes TCP 2301 and UDP 2351. Port 47624
  stays with the first, which is fine: only a host needs it.
- **`dplaysvr.exe` outlives TA** and keeps 47624; stop it before anything else hosts.
- `win32.dll` (a sound shim), `online.dll` (the online-links buttons, byte-identical to TA
  Forever's) and `mptaext.dll` (the Mplayer lobby hook) ship beside the executable. None of them
  is a network layer: `TotalA.exe` imports `DPLAYX.dll` directly.

## DirectPlay

TA uses DirectPlay 4's TCP/IP service provider, dialect `0x000E`.

**Every socket receives only.** Each side listens (host TCP 2300 and UDP 2350; a joiner on the
same machine 2301 and 2351) and opens its own outbound connection to the other's listening port
to send. Replies go to the address in the message's header, not back down the connection the
request came on.

**A message over TCP** starts with a 20-byte header: size in the low 20 bits and the token
`0xFAB` in the high 12 of a `u32`, then a `sockaddr_in` (family 2, big-endian port, IP, 8 bytes
of padding) giving the sender's reply address. **The IP is left zero**, meaning "use the source
address of this packet". A system message follows with `"play"`, a `u16` command and the `u16`
dialect. Application data follows with `u32` from and `u32` to player ids (to 0 is everyone),
then the TA packet. **Over UDP, application data has no header at all**: from, to, TA packet.

**Joining** is plain DirectPlay 4:

| Joiner | Host |
|---|---|
| ENUMSESSIONS, by UDP broadcast to 47624, with a null application GUID and its own reply port | ENUMSESSIONSREPLY by TCP: the session description and its name |
| REQUESTPLAYERID, flags 9 (a system player) | REQUESTPLAYERREPLY with an id |
| ADDFORWARDREQUEST, carrying its system player and its TCP and UDP addresses | SUPERENUMPLAYERSREPLY: the session and every player with their addresses |
| REQUESTPLAYERID, flags 8 | REQUESTPLAYERREPLY with a second id |
| CREATEPLAYER, its named player | SESSIONDESCCHANGED |

The host hands out the ids, and nothing checks them against anything else, so a host can reuse
the ids of a recording. **TA's application GUID is `{99797420-F5F5-11CF-9827-00A0241496C8}`.**
The session name is the game's name padded to 16 characters followed by the map's, and the
session description's `dwUser1` high byte is the host's options byte (below). The host sends
SESSIONDESCCHANGED again whenever an option changes and twice at launch. DELETEPLAYER, for both
of a player's ids, is how a player leaves.

## TA packets

`decrypt`, then `decompress` with a three-byte header, then skip seven bytes (type, checksum, a
`u32`), then the subpacket table: exactly `tadDecrypt`, `tadDecompress` and
`tadSplitSubPackets`. Uncompressed packets (type `0x03`) were accepted for unit sync and status,
where TA itself sends compressed ones.

**The `u32` is a per-sender count, and a reply is the one value it is not.** It is `0xffffffff` on
every reply. On everything a sender starts itself it falls by one per packet: a joining TA's own
traffic runs `0xfffffffe`, `0xfffffffd`, `0xfffffffc` and on, and in all three recordings both
peers begin one below `0xffffffff`, so a sender's own status, its pings, the launch exchange and
the in-game `0x2c` all carry a count, while the unit sync that answers someone else's request
carries `0xffffffff` and does not consume one. It is not a clock and nothing seen depends on it --
a replay writes `0xfffffead`, and a real host's first own packet in a recording can already be
seventy packets down -- so `0xffffffff` is a safe value to send anywhere.

## The battleroom

| Subpacket | What it carries |
|---|---|
| `0x20`, 186 bytes | A player's status. Byte 157 is the host's **options** (below). Byte 156 is the player's state; gpgnet4ta reads bit `0x20` as ready. |
| `0x24`, 6 bytes | `u32` player id, `u8` team. An untouched battleroom sends 5, no team. |
| `0x02`, 13 bytes | Ping: the requester's tick, the responder's tick (0 in a request), the requester's player id. Requests go to everyone, replies to the requester. |
| `0x07`, `0x06` | Filler around the status exchange. |

**The tick in a ping is a wall clock, near `GetTickCount`, and the reply's second tick is the
latency.** Two peers a second apart in uptime agree on it to the millisecond, and in the captures
the second tick in a reply is 0 to 22 above the tick the request carried, which the requester
reads as the peer's ping. So a reply echoes the requester's tick unchanged and adds the
responder's, and it is only believable if it echoes a tick the requester sent. A request is 20
bytes and uncompressed; its reply carries `0xffffffff` where the request carried a count.

**The status record's body.** Of a `0x20`'s 186 bytes four are placed by what a
capture changes: the map name in bytes 1-32, NUL-padded ASCII; the DirectPlay id
at `0x91`; the state byte at 156; and the options byte at 157. Bytes 158-169 are
the game settings the session description also carries (4, 10, 10, 250 and
`0x0103`), and 141-144, 170-185 are sixteen bytes the corpus does not decode
(TA-DEMOS D8). Bytes 151 and 152 are the only ones that differ between two
players, and every capture that reached a launch had both of its peers on the
same side, so nothing there can be told apart from a colour or a lobby slot.
`TaBattleroom` therefore builds the record over a template taken from a recorded
host's status, writes the four fields it knows and copies the rest; its header
lists the ranges.

**The options byte**, read from a capture in which the host changed one battleroom option at a
time:

| Bits | Meaning |
|---|---|
| `0x06` | LOS: True `0x06`, Circular `0x02`, Permanent `0x00` |
| `0x01` | set for Unmapped |
| `0x08`, `0x10` | Game Ends and Deathmatch; neither is Continues |
| `0x20` | cheats allowed (from gpgnet4ta; TA shows it, but `+los` did nothing with it set) |
| `0x40` | always set; not identified |

The default is `0x4f`. A mapped game with permanent LOS, `0x48`, shows the other player's units
without an alliance or cheats.

**A silent host is offered for rejection.** A real host sends a `0x07` and a
`06 26 20 24` status about every two seconds and answers every ping. When the fake host stopped
sending, the joiner put up "*player* will be rejected in *N* seconds" with a Reject Now button;
with the keepalive and live pings it waited in the battleroom indefinitely. **Pings must be
answered live**: replayed ones carry another game's clock and show as an absurd ping.

**Rules live in the host.** TA's own host will not launch a game in which every player is on one
team, but a joining TA does not check: the fake host launched one with both players on team 0.
An RWE host has to enforce such rules itself. It is also the only side that can: a
joiner picks no map and is not asked to agree on one, so the host compares what
each joiner's `0x20` names in bytes 1-32 against its own and refuses a launch on
a disagreement. A field that is not NUL-padded ASCII names no map and so
disagrees about nothing: the scripted joiner of #430 writes its own name into
those bytes in UTF-16, and a host that read a map out of that would refuse a
game over a field it had not understood.

**The team a battleroom sends is 5** (`TaNoTeam`), and 5 is what a player that
has chosen no team says, so the rule reads a player on 5 as on no team in
particular: a one-player game, and a game half of whose players have not picked,
are not the game TA's host refuses.

## Unit sync

The `0x1a` exchange that TA-DEMOS reads as a table is a conversation:

1. The host sends one sub-type 0 record, then each of its unit ids as a sub-type 3 record with
   status `0x0001` and limit `0xffff`, sorted.
2. The joiner sends sub-type 1 with its count of unit types (278 in the GOG install), then
   sub-type 2 for each: its id and its CRC.
3. The host answers each batch of the joiner's ids, in the joiner's order, with sub-type 3
   records of status `0x0101`: in use.
4. The joiner sends sub-type 4 records counting what it has received, until the count reaches
   `1 + 2n` (557), and repeats that until launch.

**Only the joiner sends CRCs, so only the host checks them.** The fake host sends the sub-type
0 record at CREATEPLAYER, and answers each batch of the joiner's sub-type 2 records with a
`0x0001` and a `0x0101` record per id. The joiner counted to 557 and launched as it does with a
real host. The ids are content-derived and the fake host never computes one: it only echoes. A
TA host checks a joiner's CRCs against its own, so **an RWE that joins still needs them**,
computed or harvested from demos for the data sets that matter.

## Launch

The host sends `0x08` (loading started), both sides send `0x2a` loading progress to `0x64`, the
host sends `0x1e` and the joiner `0x15` and `0x1f`, and each moves to UDP with `0x15`, `0x07`,
`0x2a`, a status `0x20` and team `0x24`, its commander's `0x09`, and `0x11`. The replay sends the
host's side of this on the recorded schedule, and the joiner's loading kept pace with it.

The host's ladder is the four steps `0x00`, `0x26`, `0x36`, `0x64` at 200 ms each, its state byte
goes `0x01` in the battleroom, `0x02` while loading and `0x22` in game over TCP (`0x32` over UDP),
and SESSIONDESCCHANGED goes out twice: the captures have it once as the `0x1e` is sent and once
after the move to UDP. `TaBattleroom::LaunchParams` takes the commander's type index, unit id,
position and rotation, because they are the game's to decide and not the wire's; the `0x11` byte
after the unit id is `1` in every capture and is passed through unchanged.

**A host is a player in the game from the first `0x2c` it sends**, and each `0x2c` ends with the
full state of one slot, so a host that owns one unit has to describe it once every `maxUnits`
ticks or the joiner declares it eradicated. `ta_host_probe --battleroom` does that with a fixed
commander record; a game will describe its own.

## In game

**Staying in the game takes only the `0x2c` stream and pings.** The fake host kept the host
player in one game for more than five minutes sending nothing else: no `0x28`, `0x10` or `0x11`.
TA sends its `0x2c` six ticks to a UDP message, thirty ticks a second.

**The full-state record is authoritative.** Each `0x2c` ends with the full state of block slot
`tick % maxUnits` (TA-DEMOS, "The full-state record"). An idle record,
`2c 0b00 <tick> ffff 0100`, is "no unit has news, and this slot is empty". The fake host's first
keepalive sent that for every slot; when the tick reached the commander's slot, the joiner
declared the host **eradicated**. Sending each owned slot's full state on its tick keeps the
units alive. A peer therefore has to describe every unit it owns, once a cycle, and describe it
correctly: an owner that gets a slot wrong deletes its own unit on every other machine.

**Movement is the owner's path plus its corrections.** A waypoint entry
`[(current x, z), (next), (last)]` tells the receiver where a unit is going, and the receiver's
own stub navigator steers it there; an entry with no waypoints stops it. The script walked its
commander 4,058 units by sending one entry, advancing its own model at the recorded 1.2 units a
tick, and putting the model's position, heading and speed in the commander's full state each
cycle. The joiner saw a smooth, straight walk with the right facing, over trees and hills, with
small stutters every cycle and no large jumps: the stutter is the owner's correction meeting the
receiver's steering, which varies speed with the ground where the script's model did not.
**Heading is `atan2(dx, dz) + 180°`** in 65536ths of a turn, fitted to the recorded commander.

The Python codec in `tanet.py` re-encodes all 5,153 ground `0x2c` of both captures that carry
them, both senders, byte for byte. Its type field is 9 bits for the GOG install's 278 types.

## Damage and death

The joiner force-attacked the fake host's metal extractor (170 health). The fake host never
answers damage, and it went on sending the extractor's last full state with its health at 170.

- The joiner sent 20 `0x0d` shots and 20 `0x0b` damage records, `victim, attacker, 60`: 1,200
  damage against 170 health.
- **The health bar fell**: the attacker applies its hits to its own copy, for display.
- **It never died**: only the owner's `0x0c` kills a unit, and none came.
- **Health returned to full** at the next full-state record for that slot.

This is what the corpus shows (TA-DEMOS, "Every event's sender") and what the binary shows (the
`[+0x96][0x73]` owner check at `0x489ED1`), now seen from a live game. Two consequences for RWE:
for a unit RWE owns, it applies incoming `0x0b` to its own simulation, decides the death, and
sends the `0x0c` and the true health; for a TA unit RWE attacks, it sends `0x0b` and shows the
damage but waits for TA's `0x0c` before the unit dies.

It also means **an owner that drops `0x0b` has immortal units**, and nothing on the wire can
prevent it. That is inherent to playing with TA, and players should be told so.

A player leaving is its own quit sequence: its commander's `0x0c` with cause 8, a burst of
`0x0f` from the explosion, and DELETEPLAYER.

## What this means for RWE

The estimate made before the spike, revised by what it found:

| Phase | Before | Now | Why |
|---|---|---|---|
| 1. DirectPlay | 2–5 wk | 2–3 | The session layer TA uses is the table above and fits in a few hundred lines. |
| 2. TA framing | ~1 | < 0.5 | Already in `src/rwe/io/tad/`; only the DirectPlay envelope is new. |
| 3. Battleroom and launch | 4–8 | 3–5 | Unit sync by echo removes D7 for hosting, and the options byte and team are read. Building a status `0x20` from fields rather than a recording is still to do (TA-DEMOS D8). |
| 4. Owned and remote units in the sim | 8–12 | 8–12 | Untouched by the spike, and still the largest part. |
| 5. Sending | 3–5 | 2–4 | The mandatory set is small and the `0x2c` codec exists; `0x10` is still needed for remote players to see animation. |
| 6. Receiving | 5–8 | 5–8 | Decoders exist; applying them is the work. |
| 7. Hardening | 4–6 | 4–6 | |
| **Total** | **28–47** | **24–39** | |

The order of work in #386 stands: puppet playback of demos (#387) is the receiving half, and
spectating a live game comes before playing in one.

## TA Forever

TA Forever's `gpgnet4ta` relays DirectPlay traffic between players and rewrites only the addresses
inside it, so a peer that speaks the protocol above would pass through it in principle. That was
deliberately not tested. Joining TA Forever's games as though RWE were TA would be passing one
program off as another in a community's rated play; if RWE is ever to take part there, it should
be with that project's agreement and identifying itself as RWE.

## Open

- **Joining a TA host**: needs real unit CRCs, computed (D7) or harvested from demos.
- **A status `0x20` built from fields**: the fake host replayed the recorded host's.
- **Game speed**: a player's `0x19` reached a host that ignored it; what TA does when a peer
  does not follow a speed change was not tried.
- **Resource statistics**: the host sent no `0x28` after the replay and nothing complained, but
  what the joiner then showed for the host's economy was not looked at.
- **`0x40` in the options byte**, and why `+los` did nothing with cheats allowed.
- **A side and a colour in a `0x20`.** Neither is decoded, and the captures cannot
  separate them: both peers in every recording that reached a launch sat on the same
  side, so the only two bytes that differ between players are candidates for either. A
  game assigns sides from its own player table.
