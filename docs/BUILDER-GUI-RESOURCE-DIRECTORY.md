# Builder GUI resource roots

RWE extension finding, 2026-10-25, issue #850. This records the path split
needed for mods whose unit build pages live outside the ordinary GUI folder.
It does not claim the original discovers these mod namespaces automatically.

## Evidence and scope

- `BUILD-BUTTON-850` records the static GOG/v3.1 comparison and real stock
  ARMACK2 data: its enabled product buttons use 64 by 64 page-GAF frames in
  64 by 64 GUI rectangles. The reference binds the builder's named page GAF
  to its page GUI and uses the resolved image dimensions for input bounds.
- `MOD-DIRS-01` records Escalation's `guiE` resource namespace as an RWE
  extension. `PACKAGE-003` records that native package inspection identifies
  the package's actual root and discovered resource directories without
  modifying the package.
- GOLD10.2.0 `TAESC.gp3` has 132 `.gui` records under `guiE/`, all unit build
  pages in the inspected package listing, while its ordinary front-end GUI
  files remain in the base `guis/` assets. The corresponding page art remains
  under `anims/`.

## Path contract

RWE keeps `guis` as the default for front-end and ordinary screen layouts.
Builder pages have a separate `builderGuis` path, defaulting to the effective `guis` root (including an existing `--dir-guis` override), and a
separate `--dir-builder-guis` option. The game loader reads `<unit><page>.GUI`
from this builder-only root. A missing page in the configured root remains
missing; it does not silently fall back to another dataset's `guis/` copy.
This prevents a mod's `anims/ARMACK2.GAF` from being paired with a stock
`guis/ARMACK2.GUI` when the mod supplies its page under `guiE/`.

Package inspection reports the Escalation `guiE` root as `builderGuis`.
Explicit `builderGuis` metadata can override that discovered value. An
explicit general `guis` mapping remains independent and is never reinterpreted
as a builder-only override. Vanilla launch and unrelated screen lookup retain
their existing paths.

This is an RWE-original package-selection and resource-routing policy. The
stock path and ARMACK2 image/hit geometry are statically compared to the
original; there was no executed original-game run. A synthetic RWE overlay
reproduced the mixed stock-GUI/TAESC-GAF fallback, while the correctly paired
TAESC builder GUI and GAF produced matching 64 by 64 art and hit rectangles.
The reporter's active mod has not yet been confirmed, so those two overlays
are recorded as reproduction fixtures, not an assertion about their setup.

The ui_probe geometry check uses a fixed 640 by 480 output surface. Its vertical clipping on the 640-pixel TAESC page is therefore only a fixture observation; it does not establish clipping at the game's actual framebuffer size and UI scale. No tall-page layout change is part of this fix.
