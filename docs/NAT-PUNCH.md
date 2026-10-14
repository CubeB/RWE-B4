# Playing across routers without a server of ours

Two machines on different home or mobile connections cannot reach each other
by address: each sits behind a router, and behind a carrier's router on 4G,
5G and satellite, so the address a machine has is not one anybody can send to
and there is no port to forward. RWE does it by *hole punching*, with no server
of its own: the people pass two short codes to each other, and each machine
asks a public STUN server only "what address did this datagram come from?".

Everything here is `src/rwe/net/`:

| File | What it is |
|---|---|
| `Stun.{h,cpp}` | RFC 5389 Binding request and response, the server list, and `ReflexiveProbe`, which asks every server at once from one socket and says whether they agree |
| `InviteCode.{h,cpp}` | `RWE-XXXX-XXXX-XXXXX`: an IPv4 address and port in thirteen base32 characters with an 8-bit check |
| `NatTraversal.{h,cpp}` | finds the outside address of the discovery socket and of the game port, holds the game port open, and punches toward the peers the lobby names |
| `LanDiscovery` | the lobby's transport; gained `setForeignDatagramHandler`/`sendRaw` so STUN can be asked *from its socket*, and the `gaddr` key |
| `stun_test_util.h` | `FakeStunServer` on loopback, shared by the tests and `ui_probe` |

## What happens

```
host                         STUN                       guest
 |  /invite                                                 |
 |-- binding request (from 1338, from 1337) -->             |
 |<-- you are 203.0.113.5:40001 / :40002 --                 |
 |  "RWE-...."  -------------- chat, mail, anything ------> |
 |                                          paste into the TCP address box
 |                                           |-- binding request (1338, 1337) -->
 |                                           |<-- 198.51.100.7:51000 / :51001 --
 |  <------------------ "RWE-...." (the reply) ------------ |
 |  paste it into the battleroom's message box              |
 |-- announcements to 198.51.100.7:51000 --------------->  | (the guest has been probing the
 |                                                          |  host's address all along; the
 |<-- probe/claim, with gaddr=198.51.100.7:51001 -----------|  host's router now expects it)
 |-- lobby, with gaddr=203.0.113.5:40002 ----------------> |
 |<=== game port to game port, punched, then the game ====>|
```

1. **`/invite`** in the host's battleroom (the message box) starts the lookup. The
   code is the host's *discovery* socket (UDP 1338) as the STUN server saw it.
2. The guest pastes it in the **TCP address** dialog -- Ctrl+V works in any box --
   and the list probes that address once a second like any typed one. This machine
   finds its own outside addresses and shows its **reply code** on the status line,
   copied to the clipboard.
3. The host pastes the reply into the message box. It is not said to the room.
   The host's announcements now go to that address, which is what makes the
   host's router expect what the guest has been sending.
4. The lobby then runs over those two discovery sockets exactly as it does on a
   LAN: claim, seat, ready, chat. What is new is that each side announces the
   *game port's* outside address in a `gaddr` key (host's announcement, guest's
   claim), and the seats' addresses in the lobby are those, not "source address
   and 1337".
5. While in the lobby each side's `NatTraversal` sends a small datagram to every
   seated peer from the **game port**, which is what opens the route and what
   tells the host `X is reachable directly`.
6. When the game starts, `MainMenuScene::startGame` destroys the multiplayer flow
   first, closing the punch socket; `LoadingNetworkService` binds the same port a
   moment later and the router reuses the mapping.

A LAN game never starts any of this: `NatTraversal` is idle until `/invite` or a
code is entered, and `gaddr` is only believed by a machine that is itself
traversing, so a LAN guest keeps using the source address.

## Keeping the address true

A router drops a mapping that has carried nothing, and makes another -- at another
port -- the next time the socket sends. On a carrier's NAT that can be ten seconds.
A code is read out a minute after it was made, so the first version of this, which
asked STUN once, handed out ports that were nobody's by the time they were used
(found on the first real test, 2026-10-13: the host said "Waiting for that player..."
for ever). `NatTraversal` now asks the servers again every `keepaliveInterval` (4 s)
from both sockets. That keeps both mappings alive, and `ReflexiveProbe::refresh`
follows the answers: if the router moved the socket the status changes, and the
lobby says `Your address changed. New invite code: ...` on the host, and shows a new
reply code on the guest.

The host's room also says what it can tell: `Reply code accepted (a.b.c.d:port)`,
then `Hearing from a.b.c.d:port` when anything has arrived on the discovery socket
from that address (the route is open), then `X is reachable directly` when the game
port's datagram has. Stuck before the second line means the guest's packets are not
getting through; stuck after it means the route is fine and something above it is not.
A reply from the host's own outside address is said to be on the same router.

## The version check

Two builds that are not the same commit are not the same simulation, and the
failure is a desync at tick 1 that reads as a fault in the engine (which is what
happened on 2026-10-13). Every announcement therefore carries `build=<commit>`
(`src/rwe/BuildIdentity.*`; `LanDiscovery::advertise` stamps it, so a caller cannot
forget), and the lobby refuses a mismatch from both ends:

- a **guest** is told on the list ("Version 927583c7, not 0e25e75b: cannot join")
  and when it presses Join; it sends no claim;
