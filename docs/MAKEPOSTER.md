# `+MakePoster`: the large world capture

Implementation note for #915. The reference finding is
`BT-GAME-CONSOLE-COMMANDS-PARITY.md` section 6, a **static reading** of the
systems manual at revision `97d7ac59b460417bcdfa629cf90da99f31818c65`. Nothing
in that chapter was executed, so this is a documented comparison rather than
an executed-original check. No decompilation was opened to write this
implementation.

## Interface

```
+MakePoster                     3200 by 2400, then clamped
+MakePoster all                 the map's own pixel size
+MakePoster <width> <height>    an explicit size, then clamped
```

`MakePoster` is in the original's cheat table (mask 2). RWE's existing mask
gating applies: a skirmish has the cheat bit, a campaign does not, and a
multiplayer line uses the host's Cheat Codes setting as
`chatCommandMask` describes. A caller without the bit is silent, like `+ATM`.
A malformed form prints
`Usage: +MakePoster [all | <width> <height>]`.

The two-number form is the natural reading of the finding's "each dimension",
but the prose does not spell out the grammar. Dimensions are read strictly --
decimal digits, positive, no sign or trailing text -- rather than with the
interpreter's `atoi`. A typo is refused rather than silently truncated. Both
choices are RWE-original details where the finding is silent; they are not
claims of original parity.

## Sizing and placement

The reference rules, restated:

- no argument means 3200 by 2400;
- `all` means the map's pixel size;
- each side is raised to at least the visible tile span times sixteen and
  lowered to the map's own size;
- the rectangle is centred on the view and clamped so it stays on the map;
- the screenshots directory is created;
- a `BIGSHOT` bitmap is written;
- the pacing timestamp is reset afterwards.

RWE's implementation of each:

| Rule | RWE |
| --- | --- |
| default size | 3200x2400 |
| `all` | map width and height in world units |
| lower bound | the view's world span, rounded up to whole 16-unit tiles |
| upper bound | the map's pixel size, per side |
| position | top-left = view centre - half the size, clamped to the map; a rectangle as large as the map starts at the map's corner |
| scale | one output pixel is one map pixel / world unit |

**The capture scale is RWE-original where the finding is silent.** The map's
"pixel size" is a world dimension in TA, and the finding describes the poster
in those terms. RWE therefore renders the poster at 1:1 with the world and does
not fold the in-game camera zoom into the output size. The camera position is
what is honoured; the zoom is not. This is recorded as a divergence in
`TOTALA-EXE.md` section 88 because the original's poster scale is not decoded.

## Bounds and allocation

A poster is streamed, not held in memory as one image.

- Each tile is rendered into a 512x512 off-screen framebuffer (or the poster's
  larger side when it is smaller), read back with `glReadPixels`, and copied
  into one horizontal strip.
- The PCX is written a row at a time. Peak memory is therefore
  `poster width x 512 x 3` bytes for the strip plus the tile buffer, not
  `width x height x 3`.
- A hard limit of `16384` a side and `16384 x 16384` pixels refuses a request
  that would otherwise run for an unreasonable time or produce an enormous
  file. This matters for `all` on a 63x63-tile map, whose 32256-unit side is
  over a billion pixels. The refusal names the limit.
- The PCX format's own 16-bit window is comfortably inside that limit.

## Output

`screenshots/BIGSHOT.pcx` under the local data directory (`%APPDATA%\RWE` on
Windows, the XDG data directory elsewhere), created if missing.

- **Name**: the reference calls it "a `BIGSHOT` bitmap". The stem is kept.
- **Format**: RWE writes its own 24-bit, three-plane PCX, the same writer rules
  as `SHOTnnnn.pcx`; the original's 8-bit screen and 256-colour BMP writer are
  not present. This is the same documented divergence as the ordinary
  screenshot (`TOTALA-EXE.md` section 88, `BT-GRAPHICS-PCX-PARITY.md` rule 4).
- **One file**: the fixed name is overwritten by a later poster. The finding
  names one file and gives no numbering rule; RWE does not invent one.

## What the capture does not touch

The capture is presentation only.

- It renders through `GameScene::renderWorld` into its own framebuffer and
  restores the camera, the world viewport, the world render textures and the
  GL state it changed.
- It skips the cursor-driven world UI: selection plates, order lines, health
  bars, the build ghost and the bandbox. The poster is the world, not the
  local player's interface.
- It reads the simulation; it never queues a command, writes hashed state or
  draws from the simulation's RNG.
- When it finishes it asks `SceneManager` to move the frame-pacing origin to
  the moment the capture ended, so the next update does not see the capture's
  delay as elapsed game time and try to catch up. The simulation still runs
  the same ticks with the same inputs; only the wall-clock frame boundary
  moves.

## Verification

- `src/rwe/scene/PosterCapture.test.cpp` pins the minimum-size rounding, the
  clamp to the map, the centring/clamping arithmetic, the hard limits, the
  filename, the streaming PCX bytes against `encodePcx24`, and the file-error
  path.
- `src/rwe/game/chat_command.test.cpp` pins the accepted and refused command
  forms.
- The `915-makeposter-writes-a-poster` scenario runs the real `GameScene`,
  sends `+makeposter 3200 2400` through the real chat bar, and checks that a
  version-5, three-plane PCX with a real image size was written through the
  actual off-screen render path. It was run on this machine against
  `D:/RWE-Data`: on Coast To Coast the file was 3200x2016 (the requested
  height lowered to the map height), and on Comet Catcher it was the full
  3200x2400, both at `%APPDATA%\RWE\screenshots\BIGSHOT.pcx`.
- `+makeposter all` was exercised through the same real scene on Comet
  Catcher (`RWE_CHAT_TEST=2:+makeposter all` while running an existing
  scenario): the poster was 6144x7680, the map's own pixel size, assembled
  from 12x15 tiles. A full-resolution crop across a tile boundary showed no
  seam.
- **Not done**: a visual comparison with a poster produced by the original,
  and a manual inspection of a fully explored, densely populated large map for
  tile seams. The scenario proves the path runs and writes a valid image; it
  does not prove pixel-for-pixel parity, which the finding does not provide a
  fixture for.
