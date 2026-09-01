# Probes for the original executable

Small scripts used to work out how Total Annihilation behaves, so RWE can match
it. The findings they produced are written up in [`docs/TOTALA-EXE.md`](../../docs/TOTALA-EXE.md);
these are kept so the work is repeatable and checkable rather than a set of
claims you have to take on trust.

Nothing here ships with the engine and nothing here is built. They are plain
Python 3 with no dependencies, and they read a copy of the game you already own.

There is no Python on the Windows PATH on this machine; use the MSYS2 one:
`/d/msys64/usr/bin/bash -lc 'export MSYSTEM=MINGW64; source /etc/profile; cd /d/RWE/tools/exe && python pe.py'`.

Each script has the path to `TotalA.exe` hardcoded near the top — a GOG install
at `D:\Total Annihilation\Total Annihilation`. Change it if yours is elsewhere.
The binary they were written against is identified by hash in the findings
document; offsets will differ on other releases.

| Script | What it does |
|---|---|
| `pe.py` | Dumps the PE header and section table. Run this first on a new binary: everything else needs the VA↔file-offset mapping it prints. |
| `xref.py <va>...` | Finds every place the little-endian encoding of an address appears, with the section it lands in. This is how you find call sites, jump tables and pointer arrays. |
| `str.py <va>...` | Reads the NUL-terminated string at each address. Pairs with `xref.py` for identifying what a data address is. |
| `vt.py <va> [n]` | Dumps `n` little-endian dwords from an address, as a table of `+offset -> value`. For walking vtables and jump tables. |
| `flagreaders.py <bit> [listing]` | Finds every routine that reads one bit of the weapon definition's packed flag word at `wdef+0x111`. Grepping the offset alone gives a hundred hits across thirty flags; this follows the register forward from each load and keeps only the ones that isolate the bit you asked for. The shift-and-test form is matched loosely, so expect a false positive or two — check each hit before believing it. |
| `gaf.py <file.gaf>` | Reads a GAF archive and lists its sequences and frame sizes. Used to identify the exhaust and fog artwork. |
| `png.py` | Minimal PNG reader/writer, imported by the GAF tooling to dump frames for eyeballing. |

## The method that worked

Pivot on strings. The FBI and TDF key names are all present in the binary as
literals, so:

1. Disassemble `.text` in full to a flat listing, and extract the string table
   with file offsets alongside it.
2. Find where a key name is compared. That gives you the field's offset in the
   unit definition struct.
3. `xref.py` (or a grep of the listing) for that offset gives you every routine
   that reads the field — which is usually a short enough list to read.

Working from function entry points instead is much slower, because the binary
has no symbols.

The other half of the method is arithmetic: once a routine is decoded, transcribe
it into a small standalone C++ program, feed it the real FBI values for a unit,
and compare its output tick by tick against the same scenario in RWE. That is how
several plausible readings were caught being wrong — most usefully the `max`
versus `min` in the aircraft arrival profile, which reads either way in isolation
but gives every aircraft a top speed of one world unit per tick if you get it
backwards.

## Caveats

`xref.py` finds only **absolute** references — a four-byte little-endian copy of
the address sitting in the image. That covers pointer tables, jump tables and
data addresses loaded into registers, which is what you usually want. It does
**not** find ordinary calls, because `E8` encodes its target as a displacement
relative to the next instruction. Looking up a function and getting no hits is
the normal result, not a sign that nothing calls it; search the disassembly
listing for the resolved target instead.

The `.data` section's virtual size is larger than its raw size, so addresses past
the raw extent are zero-initialised at load and have nothing to read in the file.
`pe.py` prints both sizes; `xref.py` and `str.py` only cover the raw ranges.
