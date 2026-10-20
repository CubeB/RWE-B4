# Submarine shadow and shipyard overlap handoff

Prepared 2026-10-20 for the agent taking over the owner's submarine rendering
report. This handoff changes documentation only. No submarine rendering fix
was implemented by the outgoing agent.

## Owner request and scope

The owner's report:

> Sub construction height is fine but i think the shadow isnt quite right and
> after construction it is displayed under the shipyeard.

The owner subsequently assigned #383 option B, all three #699 modes, and #700
spectator flow to the outgoing agent. Those changes were implemented, but the
submarine rendering report was omitted from that implementation batch. The
owner is now passing submarine work to a different agent.

Preserve the accepted construction height: the unfinished submarine follows
the build piece near the surface, then settles onto the seabed on completion.
Investigate and repair shadow eligibility/coverage and shipyard overlap from
the findings below. Do not lower the construction hull to disguise a rendering
problem. D-gun behaviour and lasers are outside this handoff.

## Read these before implementation

1. Root `AGENTS.md`, `CONTEXT.md`, `CLAUDE.md`, and the relevant ADRs, especially
   [ADR-0004](../adr/0004-a-spec-room-stands-between-the-decompilation-and-the-code.md).
2. [Submarine construction shadow findings](../SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md).
   This is an exact copy of the supplied spec-room document at
   `D:/RWE-spec-submarine-shadow-20261020/docs/SUBMARINE-CONSTRUCTION-SHADOW-FINDINGS.md`.
   SHA-256 of both copies:
   `8a86a0154bc33fa0e0a9e4f4a6b0be45c564fba29bc95c20f821334b678ceb3b`.
3. [Earlier naval findings](../NAVAL-CONSTRUCTION-DGUN-FINDINGS.md), particularly
   **Original-game observation: submarine under construction**, and
   [Naval systems parity](../NAVAL-SYSTEMS-PARITY.md).

The earlier naval document's claim of seabed placement throughout construction
is superseded by the owner's original-game observation. Do not restore it.
The newer shadow findings also qualify older broad claims about underwater
mobile shadows. Read their evidence and uncertainty rather than combining
incompatible statements from the older registers.

Implementation agents must never open `D:/bytetactics`. Further reference
questions go to a dedicated docs-only spec room in a separate checkout. Only
prose findings pass back; cite the finding in each implementation change.
Record any deliberate departure in the existing divergence register under
the project's rules. This handoff introduces no new reference conclusions.

## What the findings establish

These are static reference findings, not newly executed original-game checks.
The reference revision, executable identity, shipped asset hashes, routine
addresses and remaining uncertainties are in the full findings document.

### 1. While attached, no independent product shadow

The stock ARM shipyard's attached Lurker is drawn inside the yard's composite,
not as an independent top-level unit. Its independent shadow pass is skipped.
Both stock shipyards also have `NoShadow=1`, so there is no separate attached
Lurker silhouette to lower on the traced stock path.

RWE currently visits the product independently in its shadow loop and emits
a nanoframe shadow/cutout batch. This is the first concrete discrepancy to
address. Standalone nanoframes are different and must retain their applicable
shadow behaviour. Do not disable shadows for every unfinished unit.

Attachment ends at the final successful build step on the traced locally
simulated human/computer path. Factory bookkeeping may retain a child reference
longer: do not assume that any retained factory reference proves the child
should still be treated as attached. Remote-owner timing remains unverified.

### 2. Released shadow translation and water clipping are separate

For an independently drawn mobile unit, the reference copies its silhouette
five original-scale pixels right and anchors it vertically to sampled terrain,
including seabed. Under the standard camera the vertical displacement is
`floor(H / 2) - floor(G / 2)`, after the reference's whole-coordinate handling.
RWE's broad translation already agrees on flat ground. A global pad-height
subtraction is not supported by the findings.

The reference's height-buffered mobile silhouette is cut at sea level; a
fully submerged released Lurker need not show a full black silhouette.
RWE's fragment shader instead compares interpolated world height with ground
height. Underwater coverage, integer quantization and uneven-terrain sampling
are distinct questions, not evidence for one universal offset fix.

### 3. Yard overlap changes meaning at release

While attached, parent and child images are composed with a local per-pixel
vertical-height comparison; the child wins a tie. The result is then painted
as the parent's composite. This is not scene-wide camera-depth testing.

After release, ordinary ground-mode yard and submarine images paint in
increasing horizontal world-Z rows, 16 world units per row relative to the
camera. Later rows cover earlier rows regardless of elevation. Same-row
visible-list order is retained, but the findings do not establish the winner
for a particular save. Other movement modes can occupy later passes.

Consequently, a seabed submarine can paint over the yard in one position and
under it in another. Do not force it always above the yard, sort by elevation,
or equate the report with an incorrect hull origin. Changing camera Z can
move the row boundary without moving either unit.

RWE uses shared camera-depth geometry and separate finished/construction
batches. Reordering a list while leaving that shared depth test unchanged
may not reproduce image-level painter ordering. Preserve model self-occlusion
and terrain/other-object behaviour when choosing a rendering approach.

## Current RWE entry points

Line numbers below are navigation hints at `82a422c3`; recheck them after edits.

