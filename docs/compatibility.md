# Compatibility

Where Robot War Engine and Total Annihilation part company, and where they
deliberately do not.

Most of RWE's behaviour is not a guess: it has been read out of `TotalA.exe` and
written up in `docs/TOTALA-EXE.md` and its companions. This file is the short,
plain-language account of what is *left over* after that — the places RWE
knowingly does something else, and the places it keeps a quirk of the original
that looks like a defect and is not. The engineering detail, with the addresses
and the measurements, lives in `TOTALA-EXE.md` §88 (where RWE deliberately
differs) and §91 (what is decoded but not ported); a §n here is a section of
those documents.

Three rules decided what belongs in each list.

- A behaviour is **kept** when it is part of how the game looks or plays, even
  when it began as a defect. A palette table that wraps, a draw order that
  ignores height, a shell with no arc: these are what the game is.
- A behaviour is **changed** only for a stated reason, and the reason is always
  one of four: the original's limit no longer exists, the simulation has to stay
  deterministic, RWE's renderer has no palette to do the trick with, or it was
  measured and the original's answer made the game worse.
- A behaviour that is simply **not done yet** is not a compatibility decision
  and is not listed here. That is §91 and the roadmap.

---

## Kept: the original's quirks, reproduced on purpose

**Buildings have a purple halo.** Jon Mavor, who wrote the graphics engine,
names it as his own: the anti-aliasing filter on a building's cached bitmap
averages a real colour against whatever stood for transparent at the silhouette,
and what comes back is wrong. RWE reproduces it. A `DONT_CACHE` piece — a metal
extractor's arms — never gets one, in the original or here, because it is never
in the cached bitmap. §101.

**The unit shading ramp wraps instead of clamping.** The original takes a
per-vertex shade level and masks it with `& 0x1F`, so a face one row below zero
comes out at the *top* of the table rather than at black. That wrap is where the
original's contrast comes from, and RWE does the same.
`TOTALA-EXE-SHADING.md`.

**The sun vector is not normalised.** That is why the ramp is about thirteen rows
wide rather than the thirty-two the table has, and why the lighting is as hard as
it is. Normalising it would make a better-looking engine and a different game.
`TOTALA-EXE-SHADING.md`.

**Two units are drawn unshaded.** A unit whose FBI says `ZBuffer=0` has its
textured faces drawn raw, down a path that does not shade at all. CORFAV and
CORTRUCK are the only two in the shipped data. (RWE goes one step further and
draws such a unit's flat-coloured faces raw too -- see the small differences at
the end.) `TOTALA-EXE-SHADING.md` §23.

**Nothing is sorted by height.** The world has no depth buffer in the original:
everything is painted in call order, bucketed by world Z alone quantised to 16
units, with no secondary sort inside a bucket — even though the projection is
`screenY = z − y/2`. `sortbias` is parsed out of the FBI and then read nowhere.
§95.

**Effects are layered by hand, not by depth.** Particles are not in the world's
sort list at all. They sit in ten fixed buckets with each effect placed in one by
hand: wakes low, weapon impacts in the middle, thrust exhaust above them, and
damage smoke above everything including the health bars. So an effect is not
uniformly in front or behind, and RWE keeps the same order. §95.

**Shells fly flat.** The firing solution's high root is rejected by a ceiling of
45° for every target in range, so there is no lofted artillery arc in the game;
gravity reaches the flight time only through the angle. §88's ballistic entry,
and `TA-DEMOS.md`.

**Sight is much shorter than `SightDistance` suggests.** The ray-traced mode
walks an authored table whose radius caps at 8 cells (256 world units), and the
circular mode blits one of ten hand-drawn masks topping out at 14 cells (448
units). A unit with a huge `SightDistance` sees no further than that. §2.

**A wreck over water sinks, and one kind never does.** The corpse spawns at the
dying unit's exact height and gains a fixed 0.175 units a tick downward while it
is over water. A unit with `IsFeature=1` — the walls, and the floating dragon's
teeth — is exempt, and always leaves its intact wreck whatever its `Killed`
script asked for. `TOTALA-EXE-WRECKS.md`.

**The waypoint trail cuts through hills.** The line drawn between two order
positions is straight in three dimensions and does not drape over the ground.
That is the original's behaviour, not an omission.

**Shadows land on the sea bed, not on the water.** This was asked for as a fix
and declined: a building's shadow is projected flat onto the ground under it
wherever that ground is, a unit's shadow is its own silhouette displaced and is
not cut at the water line, and the only water test in either pass is the one that
stops a submerged map feature casting at all. Nothing here departs from the
original. End of `TOTALA-EXE-RENDER.md` §100.

---

## Changed: because the original's limit is gone

