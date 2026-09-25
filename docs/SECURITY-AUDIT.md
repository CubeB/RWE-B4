# Security audit, 2026-09-25

Issue #75 asked for a security audit of the code. This is the record: what
was looked at, what was found, what was fixed and where, and what was left
as it is and why.

It follows the method of Cloudflare's `security-audit-skill`, applied by hand
rather than installed. The steps were:

1. Map the trust boundaries.
2. Hunt each boundary separately, with a fresh reviewer per boundary.
3. Try to disprove every candidate.
4. Keep only what survives with a trace from the untrusted input to the fault.

The four hunters' reports were checked line by line against the code before
anything was fixed. The archive hunter's final report called the Smacker
decoder safe, and its own first draft had it right, so that bug is included
here (finding 3). Checking also found a bug that a report had passed as
sound: the HPI root directory read (finding 1). Two of the fixes were
reproduced under AddressSanitizer against the old code.

## Trust boundaries

| Boundary | Who controls the bytes | Where they arrive |
|---|---|---|
| Peers in a network game | anyone in the game, or anyone who can send UDP to its port | `GameNetworkService::receive`, `PlayerCommandService`, `applyUnitCommandToSimulation` |
| The lobby | anyone who can reach the master server | `master-server.ts`, `game-server.ts` |
| Lobby strings reaching the engine | the room's owner and every other player | `launcher/src/launcher/rwe.ts`, `OpaqueArgs` |
| Downloaded game data | whoever made the map or mod | `src/rwe/io/*`, `src/rwe/vfs/*`, `mesh_util.cpp`, the COB VM |
| Saved games and replays | whoever made the file; a rejoin bundle comes from another peer | `save_util.cpp`, `ReplayFile.cpp`, `VectorMap` |
| CI and supply chain | third-party action owners, upstream release artefacts | `.github/workflows/*.yml` |

## Findings

Severity is by impact:

- **Critical:** memory corruption that someone else's file or packet can cause.
- **High:** one message crashes or freezes a whole service or every peer.
- **Medium:** a crash, a hang or cheating that needs more setup.
- **Low:** hardening.

### Fixed

| # | Severity | Finding | Fix |
|---|---|---|---|
| 1 | Critical | An HPI archive whose root directory starts past its declared end writes its own bytes past the directory buffer. The length `directorySize - start` wraps, and it is read before the bounds check. ASan on the old code: 120-byte heap-buffer-overflow write. An archive only has to sit in the data folder. | #303 |
| 2 | Critical | A PCX run is never clamped to the row, so the last row can write up to 63 bytes past the image. ASan on the old code: 63-byte heap-buffer-overflow write. | #303 |
| 3 | Critical | A Smacker film's `width * height` is multiplied in 32 bits. A wrapping product gives a buffer of a few bytes that the 4x4 block writes, indexed by the full width, then overrun. `movies/1.zrb` plays at startup. | #303 |
| 4 | Critical | A COB thread that signals its own mask is killed but keeps executing. Its next `sleep` pops a different thread off the ready queue and files the dying one as sleeping, to run after it is freed. An honest script that sets its mask before signalling does this too. | #302 |
| 5 | Critical | A saved game's id-table layout is restored without checks, and `emplace` writes through its free chain. | #304 |
| 6 | High | The master server, which hosts every lobby in one process, dies on one message: a `create-game` with a billion seats, a `null` handshake, or `close-slot` on slot 99. | #300 |
| 7 | High | A network game froze if a packet held a command this build could not read. The throw ended the network thread and the main thread waited on it for ever. | #301 |
| 8 | High | An **honest** order to about fifty units froze a network game. A move order is 30 bytes a unit, so 49 units exceed the 1500-byte datagram. `send()` threw, with the same result as #7. A peer that never acked could force the same thing. | #301 |
| 9 | High | A command naming a unit type the game does not define reached `unitDefinitions.at()` on every peer, and the game ended for all of them. | #301 |
| 10 | High | A drop or rejoin tick near 2^32 padded four billion command sets on every peer. | #301 |
| 11 | High | A room's map name, a player's name and the relayed rejoin tick were passed to every player's engine as the element after `--map`, `--player` or `--rejoin-tick`. An element starting with `--` is a new option, so a map named `--log=<path>` opened that path for writing on every joining machine. | #300 |
| 12 | Medium | Any peer could command any unit, for example self-destruct every enemy unit. The issuing player was known but never compared with the owner. | #301 |
| 13 | Medium | A peer could name itself in a rejoin and get 180 seconds of immunity from being dropped. | #301 |
| 14 | Medium | A peer could buffer an unlimited number of command sets and hashes ahead of the game. | #301 |
| 15 | Medium | A resurrection order carried its progress from the wire, so a peer could finish one in a tick. | #301 |
| 16 | Medium | A COB `div` by zero raised SIGFPE. Unbounded argument counts, recursion, stack growth and non-yielding loops hung or ran out of memory, and so did a loop that asks for a value every pass, whose instruction count starts again at each request. Any other VM fault ended the game on every peer, including one raised while the engine carried out a thread's request: `get PIECE_XZ`/`PIECE_Y` for a piece the model does not have threw out of the tick. | #302 |
| 17 | Medium | 3DO models: object trees that point back at themselves loop or recurse for ever. Face vertex indices and flat colours index out of range. The selection plate is read as four vertices regardless. An empty piece dereferences `end()`. | #303 |
| 18 | Medium | A piece hierarchy that loops by name hangs `getPieceTransform`, in the simulation, on every peer. A saved piece vector shorter than the model is read past its end. | #303 |
| 19 | Medium | An HPI directory that contains itself recursed until the stack ran out. An entry name past the directory was scanned from past the end. | #303 |
| 20 | Medium | TNT maps: header sizes wrap in 32 bits, and the minimap scan read past its buffer. A tile index past the map's tiles was read by the renderer. | #303 |
| 21 | Medium | TDF blocks nest without limit, so a line of `[a]{` repeated overflows the stack. No handler can catch that. | #303 |
| 22 | Medium | A replay header with wrong-typed fields threw out of `readReplayFile`. The rejoin bundle is a replay from another peer. | #301 |
| 23 | Low | Header counts sized allocations before anything behind them was read: HPI file and chunk sizes, GAF entries and frame sizes, COB code, function, piece and static counts, TNT tile and feature counts. | #303 |
| 24 | Low | PCX headers, palettes and short rows were read without length checks. The texture loaders gave GL `width * height` texels from a smaller buffer. | #303 |
| 25 | Low | A palette shorter than 1024 bytes was read past its end. | #303 |
| 26 | Low | `VectorMap::tryGet` read an id's slot with no bounds check. Projectile target ids from a save reach it. A saved path search's start rectangle indexed the A* scratch without a check that it was on the map. | #304 |
| 27 | Low | NaN and infinite positions reached float-to-int conversions. Hash and chat indices past `INT_MAX` became negative. | #301 |
| 28 | Low | Lobby hardening: unbounded unjoined rooms; unvalidated side, colour, team, address, mod lists and archive fingerprints; a drop report naming any player; `getAddr` throwing without the forwarded-for header. | #300 |
| 29 | Low | CI: third-party actions pinned by movable tags, linuxdeploy fetched from a build replaced in place with no checksum, and no default token permissions. | #305 |