| Area | Files / current navigation |
| --- | --- |
| Shadow eligibility and attached nanoframe batch | `src/rwe/game/GameScene_render.cpp`, unit shadow loop around 1805-1874 |
| Unit batch depth state | `src/rwe/game/GameScene_render.cpp`, around 1908 |
| Finished models before construction meshes | `src/rwe/RenderService.cpp`, `drawUnitMeshBatch`, around 206 |
| Shadow transform / origin interpolation | `src/rwe/game/GameScene_util.cpp`, `drawUnitShadow`, around 775 |
| Mobile versus building shadow displacement | `shaders/unitShadow.vert` |
| Current ground-height fragment rejection | `shaders/unitShadow.frag` |
| Initial product build-piece height | `src/rwe/sim/GameSimulation.cpp`, factory creation around 5394 |
| Build-piece following and completion snap | `src/rwe/sim/UnitBehaviorService.cpp`, around 5933-6005 |

The renderer/shader files above and `UnitBehaviorService.cpp` are unchanged
between the findings' inspected RWE revision `7af1632c` and current HEAD.
`GameSimulation.cpp` has unrelated subsequent elimination changes; its line
numbers shifted. Inspect factory states including creating/done and building,
completion timing, and visibility eligibility before choosing an attachment
predicate. Avoid mutating simulation state or consuming simulation RNG from
presentation.

## Reproduction and acceptance work

Start with stock ARMSY building ARMSUB on flat seabed fixtures at water depths
30 and 60. Record active archive precedence and actual FBI/3DO/COB identities;
the spec-room assets were extracted snapshots, not the owner's active stack.
Data locations used by that investigation were `D:/RWE-extract/totala1` and
`D:/RWE-extract/rev31`; recent RWE native checks used `D:/RWE-Data`.

Record sea/seabed height, unit positions, yard heading, build-piece pose,
camera position/zoom, shadow settings, completion and attachment state. The
owner did not supply a map, screenshot, exact pose or measured pixels for this
rendering report. **Lava Run was supplied for the separate lava movement bug;
do not claim it is the submarine reproduction map.**

Check the following independently:

1. First attached frame, early/half/nearly finished nanoframe: no independent
   attached product shadow. Toggle vehicle shadows to identify which dark
   regions are silhouettes versus textures, tint or occlusion.
2. Final successful work step and the next two updates: preserve accepted
   height and test when independent drawing/shadow eligibility begins.
3. Released sub below the pad, moving out in both Z directions and sideways:
   compare yard overlap on either side of a 16-unit ordering boundary.
4. Displaced camera Z and same-row ties: separate established bucket behaviour
   from unresolved tie provenance; do not invent a unit-ID tie rule.
5. An independent above-seabed mobile fixture: isolate five-right/half-height
   displacement. A submerged independent fixture isolates sea-level clipping.
6. Regressions: standalone nanoframes, ordinary factory products, mobile and
   building shadows, hovercraft/floaters, model self-occlusion and fog. Repeat
   CORE shipyard/submarine checks without assuming ARM evidence proves them.

The owner normally performs visual play checks. Do not launch screenshot
automation while other agents are running; headless probes are permitted.
Separate static reference comparisons, RWE tests/renders, and actually
executed original-game observations in the results. The supplied shadow
findings ran no original executable, RWE render, test or demo oracle.

## Workspace and validation baseline

- Checkout: `D:/RWE`; branch `700-elimination-spectators`; HEAD
  `82a422c342239414d00854aa23de9c0f408d8f1d`.
- Recent commits: `bebebd2e` (#383 mission profile translation), `e93ed65e`
  (#699 modes/respawn), `82a422c3` (#700 watching plus regression fixes).
  Preserve these local changes when starting the submarine branch.
- Playable Release: `D:/RWE/build-release/rwe.exe`, built from `82a422c3`.
  This build contains **no submarine rendering repair**.
- Previous validation: 1846 permitted Debug cases plus five packet-checksum
  cases passed; all Debug targets and Release game built; Coast To Coast and
  Lava Run Deathmatch replays matched between Debug and Release. These checks
  are a baseline, not submarine-rendering acceptance evidence.
- Debug COFF budget: 1061 objects checked; largest `GameScene.cpp` at 24276
  sections against 24575. `save_util.cpp` is 24147 and
  `GameScene_commands.cpp` 24039. Avoid casually expanding widely included
  headers; check `tools/section-budget.sh build 75` after code changes.
- Never edit source while any make/compiler is active in either build tree.
  Use one builder, one target per make invocation; build `rwe` as well as
  `rwe_test`. Before a requested push, build all helper targets too. Rebuild
  Release for owner play-testing.
- Do not run the two TaChecksums tests. The full permitted filter used was
  `~*checksum*`, followed by the five packet cases separately. Consult the
  existing continuation brief for its other standing safeguards; its branch,
  backlog and test-count snapshots are older than this handoff.

At handoff, the following pre-existing owner files were dirty/untracked.
Do not overwrite, reset, stage or commit them as part of this work:

```text
CLAUDE.md
AGENTS.md
docs/NAVAL-CONSTRUCTION-DGUN-FINDINGS.md
docs/REVERSE-ENGINEERING-PRIORITIES.md
docs/TOTALA-EXE-WEAPONS.md
docs/TOTALA-EXE.md
docs/adr/0004-a-spec-room-stands-between-the-decompilation-and-the-code.md
docs/agents/continuation-brief.md
_mission_export_stage/
ta-gui-backgrounds-and-text-positioning.md
```

This handoff adds only itself and the exact findings copy. No code or build
was changed for the handoff. Use explicit staging paths; do not push or merge
as part of the transfer. The owner will direct the next agent's publication.
