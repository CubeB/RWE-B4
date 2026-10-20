# Fullscreen option: two live paths that did not agree

RWE-original UI fix, 2026-10-20. There is no original counterpart to the
launcher's settings dialog or RWE's three-state window-mode slider; the
original's own video page is different by design
(`BT-FRONTEND-OPTIONS-PARITY.md`), so this is not a conformance change.

## The fault

Two settings paths wrote different keys into `rwe.cfg`:

- the launcher's **Fullscreen** checkbox wrote `fullscreen = true`;
- RWE's own options screens wrote `window-mode = windowed|borderless|fullscreen`.

At startup `window-mode` was tested before the legacy boolean, so a config
holding `window-mode = windowed` and `fullscreen = true` launched windowed:
the launcher checkbox was silently a no-op. Unchecking the box wrote nothing,
so a stale `window-mode = fullscreen` survived as well.

Separately, the **front-end** VISUALS page saved the chosen mode but never
applied it to the live window. Only the in-game VISUALRT page called
`SceneManager::setWindowMode`, so a mode chosen in the main menu waited for the
next launch -- contrary to the options parity note, which says OK applies it.

The in-game page also had no widget at all: `VISUALRT.GUI` drops the original's
`VIDSLDR`/`VIDVAL` resolution gadgets, and the existing wiring looked for them
anyway, so it was silently dead. It now gets a code-built three-stage
`VIDMODE` button below UNDO.

Finally, exclusive fullscreen asked for the display mode closest to the 800x600
window. On a 16:9 panel that could pick a differently shaped mode, and the
display path then stretched the whole frame -- interface and game -- to fill
the panel. Fullscreen now uses the desktop's own mode, at startup and when
switched live, so the native aspect is kept and nothing is scaled.

## The change

- `launcher/src/launcher/rweConfig.ts` reads `fullscreen` from either key and
  writes both keys together. Unchecking now writes `window-mode = windowed`, so
  it overrides a stale fullscreen choice.
- `src/main.cpp` gives an explicit `fullscreen = true` precedence over a saved
  `window-mode = windowed`, so existing configs with the old pair work without
  being rewritten first; a fresh install is explicitly windowed and the help
  says so.
- `MainMenuScene`'s options OK now calls `setWindowMode(pendingWindowMode)`, as
  the in-game page already did.
- `SceneManager::setWindowMode` records the applied mode in `GlobalConfig`, so
  a later options page opens on the mode that is actually live, and refreshes
  the window metrics immediately.
- `GameScene_menu.cpp` adds the `VIDMODE` button: windowed, borderless,
  fullscreen, cycling on click; the existing OK path applies it. It is
  RWE-original because the original does not allow video changes in battle
  (`TOTALA-EXE-INTERFACE.md` S:65, `BT-FRONTEND-OPTIONS-PARITY.md` S:7).
- `GameLaunch.cpp` and `SceneManager.cpp` use the desktop display mode for
  exclusive fullscreen, with a desktop-fullscreen fallback if the mode cannot
  be set.
- `GlobalConfig`'s fresh defaults are explicitly `windowed` and
  `ShadingMode::BuildingsOnly` (the original's On), matching the values the
  rendering path already used; `GlobalConfig.test.cpp` pins both.

## Verification

- Debug `rwe` builds; the complete permitted Debug suite passed after the
  change: **1857 cases / 193971 assertions** with `~*checksum*`.
- Launcher `npm run tsc` passes.
- `build-release/rwe.exe` was rebuilt from the final tree for the owner's play
  check.
- No visual check was run; the owner's normal play check applies. The display
  mode path in `GameLaunch` is unchanged, and the in-game page's behaviour is
  unchanged apart from the config bookkeeping.
