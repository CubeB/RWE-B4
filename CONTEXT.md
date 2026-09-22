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

### The demo corpus

**Demo**:
A recorded game, `.tad`/`.ted` — a stream of *state and effects*, not of
orders: the original is owner-authoritative, so a demo cannot be fed to the
sim as input. Conformance data, not playback material.
_Avoid_: recording, replay

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