**Detection is answered live, every tick.** The original rebuilds each player's
enemy list every 30 ticks, so a target can be up to a second stale — visible for
a second after going dark, invisible for up to a second after being lit. RWE asks
at the moment it matters. The staleness is an artefact of a 1997 budget, and
copying it would mean carrying a 30-tick-old candidate list in hashed simulation
state.

**A goal something is standing on is relaxed to the nearest cell the unit could
stand on.** Nine path requests in ten aim at an occupied cell — that is what an
attack order is — and A\* can only answer such a request by closing every cell it
can reach. Measured on Crystal Maze, those searches were 15% of the total and
spent 78% of the whole expansion budget, the worst of them closing 80,716
vertices. The visible symptom was units scraping along walls while they waited.
RWE rings outward from the goal for the nearest cell the unit could stand on and
accepts arrival there. There is no game option for it; the `path_bench` harness
takes `--no-relax-blocked` so the two can be measured against each other.

**A waypoint retires at sixteen world units rather than five.** Five is fine for
a unit on its own and costs about a third of the arrivals in a crowd, because a
waypoint is a cell centre and a unit two cells across cannot always get within
five of one that another unit is standing on. At a hundred units in `path_bench`:
27 arrivals and 884 searches at five, against 49 and 509 at sixteen.

**Screenshots are 24-bit.** The original writes 8-bit PCX from an 8-bit screen.
RWE keeps the name, the folder, the numbering and the format family and widens
the pixels. The cursor is left out.

**A transport's 1.0-era `transportmaxunits` is read as well as
`transportcapacity`.** The v3.1 patch renamed the key and dropped the old one
from the executable, so a transport whose only capacity key is the old one
parses as zero and can never load. RWE takes the old key when the new one is
absent and says so in the log, because a data set patched only in the executable
is a real install and a silent one-seat ferry is worse than a warning. A patched
install is unaffected: the `.gp3` is searched before the `.hpi`, and the new key
wins when both are present. This restores the 1.0 number rather than the 3.1 one
for units the patch changed, and a transport with neither key still gets one
unit rather than being unable to load — that second difference predates this
change.

---

## Changed: because the simulation has to stay deterministic

**Build progress is an integer accumulator, not a float.** The original steps a
4-byte float by `p / BuildTime` a tick, which on a `BuildTime` that is an exact
multiple of `p` sometimes needs one step more than the division says. RWE adds an
integer each tick and is one tick fast on that minority. Closing the gap means
putting a `float` in hashed simulation state, which is the determinism hazard
`CLAUDE.md` opens with; if it is ever closed it must be closed with fixed-point
or a precomputed step count.

**The waypoint trail and the placement sweep are timed scene-side.** The original
stamps the issuing tick onto the order and takes each segment's phase from it, so
two orders queued a few ticks apart march very slightly out of step. Giving an
order an issue tick would mean carrying it through the game hash, the state dump
and the network protocol to buy an effect nobody can see.

**There is no bit-8 fallback target list and no Targeting Facility.** The
original's second candidate list is built from "on my radar picture at all",
outside the can-see gate, and that picture is recomputed for one player per tick
— so feeding it into a simulation decision would make the outcome depend on who
is sitting at the keyboard. RWE's radar and sonar contacts reach the minimap and
nothing else.

**A nanoframe appears a tick after the order that asked for it**, and a factory's
first lathe a tick after that. RWE cannot create a unit during the behaviour pass
— it is walking the unit map — so every builder defers to the end of the tick.
This shifts a job a tick or two later against the order; it does not change the
job's length, which is what the demo corpus measures.

---

## Changed: because there is no palette to do the trick with

**The interface's palette remaps are alpha blends.** Greying a gadget,
brightening the selected row of a list box and darkening a marked one are all
done in the original by running a screen rectangle through a 32×256 table of
palette indices. RWE has no palette at runtime. The list-box highlight
reproduces the measured median of that table's row 30 (1.84×) by compositing
white at 45%, which has the same shape — largest lift on dark pixels, none on
white — but not the same per-index behaviour.

**A cloaked unit is a 50% alpha blend**, not a lookup through the original's
256×256 ALPHA TABLE, and a depth prepass stands in for the private bitmap the
original composites into. The same half, a different mechanism.

**Skewed textured quads are tessellated.** The original scan-converts a quad
whole, interpolating the texture along both of its edge chains; RWE approximates
that warp with a 4×4 bilinear patch on non-parallelogram faces, which agrees
exactly on parallelograms and to within a texel elsewhere. Splitting such a quad
into two affine triangles instead is what made the solar collector's panels kink
along the diagonal.

**Backface culling stays on — and so does the original's.** This section used
to say the original has none, and that a single-sided quad facing away from the
camera rasterizes with its texture mirrored. It does not: the original's
scanline rasterizer assigns its left and right edges by the order the model
lists its vertices and draws a row only where the right edge is further right
than the left, so a face wound the other way round on screen paints nothing.
That is a backface cull by another name. RWE's culling agrees with it, and this
is no longer a difference.

