# RWE — Robot War Engine

An open-source real-time strategy game engine with high compatibility for
Total Annihilation data files. Most of its work is fidelity: reproducing the
original's behaviour exactly, and knowing precisely where it chooses not to.

## Language

### Fidelity

**Conformance**:
Behaviour decoded from `TotalA.exe` that RWE reproduces. The decode is the
standard: a mismatch against it is a bug, however plausible the alternative.
_Avoid_: TA-accurate, compat, "matching TA" used loosely

**RWE-original**:
Behaviour with no counterpart in the original — the AI, the UI, RWE's own
design choices. Not a gap and not a defect.
_Avoid_: custom, ours

**Divergence**:
A knowing difference from the original. Exists only once recorded in
`TOTALA-EXE.md` §88; an unrecorded difference is a bug, not a divergence.
_Avoid_: deviation, difference (unqualified)

**The original**:
`TotalA.exe` — the GOG v3.1 binary the findings were decoded from. The
reference implementation for conformance.
_Avoid_: TA (reserved for the game and its data files)

**TA**:
The game Total Annihilation and its shipped data files, as distinct from the
executable that runs them.
_Avoid_: using "TA" for `TotalA.exe` itself

**Finding**:
A result of reading the original, recorded in a register with its evidence —
a routine offset, a string's absence, a fixture. Findings run both ways:
"the original does X" and "the original does not, with proof". Numbered §n in
`TOTALA-EXE.md`.
_Avoid_: note, observation

**Refutation**:
A finding that overturns a prior reading — community folk-law, a plausible
guess, or RWE's own earlier decode. Kept in the register rather than deleted,
so the same guess is not made twice.
_Avoid_: wrong, removed

**Register**:
A working document that records findings and their state: decoded, ported,
refuted. `TOTALA-EXE.md`, `TA-DEMOS.md`, `TOTALA-EXE-SHADING.md`,
`TOTALA-EXE-WRECKS.md`, `TOTALA-EXE-MISSIONS.md`.
_Avoid_: doc, notes

**Unported**:
Decoded but not yet built into RWE. Lives in §91 until ported.
_Avoid_: TODO, missing

### Determinism

**Sim**:
The deterministic game simulation — lockstep, hashed, shared by every peer.
Everything that can change a game's outcome lives here.
_Avoid_: simulation logic (just "sim"), game logic

**Presentation**:
Everything outside the sim: renderer, sound, UI, particles, camera. May read
the sim but never write it, and never draws from the sim's RNG.
_Avoid_: client side, visual layer

**Lockstep**:
The simulation contract: peers exchange commands, not state, and every peer
ticks identically. The reason presentation may not touch sim state.
_Avoid_: sync mode

**Sync hash** (`GameHash`):
The per-tick digest of hashed sim state. A mismatch between peers is a
desync.
_Avoid_: checksum, save hash

**Drop**:
Cutting a peer's command stream at an agreed tick and carrying on without
them. A tick, not a moment: everything past the cut is discarded and
everything missing below it is filled in, which is what lets two peers
holding different amounts of a lost peer's stream agree on the same game.
Exactly one peer may issue one.
_Avoid_: disconnect, timeout, kick

**Rejoin**:
The inverse: reopening a dropped stream at an agreed tick, from which the
game needs that player's commands again. Every peer stalls there until the
returning one arrives, which is the design and not an accident.
_Avoid_: reconnect

**Rejoin bundle**:
The recording a returning peer is handed: an ordinary replay file cut to end
at the tick before the rejoin, which is everything it missed. Carried by the
lobby, because the lobby is what holds a reliable connection.
_Avoid_: save, snapshot, catch-up file

**Simulated locally**:
A player whose units this machine decides: their orders, their weapons, the
damage they take, their economy. The default for every player. The opposite
of a player simulated elsewhere.
_Avoid_: owned player, local player used loosely

