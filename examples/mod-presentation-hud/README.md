# Presentation HUD example

A `kind: "presentation"` package for MOD-10. It draws a small resource and
unit-count panel and handles its own click. It reads the game through the
filtered `PlayerView` only.

```lua
rwe.resources()      -- the viewer's own stockpile and storage
rwe.my_units()       -- the viewer's own units, with health
rwe.units()          -- own units plus foreign units the viewer can see
rwe.contacts()       -- radar/sonar contacts: id, x, z, sonar only
rwe.features()       -- exploded-ground features, `visible` says sight or memory
rwe.projectiles()    -- rounds over ground in live sight
rwe.selected()       -- the local selection's ids
rwe.unit_definition(type)
rwe.is_visible_at(x, z), rwe.is_explored_at(x, z)
rwe.draw_rect / draw_line / draw_text / draw_bar
rwe.submit_move / submit_stop / submit_attack
```

Nothing in that list reaches `GameSimulation`, `playerVisibility`, the shared
explored grid or the simulation's RNG. A presentation mod cannot reveal a
cloaked enemy, name a radar contact, or move a sync hash by drawing.

## Running it

Resolve the package with the rest of a launch set:

```sh
rwe_setup --resolve-mods examples/mod-presentation-hud/rwe_mod.json
```

Reload while playing by typing `+ReloadPresentation` into the message bar.
The running panel stays up if the replacement fails to parse or faults while
loading.

## What it is not

The engine's command line does not yet accept a resolved set directly (MOD-07
still owns the author/player workflow). Until then this package is exercised
by `rwe_test "[presentation]"`, which loads this exact file through the same
runtime the game uses.
