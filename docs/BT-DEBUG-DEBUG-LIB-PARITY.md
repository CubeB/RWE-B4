# Debug allocator, crash reporting, diagnostic windows and profiling parity audit

Spec-room finding, 2026-10-17. Like `BT-SOUND-PARITY.md`, `BT-GAME-CONSOLE-COMMANDS-PARITY.md` and
`TA-SYSTEMS-PARITY.md`, it allocates no section number in the main register. It covers one chapter of
the reference, `debug-debug-lib` (267 annotations from `0x4d80b0` to `0x4e42a0`): the allocation
wrappers (an ordinary CRT mode and a "memfussy" VirtualAlloc mode with guard pages and a live-block
map), page protection, the retained freed-block history, exception logging, stack walking and
symbolization, the memory-status and performance windows, the native processor counters, timers, the
diagnostic registry preferences and a few helpers (open a URL, save a window position). This is the
finding that implementation work cites for any of them, per `AGENTS.md` and ADR-0004.

**This is a developer-tooling chapter.** Nothing in it defines a unit, a weapon, a rule of the game
or a byte of a network packet. Its only effects on play are memory availability, how a fatal fault
ends the process, and timing overhead. Almost every rule below therefore ends **no action**. The
rules that do touch multiplayer are rules 5 (a fatal error ends the process on that machine) and 10
(what the debug tables do and do not give a cheater); both are noted, neither is a gap.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\debug-debug-lib.md`). Nothing else
  under that tree was opened: no source, no decompilation, and none of the source links in the prose
  were followed (the chapter links its own `.cpp` files and the sibling `debug-debug-helper.md`; none
  was opened). Addresses are the ones the chapter names; rules are restated in prose.
- **Evidence class.** A **static reading** by that project: the chapter says in terms that no
  diagnostic window, driver or crash path was invoked. Weaker than `TOTALA-EXE*.md`. Disagreements
  are marked **CONFLICT** with no winner picked; there are none in this chapter.
- **The RWE half** is a read-only audit of `util/CrashHandler.*`, `main.cpp`'s argument handling,
  `game/GameScene_debug.cpp` (the F10 panel), the network overlay (`docs/NETWORK-OVERLAY.md`, F9),
  `sim/sim_prof.h` and `render/render_prof.h` (`docs/PROFILING.md`), `GlobalConfig` (`rwe.cfg`),
  `math/det_math.*` and the determinism notes in `CLAUDE.md`, at the working tree of 2026-10-17.
  Nothing was built, run or changed. Another agent had uncommitted edits in the tree, so every line
  number is "about".

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**, **UNCLEAR**,
**DIVERGENCE (documented)**, **CONFLICT**, plus **no action** for a rule with no player-visible
effect.

## Index

1. Shape and what the register already holds
2. Command-line switches and one-time initialization
3. The allocator: modes, locking, counters, fill, guard pages
4. Freed-block history, block descriptions, generated tree and vector support
5. Fatal errors and the exception log
6. Stack capture, symbolization and the image helper
7. The memory and performance windows
8. Hardware counters, timers and report names
9. Registry preferences, window positions and the URL helper
10. What the debug library gives a multiplayer game
11. Faults of the original that RWE must not reproduce
12. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| switches, init | `CLAUDE.md` (RWE's own `--` arguments and `RWE_*` environment switches) | rule 2 |
| allocator | none | rule 3 |
| freed history, trees | none | rule 4 |
| `FatalError`, `ReportException` | `docs/ROADMAP.md` 2026-09-06 ("A crash writes a report", `CrashHandler`) | rule 5 |
| stack walk, symbols | `CrashHandler` (backtrace) | rule 6 |
| memory and performance windows | `docs/PROFILING.md`, `docs/NETWORK-OVERLAY.md`, `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 7 | rule 7 |
| counters, timers | `docs/PROFILING.md` | rule 8 |
| registry | `BT-GAME-SETTINGS-PARITY.md` (`rwe.cfg`) | rule 9 |
| the dialog template for `Assert` | `TOTALA-EXE-COMMANDS.md` section 124 ("`Assert` has an empty body") | rule 10, re-confirmed |

