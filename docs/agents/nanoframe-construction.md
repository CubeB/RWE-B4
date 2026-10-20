# Nanoframe construction rendering: comparison and fixes

Implementation note, 2026-10-20. The owner reported that a building's
nanoframe is not drawn quite like the original and asked for the
construction-phase logic to be compared. This note records the comparison and
the three concrete fixes made; it does not claim full parity.

## What already matched

The read-only comparison found these rules already in RWE and unchanged:

- the five build regimes (236-255, 201-235, 116-200, 31-115, 0-30) and their
  arithmetic, including only the low byte of the threshold reaching the
  remapper (`TOTALA-EXE-ECONOMY.md` "The build display";
  `NAVAL-SYSTEMS-PARITY.md` rule 19);
- the four-unit band and the four ways to draw a region
  (`TOTALA-EXE-ECONOMY.md`; `NAVAL-SYSTEMS-PARITY.md`);
- the sixteen-entry 160-175 triangle waves, xor 5 at 33 and xor 9 at 57, per
  unit (`TOTALA-EXE-ECONOMY.md`);
- the whole model, all visible pieces, no per-piece build-progress reveal;
- `DONT_CACHE` pieces included while unfinished and `DONT_SHADE` pieces still
  recoloured (`TOTALA-EXE-SHADING.md` sections 11 and 23);
- the scan-converted wireframe in colour B (`TOTALA-EXE-ECONOMY.md` "How the
  wireframe is drawn"; `BT-GRAPHICS-MODEL-RENDER-PARITY.md` rules 6-8).

## Fixes made

1. **Digger height bias.** The original's height byte is `modelY + 50`, or
   `modelY + 125` for a digger (ARMAMB, CORTOAST). RWE hard-coded 50, so the
   two diggers' sweep line sat 75 model units low. `UnitBuildingMeshRenderInfo`
   now carries `heightBias`, set from `unitDefinition.digger`, and
   `unitBuild.frag` uses the uniform. Cites `TOTALA-EXE-SHADING.md` sections
   04, 11 and 22 and model-render parity rule 8.

2. **Water blue remap of the fill colours.** The original recolours the frame
   to palette entries 160-175 and then applies the water pass to the finished
   picture, so an underwater fill is the blue-table entry, not the green RGB.
   `BuildPhase` now keeps the palette index beside each RGB, and
   `unitBuild.frag` looks the colour up in the combined shade/blue table
   (row 64, the same row `unitTexture.frag` uses) when the fragment is at or
   below sea level. The enemy-without-sonar cut still discards first. Cites
   BT-142 and `BT-GRAPHICS-MODEL-RENDER-PARITY.md` rule 8.

3. **Erased fragments no longer occlude.** The original writes erased pixels
   as the transparent key and its blitter skips them. RWE wrote alpha 0 but
   still wrote depth, so an unbuilt half of a frame could hide a later-drawn
   cloaked unit, projectile or wireframe. The Erase branch now `discard`s,
   which writes neither colour, depth nor mask; finished models are still
   drawn first, so the lab behind a frame keeps its already-painted bay.

A boundary test now pins the five regimes, their ratios and the low-byte wrap
(`src/rwe/game/buildphase.test.cpp`).

## Remaining differences

These are known and not changed here:

- the original keeps a per-pixel **topmost-surface** height; RWE uses the
  depth-winning fragment's own height, which differs on overhangs;
- the original stores that height in a byte that wraps; RWE keeps a float;
- an unfinished factory product is merged into the parent's composite image in
  the original; RWE still draws it as an independent unit (only its shadow is
  suppressed, `SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md` section 1);
- world paint order is still RWE's depth buffer, not the original's 16-unit
  Z rows;
- the wireframe pass still has no water tint/cut of its own.

No original-game execution or visual play-test was performed for these
changes; the comparison is static and the shader-level differences need the
owner's normal visual check.