**The fog raster is windowed on the camera.** At one texel per world unit a
640×640-cell map would be 400 MB. RWE holds a 2.6 MB window a few tiles larger
than the view, which is what the original effectively does anyway by composing
its overlay per visible screen tile.

---

## Changed: because it was measured and played better

**A feature's reclaim time counts its hit points and scales with the builder.**
The original is flat: `15 + (metal + energy) / 2` ticks, one tick of work a tick,
identical for every builder and with no term for the feature's own damage. RWE
charges `metal + energy + hitPoints / 4` at the builder's own work rate, which is
why a boulder takes longer than a bush and a shelled wreck clears quicker.
Changing it back would move every reclaim time in the game.

**Circular sight is a computed disc with no floor.** RWE stamps
`dx² + dy² ≤ r²` in place of the ten hand-drawn masks, which differs only around
the rim, keeps the ceiling of 14 cells and drops the floor of 5. A unit with no
`SightDistance` sees only the ground it stands on, in every mode; restoring the
floor would give blind units 160 world units of sight the moment the option is
switched.

**A mobile unit's `buildangle` is ignored.** The original overwrites a mobile
unit's spawn heading with the raw FBI value, which for everything but the ten
capital ships is zero. RWE takes a factory-built unit's facing from the pad's
`QueryBuildInfo` instead, which is what actually points it out of the yard.

**Right-clicking an enemy still attacks it** when nothing better applies. The
decode of the right-click chain lists capture and reclaim on an enemy and no
attack arm at all, which cannot be the whole story and is recorded as unsettled;
RWE keeps an attack arm below the two.

**A transport only picks up your own units.** The original applies no ownership
or alliance test anywhere on the load path. Whether the pickup then completes was
never traced and never play-tested, so this is a house rule kept for want of
evidence rather than in defiance of it.

**A weapon that names no `tolerance` gets 256.** The original picks 2000 or 150
depending on two flag bits whose meaning is still unknown, so neither figure can
be chosen without a guess about which units carry them.

---

## Both ways, on a switch

Some differences are options rather than decisions, and the default is the
original in every case.

| Option | Default | What the original does |
| --- | --- | --- |
| Shading (Off / Units / Buildings / Both) | Buildings | Shades buildings and features and never a mobile unit. Units and Both are the divergence. |
| `shading-strength-units`, `shading-strength-buildings` | 40 | The full depth of the measured ramp. At 100 the table's snap to the nearest palette entry reads as banding on a modern screen, and row 0 is a true black. |
| Units Smooth (`anti-alias-units`) | off | Box-filters a building's cached bitmap and nothing else. |
| `vehicle-shadows` | on | The original's own display-options bit: buildings go on casting when it is clear. |

---

## Smaller differences, one line each

- **The selection plate is skipped by the index the model header names.** This
  was listed as a difference on the grounds that the original assumes primitive
  0 and so drops a real face on the wreckage models. It does read index 0 — but
  it has already swapped the declared plate into that position while loading the
  model, so the two come to the same thing and no face is lost. Not a difference.
- **A finished `ZBuffer=0` unit's flat-coloured faces are unshaded** along with
  its textured ones; the difference is nine faces of CORFAV and one of CORTRUCK.
- **Nanolathe spray lands on the roof**, not inside the model, because RWE's
  spray is depth-tested so a construction aircraft cannot cover its own beam.
- **Exhaust occlusion is depth-tested** rather than hand-layered.
- **Off-map fog cells read as the nearest on-map cell**; reading them as clear
  leaves a strip of map showing at the border.
- **Only the heading half of the `turret=0` firing check is enforced**, the
  simulation having no hull pitch to compare an elevation against.
- **The silo readout is an addition**: the original shows the stockpile only as a
  button caption and the missile under construction nowhere at all.
- **The anti-missile coverage ring is gated on carrying an `interceptor`
  weapon**, not on the `antiweapons` FBI flag, which RWE has never parsed. The
  two sets are identical in the shipped data.
- **A builder walking to its site already says `Nanolathing`**, RWE's single
  build order covering the walk and the work where the original runs a move
  mission first.
- **A resurrect shows the reclaim cursor**, the base game's `CURSORS.GAF` not
  containing the `cursorrevive` sequence that `rev31.gp3` adds.
- **Aircraft have no pitch** (§1); the `BrakeRate` nose re-aim is in.

---

## Where to go next

- `docs/TOTALA-EXE.md` — the index to every finding, and §88 and §91 in full.
- `docs/TOTALA-EXE-SHADING.md` — the shaded rasterizer, which governs how a unit
  is lit.
- `docs/TA-PATCHES.md` — what the official v3.1 and the 2013 unofficial patch
  each changed, and what of it RWE needs.
- `docs/ROADMAP.md` — what is not done yet, which is a different question from
  what is done differently.