## 2. Command-line switches and one-time initialization

**Reference.** One finder scans the **whole** Windows command line at every character,
case-insensitively; a match must end at NUL, white space or `=` but need **not** start a token, so a
spelling inside a path counts. A boolean switch takes a default, then an enabling spelling, then a
disabling one, and **disable wins regardless of order**. A number is found, then read with `sscanf`
as decimal or `0x` hexadecimal and keeps its default on failure. The recognised settings:
`-memfussy` (also `-memfrontalign`) on and `-memnofussy` off, **default off** unless the program sets
a default before the first query; `-gonzo` (the "disable" spelling of a switch that defaults on, so
the name does not mean it enables anything); `-memset` / `-memnoset` with a value that defaults to
`0xdeadbeef`; `-fpufussy` / `-fpunofussy` (unmask divide-by-zero and invalid-operation exceptions);
`-debughelper=<n>` (calls a debug DLL entry and then **aborts unconditionally**); `-memorystatus`
and `-performancestatus` (show the two windows). Start-up installs the unhandled-exception filter,
applies the FPU policy and creates the debug thread unless caller flags suppress them; the thread is
started with an 8000-byte stack and the caller busy-spins on a flag with no timeout. The library
version reads 996.

- **Switch grammar: no action.** RWE reads explicit arguments in `main.cpp` and a fixed set of
  `RWE_*` environment variables (`CLAUDE.md`); a spelling in a path cannot enable anything. No
  player depends on the original's switches.
- **A debug thread with a message pump: no action.** RWE draws its diagnostics inside the scene
  loop with ImGui (rule 7).
- **`-fpufussy`: no action.** The original masks the FPU exceptions by default and a developer could
  unmask them. RWE never traps; its guard against a poisoned float is explicit (`isfinite` checks at
  the boundaries, for example `sim/ResourceSharing.cpp` about 168 and 190) and its determinism rests
  on `det_math` and `-ffp-contract=off` (`CLAUDE.md`, "Determinism"). Nothing to port.

## 3. The allocator: modes, locking, counters, fill, guard pages

**Reference.** Every game allocation goes through one set of wrappers under a recursive critical
section. In the ordinary mode they call the CRT and count requested bytes; in memfussy mode each
block takes a whole page-rounded reservation of **twice** its rounded size with only the first half
committed (the rest is an uncommitted guard), the returned pointer is shifted so the requested bytes
end at the committed boundary (back alignment, the default) or begin at it (front alignment), the
slack is filled with `0xcccccccc`, and a red-black tree keyed by returned address records pointer,
size, serial and flags. Free checks the slack pattern (a mismatch raises a breakpoint), decommits and
returns the reservation to an address-ordered free map; a pointer not in the live map is silently
ignored. Out-of-memory calls a replaceable handler and retries **with no retry cap**. `calloc`
multiplies without an overflow check. `realloc` of a grown block does not fill the new tail. In the
ordinary mode `free` subtracts the CRT's rounded capacity after the allocation added the requested
size, so the live counters can underflow. Initial fill of requested bytes is a separate switch from
protection; `ProtectPages` acts only under `-gonzo`.

