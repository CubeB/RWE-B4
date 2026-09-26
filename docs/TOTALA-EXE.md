# What the original executable does

Notes from disassembling Total Annihilation's `TotalA.exe` to work out how the
original behaves, so RWE can match it instead of guessing. Everything here was
read out of the binary and, in most cases, checked against RWE by replaying the
decoded arithmetic side by side with ours.

This is a findings document. It records addresses, constants and algorithms —
the facts you need to reimplement a behaviour — not disassembly listings. Short
instruction excerpts appear only where the exact operand order is the finding.

Aircraft *attack* behaviour — which point an aircraft is sent to, rather than
how it flies there — lives in a companion document,
[TOTALA-EXE-MISSIONS.md](TOTALA-EXE-MISSIONS.md): the mission table, the bomber
attack run, the gunship pendulum, and what `hoverattack` and
`maneuverleashlength` really do.

**Two documents here were not read by this project**, and are marked so on
every page:

- [TOTALA-EXE-EXTERNAL.md](TOTALA-EXE-EXTERNAL.md) — findings from the
  Nanolathe project's independent clean-room reading of the same binary, taken
  in as a cross-check. It settled **six** disagreements against this corpus,
  every one of them in their favour, and closed five questions these documents
  had left open in writing. Read its first section before relying on anything
  in it; nothing there has been verified here unless it says so.
- [TOTALA-EXE-AI.md](TOTALA-EXE-AI.md) — the retail computer player, which
  this project never decoded. Its headline is that the original has **no
  transport policy at all** and **never gives an aircraft an attack order**,
  which reclassifies a good deal of RWE's AI work from conformance to
  deliberate divergence. Corroborated from an unrelated direction by
  [TA-COMMUNITY-AI.md](TA-COMMUNITY-AI.md), the profile-modding community's
  own account of the same faults.

## The binary

| | |
|---|---|
| File | `TotalA.exe`, GOG release |
| Size | 1,178,624 bytes |
| MD5 | `8e74a1dffa1f5988624c52048f5b20cd` |
| SHA‑1 | `764dc919c3bd0365751aefba8e9a667299a3ce2e` |
| Image base | `0x400000` |

Section table, which is all you need to convert between virtual addresses and
file offsets:

| Section | VA | File offset | Size |
|---|---|---|---|
| `.text` | `0x401000` | `0x400` | `0xFA92A` |
| `.rdata` | `0x4FC000` | `0xFAE00` | `0x468C` |
| `.data` | `0x501000` | `0xFF600` | `0x10A00` |
| `.tls` | `0x52C000` | `0x110000` | `0x14` |
| `.rsrc` | `0x52D000` | `0x110200` | `0xA58` |

Method that worked, in case this needs redoing: disassemble `.text` in full to a
flat listing, extract the string table with file offsets, then pivot on strings.
The FBI/TDF key names are all present as literals, so finding where a key is
compared gives you the field's offset in the unit definition struct, and from
there `mov` sites against that offset give you every routine that reads it.
Scanning `.data`/`.rdata` for the little-endian encoding of a code address finds
call sites and jump tables. The helper scripts used for this (VA↔offset
conversion, string reads at an address, pointer-table dumps) are in `tools/exe/`.

---

## Index

The 114 findings, numbered to 116: §83 and §84 do not exist, so the count is
two short of the last number. The two to read before changing anything are
**§88**, where RWE deliberately differs from the original on purpose, and
**§91**, what is decoded but not ported; both are in this file, below.

Everything else lives in a subject file. **The numbers never move**, so a
`§n` written anywhere in the tree still names the same finding.

