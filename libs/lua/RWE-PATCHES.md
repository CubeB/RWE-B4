# RWE's changes to vendored Lua

Source: `https://www.lua.org/ftp/lua-5.4.7.tar.gz`, downloaded 2026-11-02.
Tarball SHA-256: `9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30`.
Lua is MIT-licensed; its
`README` carries the licence and is kept beside the source.

Only `src/*.c` and `src/*.h` are vendored, minus the standalone `lua.c`,
`luac.c` and the upstream `Makefile`. `CMakeLists.txt` builds the rest as
`rwe_lua`.

## Patch: fixed string-hash seed (`lstate.c`)

`luai_makeseed` is defined to a constant before Lua's own definition, so the
seed comes from neither the clock nor addresses.

Why: mod scripts run on every lockstep peer and must see the same table
traversal order. Lua's stock seed is process- and time-dependent, which would
make `pairs()` over a string-keyed table a desync waiting in a mod.

Tradeoff: hash-flood resistance is no longer randomized. Lua only ever sees
strings supplied by a bounded, sandboxed mod script, and the host does not
expose untrusted remote strings to it.

No other source file is modified. The seed pin is part of the modding SDK's
determinism contract (docs/MODDING-SDK-FINDINGS.md MOD-SDK-004) and must not
be removed when updating Lua without updating that entry.
