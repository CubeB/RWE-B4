# CORE Solar Collector: deliberately unshaded pieces

Spec-room finding SOLAR-SHADING-01, 2026-10-23. Documentation only under
ADR-0004. Read alongside `TOTALA-EXE-SHADING.md` sections 11, 12(a) and 23,
and `ANTIALIASING-ELIGIBILITY-FINDINGS.md`. No RWE code or tests were inspected
or changed in this pass.

## Result

**The whole stock CORSOLAR legitimately remains free of directional model
shading.** Its compiled creation script disables shading on all ten model
pieces. The base remains cacheable but is explicitly unshaded; the shell,
four legs and four panel wings are explicitly unshaded and excluded from the
cache. Therefore the reported absence of a visible directional-shading change
between Off and Buildings on this unit is consistent with the shipped data.
It is not, by itself, evidence that RWE's Buildings setting is broken.

| Shipped piece(s) | State after creation | Expected directional shading |
| --- | --- | --- |
| `base` | Cached; shading explicitly disabled | None |
| `shell` | Cache and shading explicitly disabled | None |
| `leg1` through `leg4` | Cache and shading explicitly disabled | None |
| `wing1` through `wing4` | Cache and shading explicitly disabled | None |

These are names in the shipped COB data, not identifiers transferred from
byte-tactics. The entire compiled script was listed: no later SHADE or CACHE
instruction restores those flags. Opening or closing the panels does not make
them directionally shaded again.

## Shade and cache flags are different controls

The original starts model pieces with both CACHE and SHADE enabled. These are
independent flags. A script may keep a piece in the cache while disabling
its directional shading, exactly as CORSOLAR does to the base.

For a piece that remains in the shaded cache rasterizer but has SHADE disabled,
the original assigns the neutral shade-table row 15 rather than calculating
directional shade from its normals. That is the register's approximately
unity row; this finding does not promise bit-identical colours between raw
texture rendering and that palette lookup for every palette index.

A completed building's DONT_CACHE pieces instead take the live unshaded pass,
independently of the global shading option. Thus CORSOLAR's non-base pieces
have two separate reasons not to acquire directional shading: their own
script shading flag is off and their live drawing path is unshaded anyway.

The distinction also matters to antialiasing. The base's cache eligibility
survives DONT_SHADE, so it can still receive the original's cached-image
antialias reduction. Its DONT_CACHE shell, legs and wings remain outside that
reduction. Antialiasing being on or off does not restore their shade flags.

## ZBuffer is enabled on this fixture

Both the base and v3.1 CORSOLAR FBI declare `BMcode=0`, `ThreeD=1` and
`ZBuffer=1`. CORSOLAR is therefore building-class and its ordinary completed
cached image is eligible for the original height-buffered textured path.
The completed-`ZBuffer=0` textured-shading bypass in section 23 **does not
explain this unit**. Its deliberate script flags do.

The original FBI parser defaults an absent `zbuffer` field to 0. That default
does not apply here because the shipped files explicitly set it to 1.

## Reference and real-data evidence

- Read-only byte-tactics revision:
  `e89721c46c3fa06e59558419b54fe26061fafb73`, clean checkout at this pass's
  investigation. Reference build GOG/v3.1 `TotalA.exe`, MD5
  `8e74a1dffa1f5988624c52048f5b20cd`, independently measured from the installed
  executable earlier in this same spec-room session.
- Static engine evidence: default piece flags `0x45AEC0`; independent cache
  and shade changes `0x480DB0` and `0x480DF0`; building/cache rasterizer
  selection and height-buffer allocation `0x4586A0`; disabled-SHADE neutral
  row and normal-based shade selection `0x459C70`; live unshaded update
  `0x459200`; FBI parser `0x42BF40`, including the ZBuffer read at
  `0x42C5A6`.
- Real shipped data were read directly from `D:/RWE-Data/totala1.hpi` and
  `D:/RWE-Data/rev31.gp3` using the existing archive reader. The compiled COB's
  creation instructions and complete list of cache/shade operations were
  inspected using the existing COB listing tool. The extracted base COB
  used for that listing was independently hashed and is identical to the
  archived base and v3.1 COB. The shipped base BOS corroborates the same
  piece choices; no source excerpt is transferred here.

| Entry | SHA-256 |
| --- | --- |
| Base and v3.1 `scripts/corsolar.cob` | `b55d1ec6ebb4cb0282edcbea106d075b732f7cc46c87ad46021b8bec09a45c9e` |
| v3.1 `units/corsolar.fbi` | `966aaa689ebfb19237c7c218c9b388e3a3df8b9e33c1fa20329d71df690561e5` |
| Base `objects3d/corsolar.3do` | `692fbe819f80db3da0eac7e439aaa7127c23eafa71502ea951d486fe55e54e00` |

Archive SHA-256 identities are recorded in
`MAP-DESCRIPTION-LAYOUT-FINDINGS.md` from the same data installation.

## Limits and useful verification

This is a **static original-reference and real shipped-data check**. No
original game was launched, no reference screenshot or RWE rendering was
measured, no demo oracle was run, and no byte-tactics rebuild/checker was
executed. The user's active archive overrides, precise script timing and
pixel colours remain unverified. The expected flag state applies after the
creation script has established it; transient construction/composite images
are a separate check.

For checking whether a global shading switch works, use a different shipped
building with at least one visible cached piece whose SHADE flag remains on.
CORSOLAR is useful for the complementary check: directional shading must not
be forced onto its authored unshaded base or its live panels. A normal-based
lighting change on this fixture would need a deliberate divergence decision,
rather than being described as a correction to original behavior.
