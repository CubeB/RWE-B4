# Labels: the two axes

Every issue and pull request carries **two** labels, one from each axis:
where the change lands, and why it exists. Purpose takes exactly one, scope
one where one fits, so the pair reads as "sim, conformance" or
"presentation, maintenance". Apply them as you open the thing:
`gh issue edit <n> --add-label scope:sim --add-label purpose:conformance`,
or the same two `--label` flags on create.

## Scope — where the change lands (one where one fits)

- `scope:sim` — Deterministic simulation: hashed state, unit, weapon and economy behaviour.
- `scope:presentation` — Engine code outside the simulation: renderer, sound, UI, camera, effects. Previously `scope:client`, which collided with network-client usage in multiplayer contexts.
- `scope:launcher` — The Electron launcher and lobby: its app, master server and game server.
- `scope:demos` — Reading `.tad` recordings and mining them into fixtures: tools, decodes, episodes.
- `scope:tests` — Test code and the harness it runs on: `*.test.cpp`, the shared fixtures, the `rwe_test` target.
- `scope:tooling` — Scripts run by hand: the arena and `TotalA.exe` probes in `tools/`, the root format and fixup scripts.
- `scope:build` — How the tree is configured and compiled: `CMakeLists.txt`, `cmake/`, the `devbox` and Nix toolchains.
- `scope:ci` — The workflows themselves: `.github/workflows/`, the build matrix, what a pull request waits on.
- `scope:docs` — The registers and working notes: `TOTALA-EXE.md`, `TA-DEMOS.md`, `ROADMAP.md`.

`tests` is the one scope that cuts across the rest: a test is `scope:tests`
even beside the code it exercises, and a change that spans two scopes is
honest under either.

## Purpose — why it exists (exactly one)

- `purpose:conformance` — Behaviour decoded from `TotalA.exe` that RWE should match. Evidence is a routine or a fixture.
- `purpose:rwe-original` — Behaviour TA has no counterpart for: RWE's AI, UI and own design choices. Not a gap.
- `purpose:divergence` — A knowing difference from the original, recorded in `TOTALA-EXE.md` §88.
- `purpose:maintenance` — Neither conformance nor design: perf, refactors, build, packaging, CI.

`conformance` and `rwe-original` are the pair to keep apart, because they look
alike from the outside and say opposite things: the first means the decoded
routine or the fixture is the standard and a mismatch is a bug, the second means
TA has nothing to say here. A change that departs from a decoded behaviour on
purpose is neither — it is `divergence`, and §88 has to say so before the label
is honest.

## Other labels

Applied with the pair and never instead of it:

- `renderer`, `ui`, `media` — the parts of `scope:presentation` that work is filed under.
- `ai`, `multiplayer`, `save-load`, `io`, `diagnostics` — subjects no scope names.
- `compatibility` — community content and mods: map packs, Core Contingency and Battle Tactics, where `io` is the parsers themselves.
- `performance`, `refactor` — the kind of change, so a refactor is `purpose:maintenance` *and* `refactor`, never `refactor` alone.
- `ci:full` — not on the axes at all: it is the switch `build.yml` reads to run the whole matrix.
- `bug`, `enhancement` and the other GitHub defaults — unchanged; they say what the item is, not where the change lands or why.
