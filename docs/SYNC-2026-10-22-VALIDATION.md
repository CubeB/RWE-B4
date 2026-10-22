# Local/GitHub reconciliation and readable map descriptions

## Sources and scope

The 2026-10-22 owner instruction authorizes reconciliation of the local work
with GitHub `revival`, enlargement of the map description box instead of
ellipsis, and investigation of detached shadows in the supplied desktop image.
Local files and the original index were backed up outside the repository at
`D:/RWE-sync-backup-20261022-170447` before reconciliation. Owner documentation
edits and extracted mission data are preserved separately from published code.

The map layout implements the deliberate divergence recorded in
`TOTALA-EXE.md` section 88, supported by `MAP-DESCRIPTION-LAYOUT-FINDINGS.md`
MAP-LAYOUT-01/02. This is not a claim of original layout parity. Description
and size text are measured with the active font in a wider 600 by 440 dialog.
The description grows vertically, leaving separate space for the summary and
buttons. Text exceeding the usable screen height remains available by clicking
the description and scrolling; the full string is retained without ellipsis.

## Detached shadow diagnosis

The supplied `Screenshot 2026-10-22 170301.png` shows shadows displaced vertically
from the corresponding models. Inspection found an extra clip-space Y-negation
in `build-release/shaders/unitShadow.vert`, absent from both the tracked source
and GitHub. This reflects shadow positions around the viewport's horizontal
midline while model positions remain unchanged. That runtime copy was backed
up and replaced with the source shader. Only this runtime shader differed
from the source shader directory in the initial comparison.

`BUILDING-SHADOW-ANCHOR-FINDINGS.md` records the fresh byte-tactics static check
of original anchoring, reference revision/build, evidence and uncertainty.
The screenshot does not establish a defect in the five-pixel horizontal bias.
No change to original shadow arithmetic is required to remove the local flip.

Runtime shader copying is an RWE-original build mechanism with no original-game
counterpart: the original does not load RWE's GLSL files. Refreshing shaders on
every library build prevents a shader-only edit or an altered build-directory
copy from surviving a no-op compilation. This makes build assets agree with
their source and does not establish additional original rendering parity.

## Validation

- Debug `rwe` and Release `rwe` build successfully.
- The headless `ui_probe --map-layout` uses the real `MainMenuScene` dialog,
  its data-loaded controls/font and all 99 selectable maps in the installed
  Steam archive stack. Every complete description fits; description, summary
  and buttons do not overlap; the Cancel button activates through the real
  callback chain. The largest measured description is 18 pixels high at the
  new width, versus a 75-pixel minimum description area. The 640 by 480 render
  was inspected visually and shows complete readable text and controls.
- Release runtime shaders match the source directory byte for byte.
  A harmless sentinel was then appended to the generated Release shadow
  shader. An ordinary up-to-date `make rwe` removed it and restored the exact
  source hash without recompiling C++; the build-asset regression is verified.
- `tools/class-key.sh src` passes (172 declarations).
- The first regression run found two work-sound tests retaining deadlines
  for the old WorkerTime-based feature reclaim. Their finite job-completion
  budgets now accommodate RA-04's 92 working ticks for the existing fixture;
  assertions still require exactly one work announcement per job.
- The final Debug regression suite passes: 1924 cases, 200207 assertions.
  `RWE_SCENARIO_DATA` points at the installed retail archive directory, so
  model-height and sound-category shipped-data tests execute. Checksum cases
  are excluded with `~*checksum*` in accordance with the local run instructions.
- `make -j4 -k all` succeeds, including the game, complete test executable,
  scenario runner, replay viewer, headless arena and diagnostic probes.
- Two separate headless RWE-AI runs of Coast To Coast at seed 7, 30 seconds,
  ARM versus CORE, produce byte-identical hash logs (899 tick records).
  This establishes same-build RWE determinism, not original-game parity.

No original-game execution or demo-oracle verification has been performed for
this change. The UI probe executes RWE with real shipped data; the findings'
original comparisons remain static.