**Simulated elsewhere** (a Remote player):
A player whose units another machine runs. This machine applies the owner's
recorded results — positions, damage, death — and takes no decision for
them: no orders, no weapon fire, no damage from local hits, no economy
settle, no AI. The receiving half of TA's owner-authoritative model, which
is why the flag is per player and not per unit. Not hashed, so it cannot
move a sync hash. A game with one cannot be saved, because the state it is
missing lives on the other machine.
_Avoid_: puppet player, non-local player, demo mode

**Bridge**:
The launcher's channel to the engine, one JSON object a line over standard
input and output. `rwe_bridge` answers questions about the data files before
a game; `rwe --bridge` answers for a game in progress. Never a simulation
input: what arrives becomes an ordinary command in the local peer's own
stream, like a keypress.
_Avoid_: IPC, RPC

**Desync**:
Two peers' simulations diverging, first visible as a sync hash mismatch.
The saved sim state can differ *before* the hash does, because some sim
state is saved and not hashed.
_Avoid_: out of sync, drift

**Desync report** (`DesyncReport`):
What a peer knows when it notices a desync: the first tick the peers
disagreed on, and every peer's sync hash for it. Distinct from the tick it
was noticed on, which is a round trip later and differs per peer.
_Avoid_: desync dump (that is the file the report writes)

**Hash source**:
A peer that runs its own simulation and so reports a sync hash: this
machine and the machines on the other end of the network. A computer
player is a player and not a hash source.
_Avoid_: peer (a hash source is a peer; not every player is)

**Archive**:
A `.hpi`, `.ufo`, `.ccx`, `.gpf` or `.gp3` file of game data, loaded by the
VFS. Lives inside a mod directory; the engine adds them in a fixed extension
order and the first copy of an entry wins.
_Avoid_: pack, HPI (one extension of five)

**Mod fingerprint**:
One mod reduced to what two players have to agree about: the archives in it and
the SHA-256 of each. What the lobby compares, because the mod name says nothing
about what is in it.
_Avoid_: mod hash (it is a list, not a hash)

**Message bar**:
The one-line field the game opens on Enter, for typing a line of chat. Holds
the keyboard while it is open, every key on it being a letter.
_Avoid_: chat box, console (the console is where the line lands, not where it
is typed)

**Chat line**:
What a player says to the other players. Carried beside the command stream on
the same packet, never in it, because a stalled game is exactly when one is
most wanted -- and so it is not simulation state: never hashed, never saved,
never recorded in a replay, and never seen by any peer's `GameSimulation`.
_Avoid_: chat message (that is the wire type, `proto::ChatMessage`)

**Hashed state**:
Sim state the sync hash reads. Must be initialised by the time the object
exists, and kept in step across the hash, the save and the dump.
_Avoid_: synced state

**Saved state**:
Sim state the save file serialises. Superset of hashed state: weapon aim
state is saved and not hashed.
_Avoid_: (nothing — distinct from hashed state on purpose)

**Derived state**:
State recomputed from sim state rather than stored. Not hashed and not saved
by default — but the exemption is decided by one question, "can it move the
simulation's future?", not by whether it is derived. `UnitSpatialIndex` is
exempt (it answers with a deliberate superset, so it decides nothing); the
suspended path search is not (it is serialized, because when the path lands
changes where a unit is).
_Avoid_: cached state, computed state

### Modding

**Mechanism**:
The machinery that stays native C++ and that no mod replaces: the tick loop,
movement, pathfinding, collision, projectile physics, the COB VM, the economy
settle, save, hash, networking and rendering (ADR-0002).
_Avoid_: core, engine (both also mean the whole of RWE)

**Policy**:
A decision a hook covers, such as whether an aircraft seeks a pad or how much
damage a hit does. Runs as wasm, with TA's answer in the base mod.
_Avoid_: behaviour (too broad), rules (reserved for `MissionRule`)

