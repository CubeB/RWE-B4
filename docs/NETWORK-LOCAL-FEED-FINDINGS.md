# The local command stream is fed by the tick, not the frame

## NETWORK-LOCAL-FEED-001: a slow tick must not make a peer wait for itself

Date: 2026-10-31. Status: RWE-original; no byte-tactics comparison applies.
Scope: RWE's lockstep client command feed and the waiting/stall figures that
read it. Not simulation rules, AI decisions, pathfinding, or TA's own network
protocol.

### Why there is no original counterpart

TA multiplayer is **owner-authoritative, not lockstep**: each machine simulates
its own player's units and broadcasts their state (`docs/TA-NETWORK.md`, "TA is
owner-authoritative, not lockstep"). The original therefore has no client-side
command-buffer depth, no "waiting for" peer, and no sync-hash stream to compare
against. RWE's lockstep game is one of the three shapes on the scene's tick path
named in [ADR-0003](adr/0003-the-guards-union-the-non-lockstep-modes.md), and
the command buffer, the overlay and the stall timer are RWE-original.

No decompilation was read for this finding, and no original executable was run.
There is nothing in the original for this behaviour to match, so this is a
deliberate RWE design decision rather than a parity claim. The comparison is
between two builds of RWE on loopback, before and after the change.

### The fault measured

Reference revision: `revival` at `e55060592df0605635507c0f0178eb02c9732386`
(the merge of #961). The pre-change executable reported that as its build
identity; the post-change executable reported `e5506059...-d`.

Run on 2026-10-31 with real shipped data (`D:/RWE-Data`), three peers on
loopback, 320x240, one machine:

```
uv run tools/net-test.py --peers 3 --seconds 45 --min-ticks 500 \
  --lag 1:200@300 --data-path D:/RWE-Data --out-dir <dir>
```

`--lag 1:200@300` makes the peer named Bob sleep 200 ms after every tick from
tick 300, which is the documented way to reproduce a machine that cannot keep
up (`RWE_SIM_LAG`, #355). Before the change:

| peer | last tick | stalls | stalled | longest | self-waits |
| --- | ---: | ---: | ---: | ---: | ---: |
| Alice (0) | 440 | 14 | 3047 ms | 567 ms | 0 |
| Bob (1), lagged | 442 | 15 | 3901 ms | 300 ms | 16, from tick 305 |
| Carol (2) | 440 | 14 | 2953 ms | 566 ms | 0 |

Bob's log contained sixteen lines of the form

```
Tick 305 is waiting for Bob
Tick 307 is waiting for Bob
...
```

Bob was named as the player **Bob's own tick was waiting for**. That is not
lockstep: a peer cannot be late for itself. The stall timer counted those
frames, the F9 overlay named Bob, and the scheduler spent the rest of each
frame attempting ticks that could not run.

The cause was the local feed. `GameScene::update` topped the local human's
command buffer up to the round-trip target **once a frame**. A frame that owed
several ticks -- a slow tick, a frame hitch, or a peer that had just started
lagging -- drained that buffer part-way through its dispatch. The next tick
then had no local set, `tryPopCommands` blocked, and the tick loop kept
attempting ticks for the rest of the frame. The local sets for the later ticks
had never been sent, so every peer also saw the stream stop.

### The change

`GameScene::pushLocalCommandSet` is now the one place a local set is queued and
sent, and `tryTickGame` calls it after a tick that has **run**, until the local
stream is back to `localCommandDepthTarget`. The frame top-up still puts the
first sets there before any tick runs. Three properties are deliberate:

- The tick-side call is after `lockstepStats.tickRan` and after the simulation
  step has been entered; a blocked attempt never tops the buffer up, so a stall
  cannot make the local stream grow.
- Sets still enter the stream in order and are still sent before the tick that
  consumes them on a peer. Nothing skips a required lockstep input, and no hash
  input changes.
- The feed is now one set per tick even when several ticks dispatch in one
  frame, so a slow peer sends a steady stream instead of a burst per frame.

After the change, the same run:

| peer | last tick | stalls | stalled | longest | self-waits |
| --- | ---: | ---: | ---: | ---: | ---: |
| Alice (0) | 475 | 75 | 10535 ms | 232 ms | 0 |
| Bob (1), lagged | 473 | 1 | 68 ms | 68 ms | 0 |
| Carol (2) | 475 | 75 | 10535 ms | 232 ms | 0 |

Bob no longer waits for itself at all. The other two still wait, and should:
Bob's machine really can only produce a command every 200 ms, so the game is
paced to it. What changed is the shape of that wait -- one 232 ms wait per Bob
tick instead of multi-frame bursts up to 567 ms -- and the game got further in
the same wall time (473-475 ticks against 440-442). The stall total on Bob's
peers is larger because it now counts the honest per-tick wait that the old
self-stall was hiding behind a shorter, slower game, not because a new wait
appeared. This change does **not** make a slow machine fast, and does not claim
to; the speed governor still caps every peer to the slowest.

Healthy control runs are unchanged: three peers for 45 s, no lag, reached
30.3 ticks/s with one startup stall; three peers plus a computer player also
agreed on every hash. The full suite passed (2055 cases, 2050 passed, 5 skipped
for missing scenario data).

### Regression check

`tools/net-test.py` now scans each peer's own "Tick N is waiting for ..." lines
and **fails any run** in which a peer names itself after tick 90, the point by
which the local stream has had time to fill. Run against the pre-change logs,
this check finds the sixteen self-waits above; against the post-change run it
finds none. `--lag` is the mode that reproduces the fault, and the check also
guards the ordinary, kill and rejoin modes.

### Evidence limits and what is still missing

- Everything here is one machine on loopback with a synthetic constant sleep,
  not a real jittery or lossy link. `RWE_SIM_LAG` changes wall time only and
  never the simulation, so it cannot reproduce packet loss, one-way delay or a
  peer whose frame rate varies.
- The report on #896 did not capture the played build, map, archives/mods,
  participants or a session. A real-session capture would need each peer's
  `rwe.log` (the `Lockstep summary` lines and every "waiting for" line), the F9
  overlay from each peer, the build identity and the map/data fingerprints.
  Without that, this finding establishes one measured local-feed defect and
  makes no claim about the cause of the reporter's particular freeze.
- No original-game check applies, because TA has no lockstep counterpart; none
  is claimed.
