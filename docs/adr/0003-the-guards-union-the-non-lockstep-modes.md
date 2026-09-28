# ADR-0003: The scene's guards union the two non-lockstep modes rather than choose one

`GameScene` runs in three shapes on one tick path — a lockstep game with RWE
peers, a scene watching a recording, and a live game whose peer owns its own
simulation — and each shape stands down a different set of guards in the same
lines. **The two non-lockstep shapes keep their own predicates and every shared
guard tests them as a union: a scene is _watching_ when `isPlayback()` and
_owns its clock_ when `isOwnClock()`, and a lockstep game is neither.** One
predicate meaning "not lockstep" would have been the smaller diff and the worse
one: it answers for a reason the caller cannot see, and at three of the six
guards below the two sides stand down for different reasons.

**Status**: accepted

Written at the merge of #421 (puppet playback) and #432 (own-clock), where both
branches had guarded the same lines and one of the two had to be chosen. #421
reached `revival` first, as `e1ac68d0`; #432's branch took the merge and the
guards below, and followed as `bf9fc1e`. #421 had already
reached most of these conditions through the raw `replayPlayback` member, which
`isTadPlayback()` widened; the shared guards now read `isPlayback()`, so the two
shapes are named rather than spelled out at each site.

## Considered Options

**One predicate for "not lockstep".** Smallest diff, and it loses the two
places where the shapes are not interchangeable. Playback is bounded by the
wall clock as well as by the per-frame tick cap, so a fast-forward drops its
backlog instead of leaving the window drawing a frame every two seconds
(`GameScene.cpp`'s `clockBounded`), and an own-clock game is not bounded that
way; a playback raises `maxTicksPerFrame` with `replaySpeed` and an own-clock
game does not. The union says which, at each site, and a fourth shape can be
given its own arm without editing the other three.

**Three separate tick paths.** The cleanest shape, and the one to reach for if
the modes ever stop being variations on the same tick. It is a large refactor
of the hottest file in the tree — `GameSimulation.cpp` sits within a couple of
hundred sections of the COFF ceiling, and `GameScene` is already eight files
because one of them outgrew what an object can address, both measured in
`CLAUDE.md` — and it buys no behaviour the single path does not already give.

## Consequences

The guards that union, and what each side contributes:

| site | playback | own-clock |
| --- | --- | --- |
| `GameScene.cpp` `averageSceneTime` | no drift gate, so a seek cannot end the recording early | no peer time to be held to |
| `GameScene.cpp` `maxTicksPerFrame` | the cap rises with `replaySpeed` | the ordinary ten |
| `GameScene_peers.cpp` `updatePeerLiveness` | no peers and no commands to wait for | nothing to wait on and nobody to drop: its peer is not an RWE peer |
| `GameScene_peers.cpp` `updateEffectiveSpeed` | no lockstep peer to slow down for | likewise |
| `GameScene_peers.cpp` `localHumanCommandsAreFedPerTick` | no local human is seated | a local human is seated, and gated on no peer |
| `GameScene_replay.cpp` sync-hash push | the exchange stands down, and `RWE_HASH_LOG` keeps writing | no peer to compare against |

`GameScene_replay.cpp`'s command-pop site is the one place the shapes are not
interchangeable, and is a three-way branch: a demo contributes an empty set (it
has no command stream at all), an own-clock game pops without waiting, and a
lockstep game pops and may block. **The first two cannot both hold** — one is a
recording being watched, the other a game against a live peer — so they are
sequential arms rather than a nested test. A fourth shape is the moment to
reconsider that, and to this.

**The two save refusals are two reasons, and stay two tests.**
`GameScene::saveCurrentGame` refuses a game that has a player simulated
elsewhere (that player's state is on the other machine, and
`saveSimulationToJson` refuses it for itself) and separately refuses an
own-clock game (`GameScene::canSave()`: the peer owns its units and its clock,
so a save of RWE's half could not be resumed into the game it came from).
Merged into one test they would still refuse, and the console would say only
that saving is unavailable — the one message that does not say which of the two
is true. The dialog is not symmetric, and does not need to be: `openSaveDialog`
asks `canSave()` alone, so a watched demo's save dialog opens and the refusal
lands on the press, while an own-clock game's never opens at all.