**Wasm mod**:
A WebAssembly module shipped inside a mod's archives, with its own instance
and memory, that supplies policy through hooks. A mod may contain one or none;
its archives are what the mod fingerprint covers.
_Avoid_: plugin, script (COB scripts are something else)

**Mod store**:
Engine-owned key-value state that wasm mods share: each mod writes only its
own namespace and every mod reads all of them, in global, per-player or
per-unit scope. Hashed state; a desync report gives one sub-hash per
namespace. A **var** is its per-unit `i32` fast path.
_Avoid_: blackboard, shared memory (mods never share linear memory)

**Base mod** (`ta-base`):
The wasm mod holding TA's own policy: the bottom layer of every fold. Ships as
wasm, and the tests run it as wasm; the same source can be built natively as a
local debugging option.
_Avoid_: vanilla mod, default mod

**Hook**:
A named entry point a wasm mod exports, with one fixed composition rule:
**fold** (each layer gets the one below's result as `prev`), **event** (all
called, none returns), or **keyed exclusive** (one owner per key).
_Avoid_: callback, override (a fold layer adjusts, it does not replace)

### The demo corpus

**Demo**:
A recorded game, `.tad`/`.ted` — a stream of *state and effects*, not of
orders: the original is owner-authoritative, so a demo cannot be fed to the
sim as input. Conformance data, not playback material.
_Avoid_: recording, replay

**Demo recorder**:
The output side of a demo: an observer attached to a game RWE is simulating
that writes every player's DirectPlay traffic as that player's TA peer would
have sent it. A wire tap and not a seat, and a pure observer — never hashed,
saved, dumped or read back.
_Avoid_: replay writer (`ReplayWriter` records a seed and a command stream,
and only RWE can play it), capture

**Puppet**:
An RWE unit standing in for one a demo's stream names, driven entirely by
that stream rather than by any local decision: spawned where the `0x09` says,
steered along the replicated path or toward the recorded goal, and corrected
to the recorded position, health and build progress at each full-state
record. Not a player and not a unit of the simulation's own — a puppet exists
only while a demo is being played, and a demo player is simulated elsewhere.
_Avoid_: bot, proxy unit

**Puppet driver** (`TadPuppetDriver`):
The owner of a `GameSimulation&` whose demo players are all simulated
elsewhere, which consumes a demo's packets in order and keeps the puppet
table that maps TA unit ids onto RWE units. Free of SDL, GL and `GameScene`,
so the headless `tad_puppet` and a future spectator scene use the same one.
Its clock is the `0x2c` serial, never `Packet::time`.
_Avoid_: replay player, demo player (that is the simulation's player, not the
driver)