- **Allocator modes, guard pages, free map: no action.** RWE uses the platform allocator and relies
  on sanitizers and the tests for the same class of fault. The only RWE counterpart that matters is
  the rule `CLAUDE.md` states from experience: **a member the sync hash reads must be initialised by
  the time the object exists** (`GameHash_util.test.cpp`, "a new unit hashes the same wherever in
  memory it was built", which builds a unit over storage filled with two different bytes). That test
  is RWE's `-memset`, and it is stronger, because it runs in CI.
- **Initial fill default `0xdeadbeef`, free fill `0xcccccccc`: no action.** Neither value reaches a
  save, a packet or a replay.
- **Counters and the live count (the memory window's figures): no action.** Presentation of a
  developer tool; RWE reports frame and tick costs instead (rule 8).
- **Unbounded retry on out-of-memory and unchecked `calloc` product: no action.** RWE does not
  reproduce them; a failed allocation is a `std::bad_alloc` and, if uncaught, a crash report
  (rule 5).

## 4. Freed-block history, block descriptions, generated tree and vector support

**Reference.** A vector of up to **8192** records of freed-block descriptions (not the memory) is
kept and overwritten at `totalCount & 8191`; a lookup scans the physical vector backward, so after a
wrap it is not chronological. A block description holds address, size, serial, a name of at most 31
characters and a tag, formatted with an unbounded `sprintf`. The red-black trees (64-byte nodes for
live blocks, 24-byte nodes for free reservations) use pooled 8192-byte `GlobalAlloc` slabs that
bypass the game allocator to avoid tracking their own bookkeeping. The routines named `lower_bound`
in the free map find the first key **strictly greater** than the input, i.e. an upper bound; the live
map's `LowerBound` is the true greater-or-equal. The generated vector insert and string assign are
compiler-library instantiations that share this address interval.

- **Freed history, descriptions, pools, tree routines: no action.** None is observable in play, and
  RWE has no analogue.
- **The mis-named bound functions: no action.** The naming trap is the original's. If a port of the
  free-map coalescing were ever written the chapter's literal comparisons would have to be used;
  RWE has no such code.

## 5. Fatal errors and the exception log

**Reference.** `ReportException` is globally **one-shot**: it sets its written flag before doing any
work, so a failed report is not retried. It captures EBP, ESP and EIP from the context, derives
`ErrorLog.txt` next to the executable (falling back to the drive root), appends a header followed by
the handler, system, exception name (24 statuses recognised), registers, 16 bytes of code at the
fault, a stack dump and the FPU and debug registers. The file handle is tested against zero rather
than the invalid-handle value, so a failed open still flows into file calls; most formatting is
unbounded. `FatalError` appends its message, raises a breakpoint inside a structured-exception
wrapper, shows a **system-modal** message box and calls `AbortProgram`, which invokes a no-op hook,
raises `SIGABRT` and exits with code **3**. The chapter says these routines "diagnose and terminate,
not roll back a partially loaded map or return a recoverable error object".

- **Report on a fatal fault: MATCHES in intent.** `CrashHandler` writes `rwe-crash-<timestamp>.txt`
  next to `rwe.log` with the signal, faulting address, a backtrace and the phase, scene, map, tick
  and unit count (`util/CrashHandler.h` about 1-40). Differences are all improvements: one file per
  crash rather than an append, no unbounded formatting, no allocation or lock in the handler.
- **One-shot: no action.** RWE writes a report per fault and a second fault in the handler is
  guarded by the signal-handler rules in the header.
- **A fatal error ends the process on that machine: DIFFERS, by design, and unrecorded.** In the
  original a failed join (the 40-second wait in `JoinLobbyGame`, `BT-NETWORK-NET-GAME-PARITY.md`
  rule 6) and other network faults go through `FatalError`, which kills the game for that player.
  RWE reports a lost peer as a drop (`GameScene_peers.cpp`) and a refused join as a message box. Not
  in `TOTALA-EXE.md` section 88 or `docs/compatibility.md`; **UNCLEAR** only in the sense that no
  document says the difference is intended. It costs a player nothing.
- **Exit code 3, `SIGABRT`: no action.** Nothing outside the process reads it.

## 6. Stack capture, symbolization and the image helper

**Reference.** A trace record holds up to **14** return addresses and one stack word; a source name
longer than 63 bytes keeps its last 63. The frame walker prefers the IMAGEHLP `StackWalk`, otherwise
follows EBP links with live-stack and monotonic checks; the full-report capture takes thirty
addresses and 2048 stack dwords. IMAGEHLP is loaded lazily with explicit enable and disable switches,
falling back to local FPO and module callbacks when `SymInitialize` fails; the initial failure is
cached. Source paths are searched under `MSDevDir` and the MFC and CRT source directories. The mapped
file and loaded-image helpers trust PE header offsets.

- **Stack capture and symbols: no action.** `CrashHandler` takes its own backtrace; the debug build
  is run under `tools/crash-catch.cmd` when more is wanted.
- **Trusting PE offsets in a mapped file: no action.** RWE's file readers are the opposite case and
  are bounded (`docs/SECURITY-AUDIT.md`, `io/malformed_input.test.cpp`).

## 7. The memory and performance windows

**Reference.** Two modeless dialogs run on the debug thread. The **memory window** (`-memorystatus`,
toggled with **Alt+End**) shows current, peak, requested, live and committed allocation figures, an
allocation-rate average over ten samples and, optionally, the working set classified from a 400000-
byte `QueryWorkingSet` buffer (page-table, shared and private pages by address and a flag bit,
counted times 4096), refreshed on a 200 ms timer. The **performance window**
(`-performancestatus`, **Alt+Home**) lists named timer reports and the two hardware-counter events
chosen for them, with checkboxes for enabling in release builds (default off), priority raising (on),
debugger output (off), window display (on) and auto-pairing (on). Both persist their position.

- **Memory and performance windows: no action.** RWE's diagnostics are the **F10** debug panel
  (`GameScene_debug.cpp`), the **F9** network overlay (`docs/NETWORK-OVERLAY.md`) and the compile-time
  `RWE_ENABLE_SIMPROF` / `RWE_ENABLE_RENDERPROF` per-phase timers that write to the log every two
  seconds (`docs/PROFILING.md`). The keys and the shape differ; the purpose is the same and no
  player depends on either side's.
- **Chapter note that matters to the register: the F-key and Alt-key debug dispatcher is gated by a
  debug-enable bit** (`BT-INGAME-KEYS-PARITY.md` rules 2 and 9); this chapter adds only the two Alt chords
  above, which that audit did not list. Developer keys: no action.

## 8. Hardware counters, timers and report names

**Reference.** The counter support opens the NT `GDPERF` device or the Windows 9x `GDPERF.VXD`,
accepts Intel family 5 or 6 (any other becomes 4), programs two counters through
`DeviceIoControl` codes `0x9c406404` (read, exactly 8 bytes) and `0x9c406400` (write, 12-byte
input) and cannot be un-cached once attempted. `IsPentiumOrBetter` only excludes processor types
386 and 486. The CPUID check tests leaf-one EDX bit `0x800000` (MMX, **not** the time-stamp
counter bit, whatever the cached global is called). The timer class reads
`QueryPerformanceCounter` each call and falls back to **wall-clock** UTC seconds if the counter
fails, so the fallback can jump. A timer started with one argument restarts twice; `AddElapsed`
**subtracts** from a stopped elapsed and **adds** to a running start, so it reduces the reported
time in both cases. Indentation for nested timers is a global array with no bounds check. A report
is produced only when profiling is enabled and counters are available, and a name map keyed by
**borrowed string pointers** copies reports into 500-byte records.

- **GDPERF, Pentium events, CPUID bit: no action.** Hardware of a former era; RWE's timers are the
  platform's steady clock.
- **Wall clock as a fallback timer: no action, and a useful contrast.** `GetTimeSeconds` is distinct
  from the game tick and from the display-scaled `GetTicks` the network layer uses; in RWE the
  simulation never reads a wall clock at all (`CLAUDE.md`, "Nothing a frame knows may reach the
  simulation"), and the profiling timers are pure observers.
- **The `AddElapsed` sign and the double restart: no action.** The names mislead in the original; RWE
  has no timer class to be misled by.

## 9. Registry preferences, window positions and the URL helper

**Reference.** Settings live under `HKCU\Software\Cavedog Entertainment\<section>\<app>`
(`CavedogLibrary\PerformanceSettings` for the profiling window); integer reads require a 4-byte
`REG_DWORD`, clamp valid data to bounds and return the **unclamped** default otherwise; writes ignore
their status; the key handles are never closed. Window positions save the current or normal
rectangle with edges clamped to at least minus 499 (minus 500 is the "no value" sentinel) and restore
within the work area at a minimum of 100 by 50. `OpenUrl` shell-executes a help link and, on failure,
resolves the open command from `HKCR` and retries; the help buttons open authored intranet
addresses.

- **Registry: no action.** RWE persists to `rwe.cfg` and the local data path (`GlobalConfig`).
- **Window geometry persistence: no action** for debug windows. (The main game window's geometry is
  `BT-GAME-SETTINGS-PARITY.md`'s subject.)
- **Help URLs: no action.** `online.dll`'s service links are the front end's, not this chapter's
  (`BT-FRONTEND-MULTI-PARITY.md` rule 3).

## 10. What the debug library gives a multiplayer game

**Reference.** The chapter defines **no console command, no cheat and no packet**. It supplies an
`Assert` dialog template (id `0x81`) but states that "the assert template's existence does not turn
empty console `Assert` into an implemented assertion command". It does not read the lobby's cheat
option, the network connection flags or any player state.

- **`Assert` and the other empty console commands: MATCHES (re-confirmed).** `TOTALA-EXE-COMMANDS.md`
  section 124 and `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rule 7 already record that `Mem`, `Assert`,
  `DPrint` and `ZBuffer` have empty bodies; the chapter adds why the template exists without a
  command behind it. Nothing new.
- **Cheat or console behaviour that would matter to multiplayer: no action.** None is defined here.
  The cheat and debug tables, their masks and whether the lobby's "Cheat Codes" option gates them
  are the subject of `BT-GAME-CONSOLE-COMMANDS-PARITY.md` rules 2 and 7 (`BT-NEW-CONFLICTS.md`
  item 133). This chapter confirms that the memory and performance tools are **not** a route to
  game state: they read allocator figures and timer reports, and "no game simulation state is
  serialized into these preferences".
- **A debug window can slow a game: no action.** The performance window raises the thread priority
  and, optionally, the process priority class while it runs (timer constructor with the priority flag).
  In an owner-authoritative game that slows one peer's own simulation and nobody else's. RWE's
  F10 panel does not alter priorities, and a slow peer in a lockstep game is slowed for everyone by
  design (`RWE_SIM_LAG`, `docs/NETWORK-OVERLAY.md`).

## 11. Faults of the original that RWE must not reproduce

The chapter lists, with care, a large number of the original's defects: unbounded `sprintf` and
`strcat` into fixed buffers (block descriptions, call-site and stack formatting, the exception
report, the system-info report, the URL helper), a file handle compared with zero, a null returned
by `GlobalAlloc` copied into without a check, registry handles never closed, a mapped-file `Close`
that is unsafe if repeated, a node pool that loops for ever on a zero-size request, a modem name list
that overwrites the second slot, and so on. None of these is a behaviour to match. They are cited
here only so that nobody reads "the original does X" in this chapter as a specification: **every
item is a defect, and RWE's rule is the opposite one** (`docs/SECURITY-AUDIT.md`: every count, size
and offset from outside is checked before it sizes or indexes anything).

- **Unbounded formatting and unchecked allocation: no action.** Not reproduced; nothing found in
  RWE that does the same (this audit did not search the whole tree; the earlier security audit did).

## 12. Gaps to fix

Ranked by what a player would notice. There are no gaps in the sense of the earlier audits: the
chapter is a developer tool and every rule is **no action**.

| Rank | Item | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Nothing to fix.** No rule of this chapter reaches play. | no action | none |
| 2 | **The fatal-error difference** (a failed join or a network fault ends the process in the original; RWE drops the peer or shows a box) is not written in `TOTALA-EXE.md` section 88 or `docs/compatibility.md`. | documentation (UNCLEAR whether intended; certainly harmless) | add one line to section 88 with the lockstep divergence lines proposed in `BT-GAME-CONSOLE-COMMANDS-PARITY.md` gap 3 |
| 3 | **Alt+End and Alt+Home are two debug chords `BT-INGAME-KEYS-PARITY.md` rule 9 does not list.** | documentation | none needed; developer keys |

No source file was changed.