1. [Aircraft flight model](TOTALA-EXE-MOVEMENT.md#1-aircraft-flight-model)
2. [Fog of war and line of sight](TOTALA-EXE-VISION.md#2-fog-of-war-and-line-of-sight)
3. [Nanolathe spray and the construction display](TOTALA-EXE-ECONOMY.md#3-nanolathe-spray-and-the-construction-display)
4. [Effects a script asks for: `emit-sfx`](TOTALA-EXE-RENDER.md#4-effects-a-script-asks-for-emit-sfx)
5. [Render order](TOTALA-EXE-RENDER.md#5-render-order)
6. [The damage pipeline](TOTALA-EXE-WEAPONS.md#6-the-damage-pipeline)
7. [Missile and projectile flight](TOTALA-EXE-WEAPONS.md#7-missile-and-projectile-flight)
8. [Which way a finished building faces](TOTALA-EXE-DATA.md#8-which-way-a-finished-building-faces)
9. [Target selection, and what the firing modes really do](TOTALA-EXE-WEAPONS.md#9-target-selection-and-what-the-firing-modes-really-do)
10. [Weapon target eligibility, `0x49ABB0` in full](TOTALA-EXE-WEAPONS.md#10-weapon-target-eligibility-0x49abb0-in-full)
11. [`turret`, and what a hull-mounted gun waits for](TOTALA-EXE-WEAPONS.md#11-turret-and-what-a-hull-mounted-gun-waits-for)
12. [Where a shell actually lands, screen shake, and waterline](TOTALA-EXE-WEAPONS.md#12-where-a-shell-actually-lands-screen-shake-and-waterline)
13. [What an aircraft does with nothing to do](TOTALA-EXE-MOVEMENT.md#13-what-an-aircraft-does-with-nothing-to-do)
14. [Recoil](TOTALA-EXE-WEAPONS.md#14-recoil)
15. [Thermal vents](TOTALA-EXE-DATA.md#15-thermal-vents)
16. [Wakes, thrust, and the small unit flags](TOTALA-EXE-MOVEMENT.md#16-wakes-thrust-and-the-small-unit-flags)
17. [Jamming, stealth and cloaking](TOTALA-EXE-VISION.md#17-jamming-stealth-and-cloaking)
18. [Where a radar contact is drawn, and where it is not](TOTALA-EXE-VISION.md#18-where-a-radar-contact-is-drawn-and-where-it-is-not)
19. [The order panel, and which flag gates which button](TOTALA-EXE-INTERFACE.md#19-the-order-panel-and-which-flag-gates-which-button)
20. [Who may reclaim, and who may not be reclaimed](TOTALA-EXE-ECONOMY.md#20-who-may-reclaim-and-who-may-not-be-reclaimed)
21. [Stockpiled weapons and interception](TOTALA-EXE-WEAPONS.md#21-stockpiled-weapons-and-interception)
22. [The D-gun](TOTALA-EXE-WEAPONS.md#22-the-d-gun)
23. [The streaming economy](TOTALA-EXE-ECONOMY.md#23-the-streaming-economy)
24. [Small systems: hit density, regrowth, kamikaze, paralysis, move rate](TOTALA-EXE-DATA.md#24-small-systems-hit-density-regrowth-kamikaze-paralysis-move-rate)
25. [The detection rings on the minimap](TOTALA-EXE-VISION.md#25-the-detection-rings-on-the-minimap)
26. [The marching waypoint trail](TOTALA-EXE-RENDER.md#26-the-marching-waypoint-trail)
27. [The building placement box](TOTALA-EXE-RENDER.md#27-the-building-placement-box)
28. [The placement animation](TOTALA-EXE-RENDER.md#28-the-placement-animation)
29. [The nuclear silo stockpile and build progress](TOTALA-EXE-ECONOMY.md#29-the-nuclear-silo-stockpile-and-build-progress)
30. [The FBI keys, and which of them the exe actually reads](TOTALA-EXE-DATA.md#30-the-fbi-keys-and-which-of-them-the-exe-actually-reads)
31. [Load eligibility -- `0x489A90`, `CanLoadUnit(transport, candidate)`, in full](TOTALA-EXE-TRANSPORTS.md#31-load-eligibility----0x489a90-canloadunittransport-candidate-in-full)
32. [Capacity and size -- the six transports, as the 3.1 exe sees them](TOTALA-EXE-TRANSPORTS.md#32-capacity-and-size----the-six-transports-as-the-31-exe-sees-them)
33. [Which mission an order produces](TOTALA-EXE-TRANSPORTS.md#33-which-mission-an-order-produces)
34. [`Ground_Pickup` -- `0x406780`, the crane flow (sea *and* hover)](TOTALA-EXE-TRANSPORTS.md#34-groundpickup----0x406780-the-crane-flow-sea-and-hover)
35. [`Ground_Unload` -- `0x406900`](TOTALA-EXE-TRANSPORTS.md#35-groundunload----0x406900)
36. [`VTOL_Pickup` (`0x4111B0`) and `VTOL_Unload` (`0x411560`)](TOTALA-EXE-TRANSPORTS.md#36-vtolpickup-0x4111b0-and-vtolunload-0x411560)
37. [The drop-legality test `0x47DB70`, and `ATTACH_UNIT` / `DROP_UNIT`](TOTALA-EXE-TRANSPORTS.md#37-the-drop-legality-test-0x47db70-and-attachunit--dropunit)
38. [The carried state, damage, and dying with the transport](TOTALA-EXE-TRANSPORTS.md#38-the-carried-state-damage-and-dying-with-the-transport)
39. [The hover transport flow -- what the Bear and Turtle actually are](TOTALA-EXE-TRANSPORTS.md#39-the-hover-transport-flow----what-the-bear-and-turtle-actually-are)
40. [Implementation spec for RWE](TOTALA-EXE-TRANSPORTS.md#40-implementation-spec-for-rwe)
41. [Loose ends](TOTALA-EXE-TRANSPORTS.md#41-loose-ends)
42. [The CD player object](TOTALA-EXE-MUSIC.md#42-the-cd-player-object)
43. [Registry footprint](TOTALA-EXE-MUSIC.md#43-registry-footprint)
44. [Track types and the CDLISTS table](TOTALA-EXE-MUSIC.md#44-track-types-and-the-cdlists-table)
45. [The five music modes](TOTALA-EXE-MUSIC.md#45-the-five-music-modes)
46. [The situation state](TOTALA-EXE-MUSIC.md#46-the-situation-state)
47. [The battle/peace evaluator, 0x494E70](TOTALA-EXE-MUSIC.md#47-the-battlepeace-evaluator-0x494e70)
48. [The chooser in situational mode, 0x4CDE96](TOTALA-EXE-MUSIC.md#48-the-chooser-in-situational-mode-0x4cde96)
49. [What RWE should implement](TOTALA-EXE-MUSIC.md#49-what-rwe-should-implement)
50. [The interface colour table: found, and it was never a table of constants](TOTALA-EXE-RENDER.md#50-the-interface-colour-table-found-and-it-was-never-a-table-of-constants)
51. [Which line is darker: the inner - but only where there are two colours](TOTALA-EXE-RENDER.md#51-which-line-is-darker-the-inner---but-only-where-there-are-two-colours)
52. [The unit model draw path](TOTALA-EXE-RENDER.md#52-the-unit-model-draw-path)
53. [There is no lighting. None.](TOTALA-EXE-RENDER.md#53-there-is-no-lighting-none) — **superseded**
54. [The palette tables — real, loaded, and not for models](TOTALA-EXE-RENDER.md#54-the-palette-tables--real-loaded-and-not-for-models) — **partly superseded**
55. [Texture lookup and frame selection](TOTALA-EXE-RENDER.md#55-texture-lookup-and-frame-selection)
56. [The quad mapping — decoded from `0x4C7580`](TOTALA-EXE-RENDER.md#56-the-quad-mapping--decoded-from-0x4c7580)
57. [Why ARMSOLAR looks wrong in RWE](TOTALA-EXE-RENDER.md#57-why-armsolar-looks-wrong-in-rwe)
58. [Where RWE diverges, item by item](TOTALA-EXE-RENDER.md#58-where-rwe-diverges-item-by-item)
59. [Implementation spec for RWE](TOTALA-EXE-RENDER.md#59-implementation-spec-for-rwe)
60. [Which keys open what](TOTALA-EXE-INTERFACE.md#60-which-keys-open-what)
61. [TABMENU.GUI -- the multiplayer drop-down bar](TOTALA-EXE-INTERFACE.md#61-tabmenugui----the-multiplayer-drop-down-bar)
62. [The GAME OPTIONS menu -- ARMOPT.GUI / COROPT.GUI](TOTALA-EXE-INTERFACE.md#62-the-game-options-menu----armoptgui--coroptgui)
63. [The exit flow -- EXITMENU.GUI, YESORNO.GUI, RESTART.GUI](TOTALA-EXE-INTERFACE.md#63-the-exit-flow----exitmenugui-yesornogui-restartgui)
64. [The Save/Load dialogs](TOTALA-EXE-INTERFACE.md#64-the-saveload-dialogs)
65. [The in-game options screens -- PREFS.GUI + the RT pages](TOTALA-EXE-INTERFACE.md#65-the-in-game-options-screens----prefsgui--the-rt-pages)
66. [The screen-squish transition (in-game only)](TOTALA-EXE-INTERFACE.md#66-the-screen-squish-transition-in-game-only)
67. [Settings lifecycle -- live apply, Undo, Restore, OK, Cancel](TOTALA-EXE-INTERFACE.md#67-settings-lifecycle----live-apply-undo-restore-ok-cancel)
68. [MUSICRT -- the CD music panel](TOTALA-EXE-INTERFACE.md#68-musicrt----the-cd-music-panel)
69. [GAMMA.GUI and other leftovers](TOTALA-EXE-INTERFACE.md#69-gammagui-and-other-leftovers)
70. [Pause semantics](TOTALA-EXE-INTERFACE.md#70-pause-semantics)
71. [Implementation spec for RWE](TOTALA-EXE-INTERFACE.md#71-implementation-spec-for-rwe)
72. [The key pipeline: translator, ring buffer, dispatch](TOTALA-EXE-KEYBOARD.md#72-the-key-pipeline-translator-ring-buffer-dispatch)
73. [The dispatch at 0x495e90, all forty cases](TOTALA-EXE-KEYBOARD.md#73-the-dispatch-at-0x495e90-all-forty-cases)
74. [Ctrl+letter is data-driven: `CTRL_%c` and the FBI Category field](TOTALA-EXE-KEYBOARD.md#74-ctrlletter-is-data-driven-ctrlc-and-the-fbi-category-field)
75. [The digits, Alt, and the `SwitchAlt` registry value](TOTALA-EXE-KEYBOARD.md#75-the-digits-alt-and-the-switchalt-registry-value)
76. [F4, Space, and the sliding panel](TOTALA-EXE-KEYBOARD.md#76-f4-space-and-the-sliding-panel)
77. [The second layer: screenshots, movies, and the gated debug keys](TOTALA-EXE-KEYBOARD.md#77-the-second-layer-screenshots-movies-and-the-gated-debug-keys)
78. [The order keys are GUI data: `quickkey`](TOTALA-EXE-KEYBOARD.md#78-the-order-keys-are-gui-data-quickkey)
79. [What RWE binds today](TOTALA-EXE-KEYBOARD.md#79-what-rwe-binds-today)
80. [The gap table](TOTALA-EXE-KEYBOARD.md#80-the-gap-table)
81. [Implementation list for RWE, in priority order](TOTALA-EXE-KEYBOARD.md#81-implementation-list-for-rwe-in-priority-order)
82. [Field offsets](TOTALA-EXE-DATA.md#82-field-offsets)
85. [The D-gun: `ATTACKSPECIAL`, and what `commandfire` really costs you](TOTALA-EXE-WEAPONS.md#85-the-d-gun-attackspecial-and-what-commandfire-really-costs-you)
86. [Patrol: what makes a unit leave its route](TOTALA-EXE-MOVEMENT.md#86-patrol-what-makes-a-unit-leave-its-route)
87. [Pathfinding: one scheduler, a bug-walk, and a unit that never waits](TOTALA-EXE-MOVEMENT.md#87-pathfinding-one-scheduler-a-bug-walk-and-a-unit-that-never-waits)
88. [Where RWE deliberately differs](#88-where-rwe-deliberately-differs)
89. [Keys the original parses and never uses](TOTALA-EXE-DATA.md#89-keys-the-original-parses-and-never-uses)
90. [`AirToAir`: a pursuit, and the twenty units that are not a hop](TOTALA-EXE-WEAPONS.md#90-airtoair-a-pursuit-and-the-twenty-units-that-are-not-a-hop)
91. [Still unknown or unported](#91-still-unknown-or-unported)
92. [The D-gun's projectile: `noexplode`, the full-range flight, and who gets hurt](TOTALA-EXE-WEAPONS.md#92-the-d-guns-projectile-noexplode-the-full-range-flight-and-who-gets-hurt)
93. [Abandoned nanoframes decay](TOTALA-EXE-ECONOMY.md#93-abandoned-nanoframes-decay)
94. [Air repair pads: who goes, when, and what the pad does about it](TOTALA-EXE-ECONOMY.md#94-air-repair-pads-who-goes-when-and-what-the-pad-does-about-it)
95. [What blocks a unit: the map square, the passability class, and why a hovercraft cannot cross a wreck](TOTALA-EXE-MOVEMENT.md#95-what-blocks-a-unit-the-map-square-the-passability-class-and-why-a-hovercraft-cannot-cross-a-wreck)
96. [Capture: where the progress is kept, and what sets the clock](TOTALA-EXE-ECONOMY.md#96-capture-where-the-progress-is-kept-and-what-sets-the-clock)
97. [Automatic reclaim, `autoreclaimable`, and what `working` in SOUND.TDF is for](TOTALA-EXE-ECONOMY.md#97-automatic-reclaim-autoreclaimable-and-what-working-in-soundtdf-is-for)
98. [Resurrect: a real mission, a crude corpse mapping, and nothing that can use it](TOTALA-EXE-ECONOMY.md#98-resurrect-a-real-mission-a-crude-corpse-mapping-and-nothing-that-can-use-it)
99. [The unit info panel, and the three things the gadget renderer does with a colour](TOTALA-EXE-INTERFACE.md#99-the-unit-info-panel-and-the-three-things-the-gadget-renderer-does-with-a-colour)
100. [Two shadow passes: a unit's shadow is a copy of its own sprite, a building's is a projection](TOTALA-EXE-RENDER.md#100-two-shadow-passes-a-units-shadow-is-a-copy-of-its-own-sprite-a-buildings-is-a-projection)
101. [The purple halo on buildings, and what stood for transparent](TOTALA-EXE-RENDER.md#101-the-purple-halo-on-buildings-and-what-stood-for-transparent)
102. [The ground path follower: an aim point on the segment, and two brakes into the corner](TOTALA-EXE-MOVEMENT.md#102-the-ground-path-follower-an-aim-point-on-the-segment-and-two-brakes-into-the-corner)
103. [Loading is issued to the transport, and what a click on a unit does](TOTALA-EXE-INTERFACE.md#103-loading-is-issued-to-the-transport-and-what-a-click-on-a-unit-does)
104. [The end of a game: a banner, a fade, and a chart that runs its bars up](TOTALA-EXE-INTERFACE.md#104-the-end-of-a-game-a-banner-a-fade-and-a-chart-that-runs-its-bars-up)
105. [The campaign: campaign files, mission files, and a unit's first orders](TOTALA-EXE-DATA.md#105-the-campaign-campaign-files-mission-files-and-a-units-first-orders)
106. [Blast impulse: `ImpulseFactor` and `ImpulseBoost` are not in this game](TOTALA-EXE-DATA.md#106-blast-impulse-impulsefactor-and-impulseboost-are-not-in-this-game)
107. [Download menus: how a patch adds a button to a builder it does not ship](TOTALA-EXE-INTERFACE.md#107-download-menus-how-a-patch-adds-a-button-to-a-builder-it-does-not-ship)
108. [What Space shows: a strip from the bottom and the players at the top right](TOTALA-EXE-INTERFACE.md#108-what-space-shows-a-strip-from-the-bottom-and-the-players-at-the-top-right)
109. [`0x46d630`, the unit-table packet builder, and the checksum behind it that got away](TOTALA-EXE-DATA.md#109-0x46d630-the-unit-table-packet-builder-and-the-checksum-behind-it-that-got-away)
110. [Why a construction aircraft finishes a build one tick early: a second lathe on the creation tick](TOTALA-EXE-ECONOMY.md#110-why-a-construction-aircraft-finishes-a-build-one-tick-early-a-second-lathe-on-the-creation-tick)
111. [The settle, re-read: one phase for every player, and what a refusal costs](TOTALA-EXE-ECONOMY.md#111-the-settle-re-read-one-phase-for-every-player-and-what-a-refusal-costs)
112. [`BUGGER_OFF` is write-only, and a blocked site only waits](TOTALA-EXE-ECONOMY.md#112-bugger_off-is-write-only-and-a-blocked-site-only-waits)
113. [Mission rules at runtime: what each tests, how often, and how they combine](TOTALA-EXE-DATA.md#113-mission-rules-at-runtime-what-each-tests-how-often-and-how-they-combine)
114. [A mission unit's scripted orders at runtime](TOTALA-EXE-DATA.md#114-a-mission-units-scripted-orders-at-runtime)
115. [The campaign's screens, progression and ending movies](TOTALA-EXE-DATA.md#115-the-campaigns-screens-progression-and-ending-movies)
116. [What a round aims at on a unit, and why the water comes after it](TOTALA-EXE-WEAPONS.md#116-what-a-round-aims-at-on-a-unit-and-why-the-water-comes-after-it)

## 88. Where RWE deliberately differs

Recorded so these do not get "fixed" back later by someone comparing against the
original. `docs/compatibility.md` is the same list in plain language, grouped by
why rather than by subsystem, and adds the other half of the question -- the
quirks of the original that RWE reproduces although they look like defects.

- **A goal something is standing on is relaxed to the nearest cell the unit
  could stand on.** The original aims a search at the goal cell and has no
  notion of settling for somewhere beside it; RWE already relaxes a goal it
  cannot reach, using the cheap first pass (§87), and this is the same idea
  applied before the search rather than after the walk fails.

  It is here because the case is not a corner. Measured on Crystal Maze with
  `RWE_PATH_PROFILE=1`, **nine path requests in ten aim at a cell something is
  standing on** — that is what an attack order is, since the goal resolves to
  the target's own position, and a move order onto a building or into a crowd
  has the same shape. A cell with a unit on it is not walkable, so A\* can only
  answer by closing every cell it can reach: such searches were about 15% of
  the total and spent **78% of every expansion the budget had**, the worst of
  them closing 80716 vertices — twenty ticks of the whole budget for one unit,
  with a hundred units queued behind it. The visible symptom was units scraping
  along walls, because a unit whose search has not landed walks the
  straight-line stand-in (`0x44F3F2`, §87) and a straight line into a maze wall
  is a unit sliding along it.

  Relaxing by a single cell is not enough: when a crowd has gathered round the
  target the ring is blocked too. `PathFindingService::beginSearch` therefore
  rings outward from the goal, up to `blockedGoalSearchRadius` (eight) cells,
  for the nearest cell the unit could actually stand on, and accepts arrival
  there. A few hundred footprint tests in place of tens of thousands of
  expansions.

  It relaxes the goal and caps nothing, which is what makes it safe: the
  search still returns the shortest route to the relaxed goal, and a search
  that must go the long way round a wall is untouched, because it was never
  the goal test holding that up. A limit on the *work* was tried first and
  rejected: "give up after N expansions without ever getting nearer the goal"
  cannot tell "has not found the way round yet" from "there is no way round".
  The case that killed it was one wall across a 64×64 map with the gap at one
  end — a perfectly good route of 1534 vertices whose longest run without
  getting any nearer was over a thousand, so every limit small enough to catch
  a sealed goal also cut that route off at the wall. A relaxation has no such
  failure mode, which is why it is the one that shipped.

  Measured in `path_bench`'s crowded case (`--units 200 --crowd 1 --spacing
  16`), off against on: **105 → 115 units arrived, 13777 → 3520 expansions a
  search**, 160 → 116 searches run to exhaustion, and the request queue cleared
  on 184 more ticks of 900. The two configurations where the goal is not
  blocked come out byte-identical. `--no-relax-blocked` restores the original
  behaviour.

- **Detection is evaluated live, every tick.** The original answers "can this
  player see that unit" out of a snapshot: `0x40AA40` rebuilds each player's
  enemy list only every 30 ticks (`0x40AD20`), so a target can be up to a
  second stale — visible for a second after it has gone dark, and invisible for
  up to a second after it has been lit. RWE calls `canSeeUnit` at the moment it
  matters. The staleness is an artefact of the original's budget, not a
  behaviour worth reproducing, and copying it would mean carrying a
  30-tick-old candidate list in hashed simulation state.
- **No bit-8 fallback list, and no Targeting Facility.** The original's second
  candidate list (§17a) is appended *outside* the can-see gate, on `unit+0x110`
  bit 8 — "on my radar picture at all" — walked only when the first list is
  empty and only when the player owns a unit with `istargetingupgrade`. RWE has
  neither the list nor the flag. The reason it should stay that way is the radar
  picture the list is built from: it is recomputed for **one** player per tick
  (§17, the visibility pass), so feeding it into a simulation decision would
  make the outcome depend on who is sitting at the keyboard.

  This entry used to give a second reason, and that reason is **wrong**
  (checked 2026-09-23): it said `ARMTARG`/`CORTARG` are Core Contingency and
  so are not in the shipped data here. Core Contingency *is* installed on this
  machine -- `ccdata.ccx` sits in the engine's own data directory beside the
  base game, `.ccx` is in the VFS extension list, and `hpi_test list` finds
  `ARMTARG.FBI` and `CORTARG.FBI` in it along with their models, gadgets and
  sounds. Every arena run and play-test in this repo has had them loaded. The
  determinism reason stands on its own and is why nothing changes, but "the
  units are not here" must not be repeated.
  RWE's radar and sonar contacts therefore reach the minimap (`canDetectUnit`)
  and nothing else; every simulation decision goes through `canSeeUnit`.

- **Mobile units can be shaded, and shading can be switched off by category.**
  The original shades buildings and features and never a mobile unit. The
  shaded rasterizer `0x459C70` itself branches on nothing but the piece's COB
  `SHADE` bit, which is why this entry used to say the original shades
  everything; but it has one caller, the cache renderer `0x4586A0`, and that
  sends only an object with `unit+0x110` bit 29 set -- `bmcode == 0`, a
  building, or the Feature Unit -- to it, and only with SHADING on
  (`0x45873C`, `0x45874A`). Everything else, every tank, aircraft, ship and
  commander, is cached by the unshaded twin `0x459830` (`0x45878B`), whatever
  the option says. Found 2026-09-11. RWE shaded units by default until the
  same day; the default is now Buildings, which is the original's On, and
  the Units and Both stages that shade units are the divergence. RWE's
  VISUALS page carries a four-state Shading switch -- Off, Units,
  Buildings, Both -- and two rwe.cfg keys, `shading-strength-units` and
  `shading-strength-buildings`, blend the measured `PALETTE.SHD` ramp towards
  the unshaded colour. Both default to 40. The shape of the ramp is the
  original's in full -- the per-vertex level, the wrap, the unnormalised
  normals and sun, the row truncated per pixel -- and only its depth is pulled
  in: at 100 the table's snap to the nearest palette entry reads as banding on
  a modern screen and row 0 is a true black, which was tried on 2026-09-08 and
  rejected on sight. Units sat at 25 until 2026-09-11, when a play-test found
  it too faint to see on a commander at all; they went to 40 with the
  buildings. (An earlier version of this entry said both defaulted to 100.
  They never shipped that way; see the comment on `shadingStrengthUnits` in
  `GlobalConfig.h`.)
- **Units can be anti-aliased, on a switch that is off by default.** The
  original box-filters a building's cached bitmap and nothing else (§101),
  and by default so does RWE. A finished building's dont-cache pieces, such
  as a metal extractor's top, are drawn sharp as the original drew them, and
  the switch does not reach them. `anti-alias-units` (Units Smooth on the
  VISUALS page) also filters everything solid that is not the ground. It
  was on by default, and the extractor's top smoothed, for part of
  2026-09-11; both went back to the original the same day, when the player
  asked for it exactly.
- **A finished `ZBuffer=0` unit's flat-coloured faces are unshaded.** The
  original leaves such a unit's textured quads raw but still shades its
  flat-colour n-gons (TOTALA-EXE-SHADING.md S:23). RWE draws the whole model
  raw: its mesh does not keep the two kinds of face apart, and the difference
  is nine faces of CORFAV and one of CORTRUCK, the only two units that say
  `ZBuffer=0`.
- **Nanolathe spray lands on the roof**, not inside the model. The original
  samples the landing height inside the model too, which it can afford because
  its spray is composited in a late layer. RWE's is depth-tested so a
  construction aircraft can cover its own beam, so a landing point inside the
  geometry would be swallowed.
- **Exhaust occlusion is depth-tested**, not hand-layered — see §5.
- **A cloaked unit is half-blended with alpha**, not through the original's
  256×256 ALPHA TABLE, and a depth prepass stands in for the private bitmap the
  original composites into. Same 50%, different mechanism — see §17.
- **The fog raster is windowed on the camera.** At one texel per world unit a
  whole 640×640-cell map would be 400 MB, past most drivers' limits, so RWE holds
  a 2.6 MB window a few tiles larger than the view. This is what the original
  effectively does anyway, composing its overlay per visible screen tile.
- **Off-map fog cells read as the nearest on-map cell.** Reading them as "clear"
  leaves the frame's ragged edge with no neighbouring tile to cover it, and a
  strip of map shows through at the border.
- **No pitch**, see §1 (the `BrakeRate` nose re-aim is ported now).
- **A mobile unit's `buildangle` is ignored.** The original overwrites a mobile
  unit's spawn heading with the raw value (§8), which for everything but the ten
  capital ships is zero and so agrees with RWE's half-turn default anyway. RWE
  takes a factory-built unit's facing from the pad's `QueryBuildInfo` instead,
  which is what actually points it out of the yard, so a ship coming off a
  slipway keeps the pad's heading rather than being spun a quarter turn.
- **A gunship's nose follows its flight path**, so it crosses its ring side-on.
  The original does the same — but it does *not* hold its aim regardless of
  where the nose points, which an earlier reading of this claimed. Gunship
  rockets are `turret=0`, so §11 applies to them and the original holds fire
  until the nose is within the weapon's tolerance, which for `vtol_rocket` and
  friends is 8000, about 44°. RWE now does the same.
- **A nanoframe appears a tick after the order that asked for it, and a
  factory's first lathe a tick after that.** The original creates the frame and
  lathes it inside one mission pass; RWE cannot create a unit during the
  behaviour pass (it is walking the unit map), so every builder defers to
  `spawnNewUnits` at the end of the tick and meets its own frame on the next
  one. That is where the first lathe lands — for a mobile builder and, since
  §110 was ported, for the two that a construction aircraft pays there. A
  factory takes one tick more: its state machine spends the tick it picks the
  creation up starting `StartBuilding` and lathes from the tick after. None of
  this changes a job's *length*, which is what the corpus measures and what the
  build fixture asserts; it shifts the whole job a tick or two later against the
  order. Closing it means creating units inside the behaviour pass, which is the
  iterator hazard that section exists to avoid.
- **Build progress is an integer accumulator, not the original's float.** The
  original steps a 4-byte float by `p / BuildTime` a tick and stops at the end
  value (§23), so where `BuildTime` is an exact multiple of `p` it sometimes
  needs one step more than the division says — ten of the fifteen such pairs in
  the demo corpus do. RWE's `UnitState::addBuildProgress` adds
  `workerTimePerTick` to an unsigned counter and finishes at `buildTime`, which
  agrees with the original on every job whose `BuildTime` is *not* an exact
  multiple, and is one tick fast on the rest. Kept deliberately: closing it
  means putting a `float` in hashed simulation state, which is the determinism
  hazard `CLAUDE.md` opens with, and the prize is one tick on a minority of
  builds. If it is ever closed, it must be closed with fixed-point or a
  precomputed step count, never with a `float`.
- **The waypoint trail marches off the global clock.** The original takes each
  segment's phase from the age of the order being drawn (§26), so two orders
  queued a few ticks apart march very slightly out of step. RWE's orders do not
  record when they were issued, and giving them an issue tick would mean
  carrying it through the game hash, the state dump and the network protocol to
  buy an effect nobody can see, so every segment marches together.
- **The placement sweep is timed scene-side.** Same reasoning: the original
  stamps the tick onto the order (`order+0x46`, §28) and RWE notes when a build
  order first appears instead. It is decoration, and decoration does not belong
  in the simulation.
- **The silo readout is an addition, not a restoration.** The original shows the
  stockpile only as the caption on the MAKENUKE button and shows the missile
  under construction nowhere at all -- no bar, no percentage, no format string
  (§29). RWE borrows the `RELOAD1` rectangle, which `SIDEDATA.TDF` defines and
  the original parses and then never reads.
- ~~**The selection plate is skipped by its declared index.**~~
  **Retracted 2026-09-24 -- this was never a divergence.** The entry said the
  original skips primitive 0 whenever a selection primitive is declared and
  so "on the wreckage models drops a real face". It does skip index 0, but the
  3DO relocation pass at `0x4CB370` has already **exchanged the declared plate
  with primitive 0** and rewritten the index to zero, on every object of every
  model, before anything is drawn. Skipping index 0 therefore *is* skipping
  the declared index, which is what RWE does. See §51.
- ~~**Backface culling stays on.**~~ **Retracted 2026-09-24 -- also never a
  divergence.** The entry said the original has none. It has no explicit test,
  but `0x4C7580` assigns its two edge chains by vertex-index direction and
  emits a row only when `right - left > 0` (`0x4C79DF`), so a quad with the
  opposite projected winding paints nothing at all. That is a signed-area
  backface cull written as a loop bound, and RWE's culling agrees with it. The
  one thing still to match is the `<= 0` sense, which drops edge-on faces too.
  See §51.
- **Skewed textured quads are tessellated, not scan-converted.** The original
  interpolates the texture along the quad's own edges per scanline; RWE
  approximates that warp with a 4x4 bilinear patch on non-parallelogram faces
  (§51), which agrees exactly on parallelograms and to within a texel
  elsewhere.
- **Only the heading half of the `turret=0` check is enforced.** The original
  compares the required elevation against the hull's own pitch at `unit+0x68`
  (§11). RWE's simulation has no hull pitch — `UnitState` carries a rotation and
  nothing else — so comparing against a notional zero would be a different rule
  wearing the same name rather than the original's. The heading half is the one
  that stops a unit shooting sideways and backwards, and it is the half that is
  implemented.
- **A feature's reclaim time counts its hit points, and scales with the
  builder.** The original is flat: `15 + (metal + energy)/2` ticks, one tick of
  work a tick, identical for every builder and with no term for the feature's
  own `damage` (S:97). RWE charges `metal + energy + hitPoints/4` at the
  builder's `workerTimePerTick`, which came out of a play-test round and is why
  a boulder takes longer than a bush and a shelled wreck clears quicker.
  Changing it back would move every reclaim time in the game, so it is recorded
  rather than corrected.
- **Circular sight is a computed disc, and it has no floor.** The original's
  Circular mode blits one of ten hand-drawn masks from `anims/vismasks.gaf`,
  radius 5 to 14 cells, so its effective sight is `clamp(SightDistance/32, 5,
  14)` (§2). RWE has no reader for those masks and stamps the disc
  `dx² + dy² ≤ r²` instead, which differs only around the rim. It keeps the
  ceiling of 14 cells and drops the floor of 5: a unit with no `SightDistance`
  sees only the ground it stands on, in every mode. Restoring the floor would
  give blind units 160 world units of sight the moment the option is switched.
- **The interface's palette remaps are alpha blends.** Three things the
  original does by running a screen rectangle through a 32x256 palette-index
  table -- greying a gadget, brightening the selected row of a list box, and
  darkening a marked one -- have no equivalent in a renderer with no palette.
  The list-box highlight reproduces the measured median of `PALETTE.LHT` row
  30 (1.84x) by compositing white at 45%, which has the same shape (largest
  lift on dark pixels, none on white) but not the same per-index behaviour.
  Greying is still not drawn at all (S:19, S:99).
- **The anti-missile coverage ring is gated on the weapon, not on
  `antiweapons`.** The FBI flag is a display flag and RWE has never parsed it;
  the ring is drawn for any unit carrying an `interceptor` weapon instead. The
  two sets are identical in the shipped data -- `ARMAMD` and `CORFMD` -- so
  only a mod could tell (S:25, S:99).
- **A builder walking to its site already says `Nanolathing`.** The original
  runs a move mission first and its footer says `Moving`; RWE's single
  `BuildOrder` covers the walk and the work, so the mission line changes one
  order earlier (S:99).
- ~~**Every player's economy settles on the same tick.**~~ **Not a divergence
  after all** (§111). This entry said the original staggers the settle per
  player because `player+0xF0` starts at whatever each player's counter started
  at. It starts at the current game tick, written for all ten player slots in
  one loop (`0x464990` -> `0x464700`), so every player of a game settles on the
  same tick, as RWE's `gameTime % 30 == 0` does. Only a saved game that
  recorded different `UpdateTime`s could stagger them. The demo corpus agrees:
  60,588 of 60,760 `0x28` samples from all 86 senders sit within 6 ticks of a
  multiple of 30, and the stall episodes in `src/rwe/sim/tad_stall_episodes.h`
  would move for every player but the first under a per-player phase. Two
  players' `0x28` samples on nearby ticks can therefore be compared, within the
  few ticks each sender's clock lags. The storage episodes stay per player
  because a sample is its sender's own state, not because of the phase.
- **A transport only picks up your own units.** The original applies no
  ownership or alliance test anywhere on the load path: not in `CanLoadUnit`
  (§31), not at any of its five call sites, and not in the LOAD button's
  cursor arm, which will show `cursorpickup` over an enemy (§103). Whether the
  pickup then completes was never traced and never play-tested, so this is a
  house rule kept for want of evidence rather than in defiance of it: the test
  lives once, in `canLoad` in `src/rwe/game/DefaultAction.cpp`, so that
  removing it later is one edit rather than a hunt.
- **A resurrect shows the reclaim cursor.** The original has `cursorrevive`,
  id 10, for it (§103). The base game's `CURSORS.GAF` does not contain that
  sequence — only `rev31.gp3`'s does — and RWE has never loaded it, so the
  reclaim cursor stands in, as it already did for a resurrect order in flight.
- **Right-clicking an enemy still attacks it when nothing better applies.**
  The decode of the right-click chain (`0x43FA00`, §103) lists capture and
  reclaim on an enemy and no attack arm at all, which cannot be the whole
  story and is recorded there as unsettled. RWE keeps an attack arm below the
  two, so a tank right-clicking an enemy shoots at it.
- **A waypoint retires at sixteen world units, not the original's five.**
  `Navigator::Update` compares the squared distance against 25 (`0x44F205`);
  RWE keeps sixteen for an intermediate waypoint and eight for the last, which
  is also `hasReachedGoal`'s own tolerance. Five is fine for a unit on its own
  and costs about a third of the arrivals in a crowd, because a waypoint is a
  cell centre and a unit two cells across cannot always reach within five of
  one another unit is standing on. Measured, at a hundred units in
  `path_bench`: 27 arrivals and 884 searches at five, against 49 and 509 at
  sixteen. §102 has the rest of it.
- **Screenshots are 24-bit, and leave the cursor out.** The original writes
  8-bit PCX from its 8-bit screen; RWE draws in true colour, so it keeps the
  name, the folder, the numbering and the format family and widens the
  pixels. The picture is read back after the scene draws and before the
  cursor and the debug windows go on top; whether the original's included
  its cursor is not decoded (§77).
- **A weapon silent about `tolerance` gets 256, not the original's
  bits-driven default.** Finding `tolerance` zero, the original picks 2000
  (`0x7D0`, about 11°) when `unit+0x110` bits 2–3 are set and 150 (`0x96`,
  about 0.8°) otherwise (`0x49D895`–`0x49D8B2`); what the bits mean is
  unsettled (§91). RWE substitutes 256 (`src/rwe/io/weapontdf/WeaponTdf.cpp`),
  and only weapons silent about both `tolerance` and `pitchTolerance` land on
  it: a weapon that names a tolerance and stays quiet about pitch gets that
  named figure for pitch too, per `0x49D8B4` — the sixty shipped weapons that
  do so are unaffected, as is the one (`vtol_emg`) that sets both. 256 is
  kept rather than either decoded figure because both hang on `unit+0x110`
  bits 2–3, whose meaning is unknown — 2000 only for the units that carry
  them and 150 for everything else, so neither can be chosen without a guess
  about which units they belong to. The number itself is a hand-set value
  from before the decode, loosened from 182 for playability, and no recorded
  rationale ties it to anything; it is left standing because the honest
  alternatives are a guess between two unknowns.
- **A unit blocking a build site is told to move, and the site is swept again
  on every failed attempt.** The original's `BUGGER_OFF` is write-only -- the
  only reader of `unit+0x10f` bit 3 is the COB `get` (§112) -- and its site
  check `0x47DB70` only waits -- a constructor ten tries thirty ticks apart
  before giving the order up, a factory every fifteen ticks for as long as it
  takes. A friendly unit left on a spawn point therefore stalls the yard for
  good, which is what a Coast To Coast play-test saw as "Core shipyard
  stopped building as it got blocked by a scout ship". RWE sweeps the *site* --
  the new unit's footprint at the spawn point, not the building's own cells,
  where a hull at the pad is outside the building and would never hear about it
  -- and hands any mobile unit standing there a `BuggerOffOrder`, on every
  blocked attempt rather than only when the script set the flag. The one-shot
  sweep on the set was itself an invention of 2019 (commit 6191923a) rather
  than a port. The AI half of the same report -- an idle hull should not stand
  on its own yard's pad in the first place -- is #189, the naval rally station.

---
- **A unit reclaim pays out as it goes, in both resources.** The original pays
  `trunc((1 - progress) * buildcostmetal)` in one lump as the reclaimed unit
  dies, metal only (`0x402666`, §97). RWE credits each sixteen-tick bite its
  share of the metal *and* the energy that went into the unit. The bite
  itself and its cadence are the original's (#19, 2026-09-24); the payback
  schedule was RWE's before and stays so, because changing it moves every
  reclaim's economy and wants its own pass with a play-test.

- **A repath is rate limited to once every 60 ticks, and a goal that only
  drifts keeps the route already in hand.** The limit is the original's
  (`WantsPath`, `0x44F260`, §87) and had not been ported: RWE re-asked on
  every trigger, which in a fight was constant -- over one Crystal Maze game
  the median gap between two of a unit's searches was 30 ticks and three
  quarters were under 60. Each re-ask threw away the search in flight, and
  `expansionsAbandoned` was 8.5% of every expansion the game spent. It is
  ported now, at one ask per unit per 60 ticks, through a single rate-limited
  site.

  The distance is RWE's: where the original's `SetGoal` ladder keeps an old
  path whose tail already answers the new goal, RWE keeps one while the new
  goal is within `PathGoalRetargetTolerance`, **64 world units**, of the one
  the route was built for. That number is chosen to sit well outside the
  eight units an attack's stand-off point drifts by when it follows a target,
  and to be far short of the distances a new order is given over, so a new
  order still takes its straight-line stand-in on the tick it arrives.

  Measured on Crystal Maze seeds 7 and 8, tick for tick over the window both
  games reach -- the change ends a game at a different tick, so whole-run
  totals are not comparable -- searches per tick fall by a third to a half,
  expansions per tick by about a third, the first-pass walk steps by about a
  third, and `expansionsAbandoned` by 86-95%. The saving is in the throwaway
  searches and the stand-in walk. Since #272 counts the terrain-region
  early-out apart from exhausted searches, the expensive A\* `exhausted` tail
  is roughly flat, and a third to a half of the figure the issue read as that
  tail was the cheap early-out, which spends no expansions. The `path_bench`
  scenarios that do not repath (`spread`, `crowd`) keep identical pathfinder
  counters, though their hashes move because the new fields are hashed;
  `pressed-water` and `pressed-wall` do repath and show the reduced counts
  too. Recorded here because the tolerance and the queue in place of the
  original's round robin are RWE's, and because the change is
  replay-breaking.

- **A transport's capacity is read from the 1.0 key when the 3.1 one is
  missing, and a transport that names neither still carries.** The 3.1 exe
  reads `transportcapacity` and nothing else; `transportmaxunits`, the 1.0
  key, is not in its string table (`TOTALA-EXE-DATA.md` §30), and a
  transport with no `transportcapacity` parses as 0 and can never be ordered
  to load (`TOTALA-EXE-TRANSPORTS.md` §32). On a patched install that never
  arises, because `rev31.gp3` is searched before `totala1.hpi` and carries
  `transportcapacity=20` for the Hulk. Without the patch data, `totala1.hpi`'s
  `ARMTSHIP.FBI` says only `transportmaxunits=20`, and the faithful answer is
  a sea transport that cannot load. RWE reads the old key when the new one is
  absent, falls back to six for a ship and one for an aircraft when neither is
  there, and writes a warning at load naming the unit either way
  (`transportCapacityFromFbi`, `transportCapacityWarning`; #199). A file that
  carries both keys reads the new one, as the original does.

- **A unit script that faults loses the thread, not the game.** The original's
  COB interpreter divides without a guard, so `div` by zero faults the whole
  process, and it has no limit on how long a thread runs between yields
  (`TOTALA-EXE-EXTERNAL.md`, unverified here). RWE gives a division by zero
  the answer 0, and `INT_MIN / -1` the wrapped answer the hardware would. A
  thread that exceeds an argument count, call depth, stack depth or run length
  far past anything a shipped script reaches (`CobExecutionContext`'s limits)
  is killed, as the original kills a thread that meets an opcode it does not
  know, and so is one that faults in any other way. A synchronous query that
  faults gives the caller's fallback, the answer a unit without the script
  gets. None of this changes a script the original can run; it is here
  because a mod's script is untrusted input, and in a network game a fault
  ended every peer's game at once (#75).


## 91. Still unknown or unported

- **`unitsonly` and `groundbounce`** (§116). `unitsonly` is not parsed, so
  no RWE round skips the ground and sea tests. RWE's `groundbounce` zeroes
  `vy` and restores the previous height, where the original sets `vy` to
  `-(vy >> 2)` and leaves the position alone.
- **The campaign's win and lose rules** (§113) are decoded and not ported:
  all eighteen `[GlobalHeader]` conditions, checked once a second, all
  victory rules in order and any defeat rule, victory first, then a
  five-second countdown. Waiting on campaign piece 4 (B4 #38); the mission
  loader it builds on is B4 #293.
- **A mission unit's scripted orders** (§114) are decoded and not run: the
  InitialMission list's WAIT, WAITFORATTACK, ATTACKUTYPE, guard, transport
  start and MAKESELECTABLE, and the selectable bit that holds a scripted
  unit out of the player's hands and out of the computer AI's.
- **The campaign's screens and progression** (§115) are decoded and not
  ported: NEWGAME, the MSNBRIEF briefing, the glamour picture, ENDMSN's
  mission list with its won/lost/untried marks, progress in save games only,
  and `3.zrb`/`4.zrb` then `5.zrb` after winning a campaign's last mission.
- TA's **Permanent** LOS mode has not been looked at.
- **Circular** LOS mode (the `vismasks.gaf` stamp) is understood but not
  implemented; RWE always uses True.
- RWE's explored grid is **per-player** rather than the original's one shared
  bitmask with a bit per LOS group. Equivalent until allied vision groups exist.
- `hitDensity` (100 for solid things, 5–10 for foliage, 0 for smudges) is no
  longer parsed at all: **removed 2026-09-20** (#108), field and parse alike, so
  the key reaches nothing. It was once guessed to be the pass-through chance for
  projectiles hitting features; that guess is **refuted** — the string does not
  occur in `TotalA.exe` at all, and the collision test at `0x49B2B3` is purely
  geometric. This entry survived here after the refutation was written up and
  is corrected rather than deleted, since the guess is an inviting one to make
  twice.
- **Weapons damage features regardless of their kind.** RWE once switched
  feature damage off for render types 0, 5 and 7 — the laser and lightning
  draws — on the community belief that beams cannot hurt wreckage. Nothing in
  the binary gates damage on `rendertype` (a drawing attribute at `wdef+0x10C`)
  or on `beamweapon` (bit 3 of `wdef+0x111`, one reader, which maintains the
  draw's tail point), and the shipped naval corpses' `damage=24000` only makes
  sense as bought immunity from gunfire that would otherwise clear them. The
  exemption is gone. Collision was always geometric and is unchanged, so a beam
  still stops on a feature — it can now also break it.
- The **strafing pass** (`AirToGround`, `0x412710`) is decoded but not ported:
  RWE's fighters still fly the generic attack run. See the missions document §5.
- **`maneuverleashlength`** is now parsed but not enforced. In the original it
  aborts an attack when the aircraft strays that far from where it was standing
  when the order was given — missions document §8.
- ~~The interface colour table~~ Resolved: `0x4AC7D0` writes it from a
  different base; see §50. The superseded entry read:
- **(superseded) The interface colour table at `cfg+0xDCB` has no writer anywhere in
  `.text`.** Every one of the nine accesses is a read; it is a logical-colour to
  palette remap installed for the blitter. So the exact palette indices for the
  minimap rings (§25), the placement box (§27) and the sweep (§28) are unknown,
  and RWE uses greens of its own choosing. Settling it needs a runtime memory
  dump, not more static reading -- break on `0x466FA8` and read `edx`.
- ~~The anti-missile coverage ring is decoded but not drawn~~ **Drawn,
  2026-09-05** (commit b1f096f6). `renderMinimapCoverageRings`
  (`src/rwe/game/GameScene_render.cpp`) draws one ring per `interceptor`
  weapon, dashed while the launcher holds a round and solid while it is empty,
  both kinds clipped to the minimap. The `antiweapons`-not-parsed half of the
  old entry was a divergence and is recorded in §88. What RWE does with the
  decode is in TOTALA-EXE-INTERFACE.md, "What RWE does with all this".
- ~~Why a construction aircraft finishes a build one tick early~~ Resolved: two
  increments on the creation tick, because `VTOL_MobileBuild` discards its stance
  wait's answer and a pending COB event re-runs the lathe in the same tick; see
  §110, which also accounts for the 8 builds off the model (late by whole
  seconds). **Ported**: RWE credits a construction aircraft twice on the tick it
  first has a frame to lathe and never makes one wait for its stance, and the
  airborne cells are in the build fixture on the ordinary §88 delta.
- ~~The exact tick at which the original commits a bomb release inside its
  weapon code is still not pinned down; RWE uses its own bombsight.~~
  **Pinned and ported, 2026-09-24 (#110).** The weapon code has no bombsight:
  a bomb fires on the ordinary fire check the first weapon pass after
  `AirStrike` state 5 hands it the target, and keeps firing on every reload
  until state 6 clears it `attackrunlength` past the release point. The
  mission's trigger, `1 + attackrunlength + trunc(falltime * speed)`, is the
  whole of the aiming (`TOTALA-EXE-MISSIONS.md` §11). RWE's own bombsight and
  its stick of three are gone; `bombReleaseTrigger` and the run-length stick
  replace them.
- **`unit+0x110` bits 2–3.** They pick the loose 2000 default over the tight 150
  when a weapon names no tolerance (§11), and are tested at only three places —
  `0x40458A`, `0x4057D9` and `0x49D899` — none of which says what they mean. RWE
  keeps its own 256 default rather than guess, which is recorded as a divergence
  in §88.
- **`holdtime` has no known reader** — see §11. `aimrate` is not a key the
  original recognises at all, so there is nothing there to find.
- **`DefaultMissionType`** is parsed and acted on for `Standby_Mine`, which
  is how Core Contingency's mines go off, cadence included (#108,
  `TOTALA-EXE-WEAPONS.md` §9). The other three names the data uses are what
  RWE's idle behaviour already is for the units that name them, bar one:
  `Standby` for an idle mobile unit, where RWE lets its ordinary idle
  targeting stand in, so an idle tank shoots what comes into range but does
  not walk off after what it merely sees. The sight-range search `0x43B700`
  behind it is ported, as `findEnemyToEngage` (§86), and used by patrols,
  idle aircraft and now the mines. Any other mission a mod names is warned
  about at load and idles as if it named nothing.
- **The movement mode is acted on for a break-off, and nowhere else.** A unit
  that leaves its post now carries the leash `0x43B1F0` gives it —
  `maneuverleashlength` from the spot where it saw the target — and Maneuver
  walks back to that spot afterwards, so Maneuver and Roam are no longer the
  same thing. **2026-09-24 (#109):** every reader of the mode in the binary
  is now listed in §9 of the weapons document. The two that decide behaviour
  are `0x43B1F0` (sighting to attack: Hold Position refuses, Maneuver comes
  home, Roam goes) and `0x43B400` (the repair patrols' job issuer: Hold
  Position and Maneuver come home); RWE's patrol break-off and idle-aircraft
  sighting follow the first and read the unit's own mode rather than the
  definition's. Left: an idle ground unit does not chase a sighting at all
  in RWE, where the original's Standby does through `0x43B1F0`, and the
  repair patrols do not plant `0x43B400`'s return move.
- ~~Smoke does not drift downwind~~ **Ported, 2026-09-24** (#111).
  `GameSimulation::currentWindVector` was already hashed and pushing ballistic
  rounds and bombs off course (commit 72f8b402, the shape of `0x49BD10`); the
  smoke now rides the same vector at eight times that drift a tick, which is
  the `x += windX × 8`, `z += windZ × 8` of the puff stepper at `0x475340`
  and the vent's at `0x475620` (§4, §23). `Particle::driftsWithWind` opts a
  puff in; wake dots stay out, since `0x474580` has no wind term, and so does
  anything flying under its own velocity. The lift is unchanged and still
  right for the 112 that nearly every map uses: RWE's half a unit a tick is
  the original's gravity × 4 there, though it will not track a map that sets
  gravity to something else.
- ~~The **explosion smoke** and the **30-second burning wreck plume** are
  decoded but not ported~~ **Ported, 2026-09-24** (#113). Both are one
  `SmokeEmitter` on the game scene, the shape of `0x472630`: an impact that
  smokes gets three puffs seven ticks apart (`0x420AE1`, interval 7, lifetime
  15), and a wreck placed on ground above sea level gets a puff every fifteen
  ticks for nine hundred (`0x48644B`). The sim says which wrecks may burn
  through `WreckSpawnedEvent::mayBurn`, the original's flag: cleared by the
  wet branch and by death cause 7, so a sea wreck and an `isfeature` unit's
  never smoke. RWE's own: each puff is gated on the fog like the vents' steam,
  and the original's quirk of smoking over a refused placement is kept only in
  that the event fires whether or not the wreck was placed.
- ~~**`BadSlope` and `BadWaterSlope` are not parsed.**~~ **Ported, 2026-09-24**
  (#112). §95 decodes them: the movement class's *free* slope threshold, with
  `MaxSlope` / `MaxWaterSlope` above them admitting the cell as "tight" at an
  extra 30 of path cost, each defaulting to half the corresponding max and
  clamped to it. The movement class parser reads both with those defaults
  and clamps, an FBI-built class gets the halves, and the pathfinder's rough
  test reads them, picking the dry or the wet threshold per cell by whether
  the cell's lowest corner is at or above sea level, as `0x47E145` does. Only
  the two hover classes name them in the shipped data, `TANKHOVER3` and
  `TANKHOVER4` with `BadSlope=12` equal to their `MaxSlope`, so a hovercraft
  now pays nothing for ground a tank calls rough. Path cost only: nothing
  about what is passable changed, and `path_bench`'s three scenarios agree
  hash for hash before and after (the bench's own class is 255 everywhere).
- **The work sounds are played. Ported, 2026-09-10.** Sound slot 11,
  `working` (`reclaim1` in every construction unit's category), is played once
  when reclaim or capture work starts, and slot 16 `capture` when a capture
  finishes -- S:97. Both now have a simulation event to hang off: a new
  `UnitStartedReclaimingEvent` raised on the first tick of actual work by
  feature reclaim, unit reclaim and capture alike, and the `UnitCapturedEvent`
  that already existed, which now carries the captor so slot 16 is the
  captor's sound. Slot 16 stays silent on the shipped data, no category
  setting it.
- **`Resurrect` is decoded and not implemented** -- S:98. No shipped FBI sets
  `canresurrect`, so it would be a mod-only capability, and a new `UnitOrder`
  alternative cannot be added from inside `src/rwe/sim` alone.
- **`beamweapon`'s tail point** (`0x49BBA3`) is decoded and deliberately *not*
  ported — §92. It gives a round a second, trailing point that starts moving
  `duration + 1` ticks after the shot, and it is drawn only by `rendertype 0`.
  No shipped weapon both sets the bit and meets those conditions: the two
  disintegrators are `rendertype=3` and declare no `duration`, so for them the
  original computes the tail and throws it away. Anything that wanted it would
  have to be a mod.
- **An aim script that answers no is asked again on the next tick.** The
  original leaves bit 0 up with the answer still zero (`0x49D580` returns at
  `0x49D86D`), so it starts no other aim until something lowers the bit — §11,
  *One aim per shot*. It matters in the shipped data: ARMCOM's `AimPrimary`
  returns 0 while its static 3 is set. What lowers the bit when a unit changes
  target has not been read, and asking again cannot strand a gun the original
  would have freed, so the wait is not ported.
- **Self-destruct's tail and its corpse.** The original blasts a
  self-destructing unit `150 + rand(15)` ticks after the order (`0x402117`),
  with thirty thousand points of ordinary damage from the unit to itself
  (`0x402147`, cause 3), so the death runs the `Killed` ladder and leaves the
  corpse it picks. RWE blasts at exactly 150 ticks and removes the unit with
  no corpse (`GameSimulation::selfDestructUnit`). Both are hashed behaviour;
  decoded under issue #29, not ported there because that issue was the
  presentation.