Each fix has a test that builds the malicious input. Most are in
`src/rwe/io/malformed_input.test.cpp`, `game-server.test.ts` and the
`[network]` and `[cob]` cases. Behaviour for honest input is unchanged, and
was checked by:

- same-seed arena games that are byte-identical to before;
- every shipped archive, map, model, script and picture still reading;
- two real peers on loopback staying in step.

### Left as they are

These are design properties of a lockstep peer-to-peer game, or tools that are
not shipped. They are recorded so that nobody mistakes them for oversights.

- **Peers are not authenticated.** A peer is known by its UDP source address,
  and the packet checksum is an unkeyed CRC. Anyone who can spoof a peer's
  address can speak as that player. Fixing this needs a shared secret from
  the lobby, which is a protocol change.
- **The game trusts peers to be honest about the game.** This is inherent in
  lockstep, and it is true of the original.
  - A peer can report a low scene time and so stall the others without
    tripping the drop timer. The drift gate waits for the slowest peer by
    design.
  - A false sync hash ends the game, which is by design.
  - A unit's build menu is not checked, so a peer can order a builder to
    build any defined type. The simulation has no build menus to check against.
- **The Electron launcher runs with `nodeIntegration: true` and
  `contextIsolation: false`.** No injection sink was found: every lobby
  string renders as a React text node, and nothing uses `innerHTML`, `eval`,
  `openExternal` or `webview`. But any future sink would be remote code
  execution. Isolating the renderer means moving its Node use behind a
  preload script, which is a refactor of its own.
- **Self-reported archive hashes.** The lobby's "same data" check (#43)
  compares what each client says it has. It prevents accidents, not lies.
- **A malformed installed archive stops the game at startup** with an error,
  rather than being skipped. That is safe, and it was already the behaviour.
- **The renderer indexes a unit's pieces by its model's numbering.** A
  crafted save with too few pieces reaches it. Every unit the game itself
  makes has its model's pieces. The simulation's own lookup is guarded (#303).
- **A rejoin tick far past the recording** makes the returning player wind
  forward for a long time. Only the player who asked is affected, and the
  recording must reach that tick.
- **The demo tools** (`tad_probe`, `tad_episodes`) are developer tools that
  are not installed.
  - A non-UTF-8 player name aborts `--emit-resources`.
  - A crafted demo can cost quadratic CPU.
  - A demo's file name is written unescaped into generated headers.
  - `tad_probe` prints player names raw, terminal escapes included.

  Worth fixing if the tools ever process demos from strangers in bulk.
- **Saved games** were reviewed by hand after the hunter assigned to them was
  cut short. The review covered the id tables, the explored grid, the
  suspended path search, and the unit-type and player lookups. The per-field
  restore of every order and behaviour state was sampled, not read in full.

### Checked and found sound

- The HPI LZ77 and zlib decompressors, and chunk checksums.
- GAF row decoding.
- The Smacker bit reader and tree builders, which stay bounded by the file.
- `SpanStream`, which refuses seeks outside the buffer.
- Replay record sizes, capped at 1 MiB.
- The lobby's use of server-assigned player ids for everything except the
  drop report fixed above.
- `spawn` with an argument array, so the launcher never builds a shell
  command.
- The use of `pull_request` rather than `pull_request_target` in the
  workflows.
- `fetch-msvc-libs.py`'s SHA-256 check.
- Chat sanitising in both the engine and the lobby.

## Keeping it that way

- **Treat anything read from a file or a socket as untrusted.** That covers
  every count, size, offset and index. Check it before it sizes an allocation
  or indexes memory. An `assert` does not count as a check: release builds
  compile it out.
- **Bound every walk over a structure the input describes.** That means
  trees, chains and name lookups, with a visited set, a step budget or a depth
  limit.
- **A fault in content should cost that content, not the process, and never
  every peer.** Kill the script thread, refuse the file, or drop the packet.
- **Pass values to the engine as `--key=value`.** Never pass a value as a
  separate argument after its key.
- **Add a case to `src/rwe/io/malformed_input.test.cpp` when you write a
  parser.** Include the file that would have broken it.