- a **host** does not seat a claim from another build, and says so to the room once.
  This is the half that protects against a build that does not check, which is
  every build from before this existed: no `build` key is a different build.

The identity is the commit, plus `-d` if the source differed from it, written by
`cmake/GenerateBuildId.cmake` into a file **regenerated by every build**. It is not
`config.h`'s `GitHash`, which is taken at configure time and is wrong the moment a
commit is made without re-running CMake -- two different zips would have carried one
name. Debug and Release, Linux and Windows, at one commit are the same build; a
build made without git says `unknown` and is not refused. It is a courtesy against
honest mistakes, not authentication: the string is whatever the sender writes.
It does not look at game data; two peers on the same build with different mods
still differ, which is the launcher's archive check.

## Peer address learning

The lobby's address for a peer is the outside port STUN reported, and it can
still be wrong: the router may have remapped it since, or may filter on the
address and not give the same port. So both transports follow a peer:

- `NetworkMessage`'s `LoadingStatusMessage` gained `optional uint32 sender_slot`;
  the in-game `GameUpdateMessage` already carried `player_id`.
- A whole, well-formed datagram (CRC good, it parses) from an address nobody was
  told about, naming a seat that is known at **the same IP address**, moves that
  seat's endpoint to where the datagram came from. `LoadingNetworkService::onReceive`
  and `GameNetworkService::receive`; each logs `Player N is sending from X, not Y; following it`.
- The IP has to match; only the port may differ. This is not authentication --
  the CRC is not a MAC -- but spoofing the address was already all it took to
  inject packets, and another port of the same address gives an attacker nothing it
  lacked. An older build never sends the field and is never relearned.

## One machine, two outside addresses

A carrier's NAT can give one machine's sockets different outside *addresses*,
not only different ports. Measured 2026-10-14 on the Smarty/Three 4G connection:
the discovery socket left as `92.40.168.165:17992` and the game port, a moment
later, as `92.40.168.167:57274`. The lobby used to believe a `gaddr` only when
its host part equalled the sender's, so it threw the game port's address away
and fell back to "source address and 1337": both machines reached
"starting game", sent their "ready" every 100 ms to an address nothing was
listening at, and waited for ever (the other end's log showed
`Sending notification to ::ffff:92.40.168.165 1337` and no datagram received).

The rule is now `sameCarrierBlock` (`network_util.h`): equal, or both IPv4 in
the same /16. It guards `announcedGameAddress` and both transports' peer
following. It is still not authentication -- it stops a lobby aiming a peer's
packets at an arbitrary address, and nothing more.

## What it cannot do

- **A router that gives each destination its own port** (a "symmetric" NAT) cannot
  be punched: the address STUN saw is useless to anybody else. The two-server
  comparison in `ReflexiveProbe` finds this out and the lobby says so
  (`This router gives each destination its own port, so direct play cannot work.`)
  instead of handing out a code that cannot work. The only cure is a relay, which
  RWE does not have.
- **Two machines behind the same router** should use the LAN path. The outside
  address needs the router to send a machine's packets back to itself ("hairpin"),
  which most do not.
- **No STUN server reachable** (UDP blocked) is reported as such and the LAN path is
  untouched.
- An invite code is a *session's* address: it is single-use in practice, since both
  the address and the port change from session to session.
- Three or more people need every pair of guests to punch to each other, using
  the addresses the host lists. Two people is the case that has been looked at hardest.
- The STUN servers are other people's (`stun::defaultServers()`: Google, Cloudflare,
  sipgate). The list is tried in order and costs a second when one is down.

## Verified, and not

Verified by tests here: the STUN codec against hand-made responses (including the
example address of RFC 5769), with every prefix and every flipped bit of a response
parsed without a read out of bounds; the invite code round trip, look-alike characters and that every
single-character slip is refused; `NatTraversal` against a fake STUN server on
loopback (both sockets found from their own ports, the game port released on
stop, a taken port named, a symmetric router named, discovery traffic undisturbed,
two peers punching each other); both transports following a peer that arrives from
another port and refusing one that arrives from another address or another seat;
and `ui_probe --multiplayer-flow`, whose last section is a host and a guest with
ports and a STUN server of their own, no broadcast between them, joined by codes.

**Not** verified on a real network: loopback has no router in it, so whether a
particular carrier's NAT reuses the game port's mapping after this socket is
closed and the game's is opened is a property of that NAT. The MR500/Smarty
connection was measured endpoint-independent -- the same outside port from four
different STUN servers -- which is the property the whole design rests on;
Starlink's has not been measured.

The two-connection test has now been done once (2026-10-13/14, a 4G carrier
NAT against a home line running the Linux build). The lobby, the codes and the
punch worked. Starting the game did not, for the reason in "One machine, two
outside addresses" above; the fix is `sameCarrierBlock`. What is *still* open is
the step after it: whether the game port's mapping survives `NatTraversal`
closing its socket and `LoadingNetworkService` binding the same port, which is
a property of the carrier's NAT and has not been observed. If a game gets as
far as "ready" and stops there again, the two `rwe.log` files say where each
side is aiming (`Sending notification to ...`) and whether anything arrived.