**Live receiver** (`TaLiveReceiver`):
A jitter buffer in front of the puppet driver for a game rather than a file. It
holds each sender's packets until the local tick is within its depth of the
tick the `0x2c` serial names, so the driver has them in hand at that tick and
applies them there rather than at the tick they arrived; a packet whose tick
has passed is applied at once and counted, a serial already held or already
handed over is dropped, and the map from serial to RWE tick settles on the
lowest serial seen before the first packet is applied. Reports how far the two
clocks drift apart over a run. Its counters are the overlay's.
_Avoid_: network layer (that is the datagram, #424), delay, lag compensation

**Receive buffer**:
The depth of a live receiver's jitter buffer, in ticks, and the distance the
local clock runs behind the sender's. A packet is available up to that many
ticks *before* the tick it names, which is what buys the reordering tolerance;
past it a packet is late and can only be applied where it landed.
_Avoid_: jitter buffer (that is the mechanism, this is the setting), buffer
size (that is the bound on packets held)

**Demo output**:
A `.tad` RWE wrote rather than recorded from TA. Readable and mineable by the
same tools as a real one, with three recorded divergences: the `0x1a` ids are
synthetic, the status-message body is zero beyond the DirectPlay id, and the
`0x10` echo is absent until the closeout work.
_Avoid_: recording, capture

**Corpus**:
The set of real demos mined together — thirteen games today. Evidence is
counted over it, so "the corpus found" is a claim about all of them, not
one.
_Avoid_: demo directory, sample set

**Episode**:
A short bounded slice of a demo with real numbers in it: input explicit in
the stream or irrelevant, window short enough that divergence has not
accumulated. The unit the corpus is mined into; becomes a test case.
_Avoid_: sample, snippet, window

**Fixture**:
Checked-in test input generated from the corpus — a header of episodes with
the data set's own FBI values transcribed inline, provenance, and
expected-difference deltas. Regenerated by an engine script, read back, and
never hand-edited.
_Avoid_: test helpers (those are helpers, not fixtures), test data, mock

**Cell**:
A pool of repeated observations for one combination of the corpus axes —
builders down the side, products along the top, shooters and weapon slots
likewise, each combination one element like a spreadsheet cell. The pool is
what makes a mode meaningful: one build is noisy, 249 builds of the same
pair are not.
_Avoid_: group, class (reserved for round behaviour: motor, shell, burst)

**Oracle**:
A model — a port of the original's arithmetic — that lives outside the
engine, in the reference scripts and the emitters, and predicts what the
corpus should show. Its predictions are baked into fixtures; the engine,
which shares no code with it, is then held to them. A shared bug can never
fail a test, which is why the separation is the point.
_Avoid_: the engine's own code (that is what is tested against the oracle);
model (reserved for round-behaviour classes — motor, shell, burst)

### TA's network

**Owner**:
In a game with the original, the peer whose machine simulates a unit. Its
word on that unit's position, health and death is final everywhere else:
other peers steer a copy along the path it sends, show damage they cause, and
wait for its death record. The opposite of lockstep, where no peer owns
anything. `docs/TA-NETWORK.md`.
_Avoid_: authority, master, server

**Full-state record**:
The tail of every `0x2c`: one owner-block slot's complete state, the slot
chosen by tick, so each unit is described once a cycle. The receiver believes
it over anything it had, so a record for an empty slot deletes the unit there.
_Avoid_: snapshot, keyframe

### The instruments

**Harness**:
An executable linking `librwe` that runs the real game rather than a reduced
imitation. The family: `rwe`, `rwe_bridge`, `rwe_test`, and the diagnostic
harnesses. Those that go through `GameLaunch::run` — `battle_test`,
`replay_viewer` — share the renderer, simulation and scene loop with
`rwe.exe`, which is the only way their numbers mean anything.
_Avoid_: tool, sandbox, executive

**Probe**:
An offline diagnostic — no window, no GL, sometimes no VFS — that asks one
question of real data and prints the answer: `ui_probe`, `solar_probe`,
`tad_probe`, and the `tools/exe/` scripts behind the registers' findings.
_Avoid_: debugger; the original's own footprint probing, which shares the word

**Check**:
An instrument whose contract is its exit code: non-zero when a scored
observation moved — including when there is nothing to score, so read the
message, not just the status. A **listing**, by contrast, prints and always
exits zero. `tad_probe --dir`, `tools/tad-buildtime.py`, the cell emitters.
A third sibling, the **extractor**, is deliberately neither: an extraction
failure is not a conformance failure, which is why `tad_episodes` is not a
`tad_probe` mode.
_Avoid_: validator, linter, "test" (reserved for `rwe_test`)

**Counted rejection**:
A filter reports what it threw away, in classes, rather than dropping
silently — because the rejections are the shape worth reading, and a filter
that silently drops cannot defend itself. The episode filters count every
rejection; `--miss-buckets` is the form it takes at scale.
_Avoid_: skip, discarded record

**Arena**:
A batch of skirmish games between two AI sides — one tuned, one control —
run headless and measured by CSVs, events and logs, read through
`tools/arena-analyse.py` and its siblings.
_Avoid_: benchmark, tournament
