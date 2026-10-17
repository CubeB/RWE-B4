# Packet channels, bundle sequencing, send pacing and the receive queue parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-WEAPONS-WEAPONS-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one
chapter of the reference, `network-packets` (68 services from `0x460e20` to `0x463ca0`): the
packet manager that groups TA's command subpackets into bundles, paces the sends, numbers the
bundles, and on the other side orders incoming bundles, spots gaps and splits a bundle into
commands with due ticks. This is the finding that implementation work cites for any of them,
per `AGENTS.md` and ADR-0004.

**Scope.** RWE's own lockstep multiplayer is RWE-original and is **not** audited against this
chapter. Only RWE's TA-compatible networking is: `net/ta/` (`TaPacket`, `TaOutboundBatcher`,
`TaHostGame`, `TaBattleroom`), the live receiver (`puppet/TaLiveReceiver`), the shared envelope
and splitter in `io/tad/` and `docs/TA-NETWORK.md`, which is an **executed-against-real-TA
record and outranks a static reading**. The envelope itself (checksum, XOR, compression) is
`BT-NETWORK-CONDENSER-STATS-PARITY.md`; the DirectPlay layer under it is
`BT-NETWORK-HAPINET-PARITY.md`; the unit-catalogue handshake is
`BT-NETWORK-UNIT-SYNC-PARITY.md`.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\network-packets.md`). Nothing
  else under that tree was opened beyond the sibling chapters named above: no source, no
  decompilation, and none of its source links were followed. The spec-room discrepancy
  register (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; BT-178
  (the `0x19` layout) is not re-raised.
- **Evidence class.** A **static reading** by that project: nothing was executed. Weaker than
  `TOTALA-EXE*.md` and than `TA-NETWORK.md`'s captures. Disagreements are marked **CONFLICT**
  with no winner picked.
- **The RWE half** is a read-only audit of `net/ta/TaPacket.*`, `net/ta/TaOutboundBatcher.*`,
  `net/ta/TaBattleroom.*`, `net/ta/TaHostGame.cpp`, `puppet/TaLiveReceiver.*`,
  `io/tad/tad_util.*` and `game/GameScene_tahost.cpp`, with `docs/TA-NETWORK.md` and
  `docs/TA-DEMOS.md`, at the working tree of 2026-10-17. Nothing was built, run or changed.
  Another agent had uncommitted edits in the tree, so line numbers are "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. Channels, block pools and rings (not applicable)
3. Send pacing: why TA sends six ticks to a message
4. The bundle sequence number
5. Bundle size and flush limits
6. Splitting a bundle into commands
7. The receive side: gaps, order and due ticks
8. Failure semantics, and the helper stubs
9. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| pacing constants | `TA-NETWORK.md` "In game": TA sends its `0x2c` six ticks to a message, thirty ticks a second | rule 3 explains the number |
| bundle header dword | `TA-NETWORK.md` "TA packets": the `u32` is a per-sender count, `0xffffffff` on a reply | agrees; rule 4 adds the broadcast detail |
| splitting, command lengths | `TA-DEMOS.md` subpacket table; `tad_util.cpp` | rule 6 |
| receive ordering, gap, due ticks | `TA-NETWORK.md` "How a peer's `0x2c` is received" (`TOTALA-EXE-MOVEMENT.md` section 118: applied the moment it arrives) | rule 7, **CONFLICT** |
| pools, rings, reuse, growth | none | rule 2 |

## 2. Channels, block pools and rings (not applicable)

**Reference.** The packet manager holds eleven destination channels (index zero is "everyone",
one to ten are the other game player ids), each with byte blocks of 1066 payload bytes, a pool
of descriptors, a 1024-entry pending ring and a retention time of four seconds; blocks are
reused only when no queued descriptor remains and no sent descriptor is inside the retention
window, and a full pool grows by sixteen blocks and 800 descriptors up to forced reuse at 34
blocks and 1700 descriptors. Channel lookup reclaims a channel whose id belongs to no player
slot. The chapter lists many unchecked-allocation and stale-pointer faults in this machinery.
Retention exists so a resend could be served; **the gap callback is empty in this revision**
(rule 7), so nothing ever asks for a resend and the retained bytes are never read again.

**RWE: not applicable.** `TaOutboundBatcher` keeps a `std::map` of per-peer, per-transport
vectors of subpackets and clears it on every flush; there are no pools, rings or retention.
Nothing is lost that the original uses, because the original never resends either.

## 3. Send pacing: why TA sends six ticks to a message

**Reference.** The default send interval is **200 milliseconds**. A configured frame rate
selects 1000 divided by the rate in milliseconds, with the rate clamped to **2 to 30 sends a
second**; the channel converts the interval to thirty-hertz ticks, rounding up. A negative
configured value turns the packet manager off. A send is due when its channel deadline has
passed (or when forced), and a due send groups every pending record with the same sender
argument into one bundle.

**Register.** `TA-NETWORK.md` records the *effect* as an observation: "TA sends its `0x2c` six
ticks to a UDP message, thirty ticks a second", and that a received ping request flushes the
queue early (a flush that is not on the interval).

**RWE: MATCHES the observation, and the chapter supplies its cause.** 200 ms at thirty ticks a
second is exactly **six ticks**: the six-tick message is the packet manager's *default* pacing,
not a property of the `0x2c` stream. The early flush on a ping request is the "forced" send.
RWE sends one tick to a message (`GameScene_tahost.cpp` about 335-355, `ticksPerPacket()`,
`RWE_TA_TICKS_PER_PACKET`), which is thirty bundles a second, the clamp's own upper limit; so
RWE is inside the range TA's own code allows. `TA-NETWORK.md` chose one tick for a measured
reason (a real TA applies each received `0x2c` record the moment it arrives and showed RWE's
six-tick walk in jumps). That reason is a **recorded divergence from TA's cadence**, not an
error; it costs about six times the packet count and the per-packet header (seven bytes plus
the transport's).

## 4. The bundle sequence number

**Reference.** The first dword of a bundle is a signed counter. A bundle sent to a **specific
nonzero destination** carries minus one (unsequenced). A bundle sent through the **channel for
destination zero** (everyone) carries that channel's sequence: it starts at minus two, falls by
one per bundle sent on that channel, and a value of minus one or above is renormalised to minus
two. The receiver tracks one "last sequence" per sender and expects the previous value minus
one.

**Register.** `TA-NETWORK.md`: "`0xffffffff` on every reply. On everything a sender starts
itself it falls by one per packet", both peers start one below `0xffffffff`, and "nothing seen
depends on it".

**RWE: MATCHES for two players; DIFFERS in a way that matters at three.**

- **Start and step: MATCHES.** `TaMarkerCounter` starts at `0xFFFFFFFF` and returns the
  pre-decremented value (`TaPacket.h` about 20-28), so the first own packet wears `0xFFFFFFFE`
  (minus two) and each following one is one lower. The reply marker is `0xFFFFFFFF`
  (`TaReplyMarker`). No renormalisation exists; the counter would take 2^31 bundles to reach it.
- **Directed versus broadcast: DIFFERS, latent.** The original numbers a **broadcast** once
  (one bundle, one value, every recipient gets the same). `TaOutboundBatcher::queueForAll`
  takes a **fresh counter value for each peer** (`append(peer, ..., markerCounter.take())`), so
  with N joiners each peer sees the sequence fall by N per broadcast. A receiving TA expects
  minus one per bundle; a gap with nothing saved makes it **keep that bundle back and report no
  messages** until the missing one arrives or a later one forces a choice (rule 7), and the
  missing numbers were sent to somebody else. The game hosts a single joiner today
  (`TaHostSession::MaxPlayers` allows more at the session layer; `GameScene_tahost.cpp` builds
  one remote), so this cannot yet occur. It becomes a stall, not a cosmetic issue, the day a
  second joiner is added. Fix: take one value per broadcast, or number per peer.
- **Replies and per-peer sends: UNCLEAR.** The original calls a send to one nonzero player a
  "directed" send and gives it minus one. RWE gives minus one only to `queueReply`; a
  `queue(peer, ...)` that is addressed to one peer (the unit-table header, a colour grant's
  numbering) takes a count. The captures agree with RWE on the replies; whether TA's own
  directed non-reply sends carry minus one was not separated from replies in the captures.

## 5. Bundle size and flush limits

**Reference.** A block holds at most 1066 payload bytes; adding a record past that moves to a new
block, and the manager force-flushes when queued bytes reach 1066 or the ring holds 1024
entries. A bundle is therefore bounded by one block before it reaches the condenser (whose own
buffers are 2800 bytes, `BT-NETWORK-CONDENSER-STATS-PARITY.md` rule 2).

**RWE: DIFFERS, no cap.** `TaOutboundBatcher::flush` sends everything queued for a peer and a
transport as one packet. The unit-sync echo is batched by count (35 records of 14 bytes, about
490 bytes, `TaBattleroom.h` about 261, "near the size TA sends"), and the in-game stream is one
tick a bundle, so in practice no bundle is large. But nothing stops a tick that carries many
unit records, a long chat line and several `0x0b`/`0x0c` events from exceeding 1066 bytes, and
the receiver's own splitter and buffers (rule 6, the condenser's fixed 28000-byte receive
buffer) were sized for the original's bound. Kind: robustness; a bundle limit with an early
flush is the original's rule.

## 6. Splitting a bundle into commands

**Reference.** After the four sequence bytes the receiver scans command kinds **two to
forty-four**; most lengths come from a fixed table and kind forty-four (the unit-state record)
reads a 16-bit length after its type byte. It **stops** on an invalid kind or a length that runs
past the end of the bundle. A zero entry in the table makes no progress. If there are more than
512 commands the early unit-state ones are skipped first. A kind outside two to forty-four is
not a command the live receiver understands.

**RWE: MATCHES for every code TA sends, with two differences.**

- **Lengths: MATCHES.** The table in `tadExpectedSubPacketSize` (`tad_util.cpp` about 290-410)
  was built from the corpus and the exe and agrees with the chapter's shape, including the
  `0x2c` 16-bit length and the `0x20` correction (186, not 192; `TA-DEMOS.md`). `0x13` is
  left unsized there; `BT-SOUND-PARITY.md` rule 8 supplies 18 bytes.
- **Unknown or overrunning: MATCHES in effect.** An unsized code or one that runs past the end
  becomes a single subpacket of everything that remains (`tadSplitSubPackets`), counted in
  `unknownCodes` or `truncated`; the rest of the bundle is lost either way, as in the original.
  A zero length cannot loop because the walker consumes the remainder.
- **Codes above forty-four: DIFFERS.** The walker is the demo reader's and accepts recorder-only
  codes (`0x42`, `0x2e`, `0xF6`, `0xF9` to `0xFC`, the smartpak ticks). A live TA would stop at
  the first one. For an honest TA peer this never matters; for a hostile or modded peer RWE
  parses codes the original would refuse. Low.
- **512-command skip rule: MISSING, harmless.** RWE has no ring to overflow.

## 7. The receive side: gaps, order and due ticks

**Reference.** Per sender, the receiver:

- takes a command that is **due** when its requested tick is zero, its due tick is not later than
  the current game tick, or it is **more than thirty ticks ahead** (a future entry that distant
  is released rather than waited on); senders are scanned in table order, so a lower-index
  sender wins;
- on a bundle whose sequence skips a value with nothing saved, **saves the whole bundle and
  returns "no messages"**; with a bundle already saved it compares the two, reports the missing
  range to the gap callback (empty, so no resend is requested) and processes the earlier
  first;
- splits an accepted bundle into commands and, with **smoothing** enabled, spreads their due
  ticks from the current tick using the spacing between packets (clamped to 1 to 30 ticks) and
  the command count, instead of giving all of them the current tick; the saved-bundle path
  always uses the unsmoothed form. A new bundle arriving while older commands remain does not
  merge: it accelerates the old ones (every old record is re-stamped with the current tick) and
  is itself not copied.

**RWE.** The live receiver is RWE's own jitter buffer (`TaLiveReceiver`), by design a different
mechanism: it holds a sender's packets, hands them over in that sender's marker order, releases
on the `0x2c` serial's tick, refuses a serial more than **300** ticks ahead
(`maxSerialLeadTicks`), and waits one `jitterTicks + 1` window (three by default) for a missing
packet before giving up on it (`packetsDroppedGap`).

- **Gap handling: DIFFERS, in RWE's favour.** The original holds a bundle back on a gap with
  no resend and no timeout in this chapter's code; RWE waits a bounded time and then moves on,
  so a lost UDP packet costs at most a few ticks. `TA-NETWORK.md` lists loss as a TA hazard it
  does not otherwise treat.
- **The far-future threshold: DIFFERS.** The original releases anything more than thirty ticks
  ahead; RWE refuses past 300. Both are defences against a peer naming a distant tick; only
  the number differs, and RWE's is the safer reading of a peer's clock.
- **CONFLICT: is a record held for its tick?** `TA-NETWORK.md` (decoded from `TotalA.exe`,
  `TOTALA-EXE-MOVEMENT.md` section 118) says the `0x2c` case "is applied the moment it arrives.
  Nothing is queued and no clock is compared: a record is never held for a tick to come round",
  and records a measurement (RWE at six ticks a message showed a real TA's view advancing in
  single-frame jumps). The chapter's frame queue says commands carry **due ticks** and, with
  smoothing on, are spread across ticks. Both can be true if smoothing is off in the shipped
  game or if the unit-state case bypasses the queue, but neither source says so. The chapter
  does not give the smoothing switch's default. The executed measurement outranks; the open
  question is which commands go through the queue. Kind: register versus reference. Settled by
  a spec-room read of where the smoothing flag is set and which kinds the queue's drain
  delivers.
- **Late packets: DIFFERS.** The original's replacement rule (accelerate the old, drop the new
  into the next call) has no counterpart; RWE applies a late packet at once (`packetsLate`).

## 8. Failure semantics, and the helper stubs

**Reference.** Send adapters disagree on what "success" is: one converts the transport's zero to
one, one returns one regardless, one returns the pointer-shaped integer of an add. A failed
append after a queue removal loses the removed entries. `PacketTrace`, the integrity stubs and
the exit handler do nothing, so none of the formatted traces is visible. The chat-index reset
only zeroes the two indices (`BT-NETWORK-NET-CHAT-PARITY.md` rule 2).

**RWE: not applicable.** RWE's send path is `TaHostSession::send` returning whether the peer is
known, and a lost UDP packet is a lost UDP packet, as in the original where no result is
checked. The unchecked-return hazards are the original's own defects and are not reproduced.

---

## 9. Gaps to fix

Ranked by what could hurt a TA game. None touches the simulation's hashed state (an own-clock
TA game has no hash).

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Broadcast takes one counter value per peer**, so with two or more joiners each TA sees gaps and may hold bundles back. Invisible with one joiner. | DIFFERS (latent stall) | code: one value per broadcast before the first multi-joiner game |
| 2 | **Is smoothing on in the shipped game, and does `0x2c` go through the due-tick queue?** The register says applied on arrival; the chapter says queued with due ticks. | CONFLICT (register) | spec-room read of the smoothing flag's default and the queue's drain |
| 3 | **No bundle size limit**; the original bounds a bundle at one 1066-byte block. | DIFFERS (robustness) | code: flush early past about 1000 bytes |
| 4 | **Default 200 ms pacing is the reason for "six ticks"**; RWE's one-tick cadence is a recorded divergence, not documented as one in `compatibility.md`. | documentation | add the reason to `TA-NETWORK.md` "Why a TA flushes a packet early" |
| 5 | **Per-peer directed sends take a count; the original gives a directed send minus one.** | UNCLEAR | capture comparison of non-reply directed sends |
| 6 | **Live walker accepts recorder-only codes** TA would stop at. | DIFFERS, low | optional: a strict live mode that stops above forty-four |
| 7 | **Gap and far-future thresholds differ** (bounded wait versus none; 300 versus 30 ticks). | DIFFERS, by design | none; record in `TA-NETWORK.md` |

No source file was changed.
