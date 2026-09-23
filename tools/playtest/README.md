# Testing notes

Everything the playtest suites exercise is either generated at test time or a
checked-in static fixture; no fixture is produced by a script that must be
re-run to stay correct.

## Format truth

The engine writers are the source of truth, and everything else mirrors them:

- `ai-arena.csv` / `ai-arena-events.csv` headers and the `AI-ARENA-RESULT`
  line come from `src/rwe/game/AiArenaReport.cpp`.
- The event vocabulary for `event-log.jsonl` is documented in `q.py`'s
  docstring; the writer's sites in `src/rwe/game/SimEventLog.{h,cpp}` and the
  AI managers are the definitions.
- Checker thresholds live in `checkers/thresholds.toml`, one comment per
  number saying where it came from.

The appended death-cause columns (`deathCause,killerType,killerPlayer`, tests
§4 Phase 2) exist in newer run roots only. Older fixtures deliberately keep
the 16-column header: they are how we pin the old-roots-still-work contract.
Checkers must treat missing killer columns as unknown and skip — a change to
the engine's events schema should keep at least one 16-column fixture alive.

## Static fixtures (`testdata/`)

Hand-made minimal tables, one per rule's hit and clean case. To refresh one,
capture a real run and trim:

    build/ai_arena --map "Coast To Coast" --ai-arena 900 --seed 1 \
      --player "A;Computer;ARM;0" --player "B;Computer;CORE;1" \
      --out /tmp/x --log /tmp/x/game.log

then copy the relevant rows into the fixture by hand. The point is a small,
readable file where the next reader can see exactly which row makes a rule
fire — a bulk capture would hide that.

`fake-ai-arena` is not a fixture: the fake binary writes its artifacts itself
at test time (env switches documented in its own docstring), so a healthy fake
run needs its events and its result line kept mutually consistent — that exact
agreement is what keeps the invariant checker quiet in the end-to-end tests.

When an event-log field is renamed, the bug pattern to watch for is a fixture
that still carries the old shape: the tests they serve fail visibly, and the
observed vocabulary should be re-checked against the sources above before
editing.

## The purity reference binary

Purity checks diff the current tree against a binary built from a chosen
source state (for PR #187 that was `/tmp/pt-ref/ai_arena`, tmpfs and so
session-scoped). To recreate one, build it in a worktree at the chosen
commit:

    git worktree add ../RWE-purity <commit>
    cmake -S ../RWE-purity -B ../RWE-purity/build -DCMAKE_BUILD_TYPE=Release
    cmake --build ../RWE-purity/build --target ai_arena -j$(nproc)

and verify with same-seed runs diffed on the columns that must not have
changed (`cut -d, -f1-16` when columns are being appended).

A worktree and not `git stash`. A stash is a stack shared with whatever else
is using the repository, and a purity check is exactly the moment when an
unrelated `git stash pop` -- or a forgotten entry that was already on the
stack -- restores the wrong tree over the one being measured. The worktree
leaves the working copy untouched, so a build can be running in it while the
current tree is still being edited. Remove it with `git worktree remove`
when the comparison is done.
