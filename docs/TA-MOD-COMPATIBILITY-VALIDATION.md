# TA mod compatibility: implementation and bounded validation

2026-10-25. Target corpus is the owner's desktop TAESC GOLD10.2.0 package,
plus the nine community-AI distributions already available locally. This is
an initial compatibility pass, not a claim that every existing TA mod works.

## Implemented behavior and provenance

- Explicit `--dir-*` resource configuration is shared by the game and headless
  arena. The arena also reads side data and OTA metadata from those roots.
  AI selected/default profiles both use the configured AI root; no fallback
  into another installed mod's stock AI directory. MOD-DIRS-01 records this
  RWE extension and MOD-AI-01 records vanilla profile fallback.
- Download records with valid UNITMENU/UNITNAME still augment existing AI
  build lists when MENU/BUTTON are omitted. They do not manufacture human GUI
  buttons or BLANK builders. DOWNLOAD-LINK-01 records the original comparison.
  RWE's existing GUI-derived known-builder table is used as its build-storage
  boundary; parity for a builder with missing/empty GUI storage is not claimed.
- The classic controller receives immutable FBI `ai_weight` fragments with the
  effective low-bit Downloadable flag, including valid first-row promotion.
  The 63-byte payload bound, UNITNAME order, independent owning-weight/limit
  gates, whole-fragment execution and per-pass command enabling follow
  MOD-AI-02/03. This does not reinterpret the unused `ai_limit` FBI field.
- COB GET75 implements the extension author's locality query using RWE's
  Local/Remote simulation authority. Computer seats qualify; alliance alone
  does not. Section88 records Boolean, invalid-target and ownership-change
  policies as RWE adaptations. EXT-COB-75-01 is the primary extension finding;
  vanilla v3.1 answers zero for this unknown port.

Findings are `TA-MOD-COMPATIBILITY-FINDINGS.md`,
`DOWNLOAD-MENU-BUILD-LINK-FINDINGS.md` and `COB-PORT-75-FINDINGS.md`.
Their reference revision, executable hashes, evidence and limits remain in
those documents. No implementation session opened the decompilation.

## Reproduction tool

`tools/mod_compatibility_probe.py` is an RWE diagnostic with no original
counterpart. It runs the real `ai_arena`, records archive identities and
warnings, requires an arena result and hash log, checks profile-root selection,
and compares hash logs plus both arena CSVs across repeated same-seed runs.
Its report says bounded execution, not full mod parity. It copies no game
assets into the repository and does not load a mod's native executable/DLLs.

Example for the desktop package (PowerShell, from the repository root):

```powershell
$esc = 'C:\Users\Baile\Desktop\TAESC\Step_2_Install Main Files (copy all contents into your main TA folder)'
python tools/mod_compatibility_probe.py --engine build-release/ai_arena.exe `
  --data-path $esc --data-path D:\RWE-Data `
  --dir-ai ae --dir-gamedata gamedate --dir-guis guie `
  --dir-units unitse --dir-weapons weapone --dir-downloads downloadse `
  --dir-unitpics unitpice --require-profile --seconds 600 --repeat 2 `
  --out D:\RWE-mod-audit-20261025\esc-final
```

On this machine Python is `D:\msys64\mingw64\bin\python.exe`; substitute
your own interpreter, installed base-data directory and output directory.
The same seven directory overrides apply to `rwe.exe`. Base game/CC data is
required in addition to this unpacked Escalation distribution.

## Executed data checks

External artifacts: `D:\RWE-mod-audit-20261025`.

- Release game, arena and test executables built. The five `[mods]` cases pass
  45 assertions, covering namespace isolation, metadata bounds/flags/promotion,
  both command-lock passes, coordinate-free build links and GET75 execution.
  Full Release suite:1931 cases,1929 passed,2 skipped,211814 assertions passed;
  checksum tests excluded per project instructions. Skips require scenario
  data not configured for that run. Both skipped retail-data tests were then
  executed with RWE_SCENARIO_DATA set:2 cases and3289 assertions passed.
  One initial test fixture omitted its FBI
  wrapper block; corrected before this successful run.
- All Debug executables built. Full Debug suite with retail data enabled:
  1932 cases and215106 assertions passed, no skips; checksum tests excluded.
  MinGW section-budget check passed; worst object GameScene.cpp at24348
  sections against the24575 budget. GET75 shares the existing owner-query
  variant alternative instead of increasing the variant's alternatives.

- Eight Escalation archives were readable through the existing HPI tooling.
  Census:549 FBI copies and550 COB copies; counts include archive copies.
  All550 COB instruction streams decoded using known RWE opcodes. Maximum
  code39281 words, functions30, pieces167, statics85. This is a static data
  census, not proof that every script path executed.
- Conservative constant-stack scanning located GET75 in190 script copies.
  It can miss dynamically calculated query IDs. Other ignored FBI keys were
  catalogued; metadata presence alone was not called an engine requirement.
- An initial stock-root game succeeded while mixing stock definitions and
  Escalation resources. It was rejected as compatibility evidence.
- With all seven actual mod roots, two seed7 Hard-classic games each completed
  18000 ticks/600 seconds. Both read `ae/SeaBattle.txt`. Hash logs,
  `ai-arena.csv` and `ai-arena-events.csv` matched byte-for-byte.
- The real loader collected12 nonempty FBI command fragments and added6 AI
  build options absent from the GUI-derived tree, which contained138 builders.
- The nine community distributions were unpacked into external scratch, never
  installed over base data. BAI4's three executables were read as ZIP containers
  without execution. Each package's Default profile was copied into the
  selected test map's SeaBattle profile slot in scratch. Nine two-minute
  classic-engine profile runs completed. This establishes bounded acceptance
  of those profile texts, not complete gameplay parity for every companion
  unit add-on or original installer. Their identities are recorded in
  `ai-profile-fixtures.json` and `ai-pack-runs.json` outside the repository.

## Remaining compatibility work

No original game or patched Escalation executable was run for this pass.
Static reference checks, primary extension documentation and RWE execution
are distinct evidence classes. No screenshot/rendering comparison was made.

The bounded game does not exercise every upgrade, shield, teleport, transport,
veterancy, nuke, advanced unit or late-game interaction. Runtime warnings still
include unimplemented DefaultMissionType values and obsolete/unknown profile
targets. Record and test their actual behavior before assigning compatibility.
FBI translation/localization edge cases and non-ASCII command truncation are
not established by the ASCII corpus checks here.

Further work needs a compatibility matrix by mod version and feature, not a
single global compatible switch. Priorities are real midgame fixtures for
Escalation extensions, the remaining original AI/profile grammar edges,
companion AI-only unit/build-menu packages, mod/profile discovery in setup and
the launcher, and map-pack import as planned in `MAP-IMPORT-PLAN.md`.
