#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rwe
{
    /** SOUNDS.GUI's Sound Mode: Off | Mono | 3D. */
    enum class SoundMode
    {
        Off = 0,
        Mono = 1,
        Stereo = 2,
    };

    /** SOUNDS.GUI's Unit Sounds: Off | Medium | Full. */
    enum class UnitSpeechLevel
    {
        Off = 0,
        Medium = 1,
        Full = 2,
    };

    /**
     * MUSICRT's TRACKMODE: Play All | Random | Repeat | Custom (TOTALA-EXE.md
     * S:68). The original keeps it 1-4 at [game+0x37f16]; RWE keeps 0-3 in
     * the same order. Custom is the default, and the only mode in which the
     * situational Building/Battle music chooses the track.
     */
    enum class MusicTrackMode
    {
        PlayAll = 0,
        Random = 1,
        Repeat = 2,
        Custom = 3,
    };

    /**
     * VISUALRT's Shading switch, widened from the original's Off|On.
     *
     * The original has two states because it has two whole rasterizer chains
     * and picks between them on one bit (0x458744). Its On is this switch's
     * BuildingsOnly: the cache renderer sends only a building or a feature to
     * the shaded chain (0x45873C) and caches every mobile unit unshaded
     * whatever the option says (TOTALA-EXE-SHADING.md S:11). So BuildingsOnly
     * is the default, and the two stages that shade units are RWE's own,
     * recorded as a divergence in TOTALA-EXE.md S:88.
     */
    enum class ShadingMode
    {
        Off = 0,
        UnitsOnly = 1,
        BuildingsOnly = 2,
        Both = 3,
    };

    /** All three buttons cycle through their stages and wrap, as the original's do. */
    SoundMode nextStage(SoundMode mode);
    UnitSpeechLevel nextStage(UnitSpeechLevel level);
    MusicTrackMode nextStage(MusicTrackMode mode);
    ShadingMode nextStage(ShadingMode mode);

    /** The label the Shading button shows for each stage. */
    const char* shadingModeDisplayName(ShadingMode mode);

    /** All four labels in stage order, for building the button. */
    std::vector<std::string> shadingModeLabels();

    /** True if models of that kind are shaded under this mode. */
    bool shadingModeCoversUnits(ShadingMode mode);
    bool shadingModeCoversBuildings(ShadingMode mode);

    class GlobalConfig
    {
    public:
        bool leftClickInterfaceMode{true};

        /** bordered, borderless or fullscreen; changing it takes effect on restart. */
        std::string windowMode{"bordered"};

        /** 0 to 100, as the options screen and rwe.cfg deal in. */
        unsigned int soundVolume{100};
        unsigned int musicVolume{100};
        bool musicEnabled{true};

        /** Unit shadows on or off -- the VISUALS page's Shadows toggle. */
        bool shadows{true};

        /**
         * Shadows for mobile units specifically, under the master above.
         *
         * The original keeps three shadow bits in its display-options word,
         * not one: bit 2 Shadows, bit 3 VehicleShadows, bit 4 FeatureShadows
         * (S:76's registry table), and the vehicle bit gates only the vehicle
         * pass, so buildings go on casting with it off. It is deliberately not
         * on the options screen: VISUALRT has exactly one shadow gadget,
         * BSHADOWS, which RWE already wires to the master, and the GUI files
         * are read-only game data -- so adding a second button would make the
         * panel less like TA's, not more. It is an rwe.cfg key,
         * vehicle-shadows, like the shading strengths above.
         */
        bool vehicleShadows{true};

        /**
         * How many screen pixels one game pixel is drawn as, 1 to 4. The
         * original ran at 640x480 and, stretched across a modern display,
         * shows each of its pixels about two screen pixels wide; RWE draws
         * one to one, so its one-pixel wireframe and selection box read as
         * thinner than TA's. At 2 the whole frame -- world, interface and
         * cursor -- is rendered at half the window size and blown up with
         * nearest-neighbour sampling, and mouse input is mapped back
         * through the same factor. The world's own 2x supersample and the
         * building halo filter happen inside that frame, so they look the
         * same as at 1, only larger. An rwe.cfg key, screen-scale.
         */
        unsigned int screenScale{1};

        /** Screen scroll speed percentage, 25 to 200; 100 is the old fixed rate. */
        unsigned int scrollSpeed{100};

        /** 0 off, 1 mono, 2 stereo. */
        unsigned int soundMode{2};

        /** Unit voice acknowledgements: 0 off, 1 medium, 2 full. */
        unsigned int unitSpeech{2};

        /** MUSICRT's track mode: 0 Play All, 1 Random, 2 Repeat, 3 Custom. */
        unsigned int musicTrackMode{3};

        /** Screen gamma percentage, 50 to 133 (the original's own range); 100 is untouched. */
        unsigned int gamma{100};

        /**
         * Model lighting, the VISUALS page's Shading switch: 0 off, 1 units
         * only, 2 buildings only, 3 both. 2 is the original's On.
         */
        unsigned int shadingMode{2};

        /**
         * How much of the measured PALETTE.SHD ramp each kind of model gets,
         * as a percentage. 100 is the original exactly -- its darkest row is
         * a genuine black -- and lower values keep the same curve with its
         * contrast pulled in around the unshaded colour.
         *
         * **These are play-tested values, not placeholders. Do not raise them
         * to 100 because the arithmetic says 100 is faithful.** That was tried
         * on 2026-09-08, on the strength of an earlier version of this very
         * comment claiming both defaulted to 100, and it was rejected on
         * sight: at full strength the table's snap to the nearest palette
         * entry stops being a subtlety and reads as banding, and row 0's true
         * black lands on an already dark texture. 25 and 40 were chosen by
         * measuring how much of a solar collector went black at full strength
         * -- twice the share of pixels under luminance 16, 18.4% against 8.8%
         * unshaded -- and they are what looks like the original on a screen
         * far larger than the one it was drawn for.
         *
         * Units started at 25 and were raised to 40 on 2026-09-11, when a
         * play-test found shading had no visible effect on a commander or a
         * tank at 25. Nothing was wrong with the pipeline: none of ARMCOM,
         * CORCOM, ARMPW or ARMSTUMP's scripts has a CACHE, DONT_CACHE or SHADE
         * opcode, so every piece of them is shaded. A quarter of the ramp was
         * simply too faint to read on a moving model.
         *
         * These are deliberately not on the options screen: VISUALRT has no
         * gadget for them and the GUI files are read-only game data. They
         * are rwe.cfg keys (shading-strength-units,
         * shading-strength-buildings) for anyone who does want it softer.
         * See TOTALA-EXE.md S:88.
         */
        unsigned int shadingStrengthUnits{40};
        unsigned int shadingStrengthBuildings{40};

        /** Edge anti-aliasing: the original supersamples the unit and box-filters it down. */
        bool antiAlias{true};

        /**
         * The purple building fringe, on by default. VISUALRT has no gadget
         * for it -- the GUI files are read-only game data -- so RWE builds one
         * in code and puts it on the VISUALS page under the shadows toggle,
         * the way the Shading switch is rebuilt there with four stages. It is
         * separate from anti-alias deliberately: the artefact needs the
         * supersample to exist, so turning anti-aliasing off takes the fringe
         * with it, but plenty of people will want the crisp edges and not the
         * bug. rwe.cfg key building-halo.
         */
        bool buildingHalo{true};

        /**
         * Whether the box filter reaches past the buildings, on by default.
         *
         * RWE renders the world into a buffer twice the size and filters each
         * 2x2 block down, which is how it reproduces the original's
         * anti-aliasing. The original only ever did that to a building's
         * cached bitmap: never the ground, never a mobile unit, because
         * nothing else had a cached bitmap for it to be done to. So the
         * filter is restricted the same way, and off is the faithful setting.
         *
         * On extends it back over units and everything else solid, for anyone
         * who would rather have smooth edges on them than the edges the
         * original drew. It is a preference, not a fidelity fix -- and the
         * preference, play-tested 2026-09-11, is on: sharp units beside
         * smoothed buildings looked like a fault, not like 1997. Off is still
         * one click away on the VISUALS page. rwe.cfg key anti-alias-units.
         */
        bool antiAliasUnits{true};

        /**
         * The purple halo on building edges, reproduced by running the
         * original's own arithmetic.
         *
         * It is a bug of the original's, and its author says so: the table he
         * box-filtered a building's double-size buffer down through "broke
         * when dealing with the edge and transparency". RWE does not paint an
         * approximation of the result on; it renders the buildings' PALETTE
         * INDICES into a mask at the supersampled size and filters that down
         * through the shipped palettes/PALETTE.ALP with three chained lookups,
         * which is what the original did and in the order it did it. The wrong
         * colour then falls out of the table rather than being chosen. See
         * worldPost.frag, AlphaTable.h and TOTALA-EXE.md S:101.
         *
         * There is no width setting, and that absence is the point. The
         * artefact is one output pixel wide because it is a 2x2 downsample,
         * exactly as it was in 1997; widening it would mean inventing pixels
         * the original never drew. What is left is a strength: 100 is the
         * original, where the filtered pixel simply IS the pixel, and lower
         * values blend it back towards RWE's own rendering for anyone who
         * finds it too strong. 0 leaves it out.
         *
         * **Two warnings, both paid for.** The first: this follows the
         * anti-alias setting, because with no supersampling there is no 2x
         * buffer and so no downsample for the halo to have come out of --
         * which is why the original's went away with its anti-aliasing off
         * too. The second, and the expensive one: the halo was twice reported
         * invisible and twice the width was blamed, on the entirely reasonable
         * ground that one pixel of a 640x480 screen is a much larger share of
         * a building than one pixel here. That was wrong both times. It was
         * not being drawn at all, because the mask pass ran under GL_LESS over
         * geometry already at that exact depth. An invisible effect is a
         * broken effect until proven otherwise, and turning the number up is
         * not how you find out which.
         *
         * rwe.cfg key building-halo-strength.
         */
        unsigned int buildingHaloStrength{100};

        /**
         * Two corrections applied on top of what the table returns, and they
         * are RWE's rather than the original's.
         *
         * They exist because a play-test of the exact colours asked for
         * something less saturated and redder, and that is a fair thing to
         * ask. A palette index is not a colour until something displays it,
         * and TA's were displayed on a 1997 CRT through a hardware LUT --
         * phosphor, a warmer white point and the gamma of that path all pull a
         * saturated blue-purple towards a duller red-magenta, and none of it
         * is in the data. The arithmetic upstream stays exact; the correction
         * lives in one place and is switched off by setting these to 100 and
         * 0, which is what to do to see what PALETTE.ALP actually says.
         *
         * Saturation is a percentage of the table's own, pulling each pixel
         * towards its own luminance so the hue does not move. Red shift pulls
         * blue down towards green, rotating the hue from purple round towards
         * red-magenta; at 100 blue meets green and nothing magenta is left, so
         * the middle of the range is the useful part. It never raises blue, so
         * a blend that already leans green -- the plain grey a green edge
         * returns -- comes through untouched.
         *
         * rwe.cfg keys building-halo-saturation and building-halo-red-shift.
         */
        unsigned int buildingHaloSaturation{65};
        unsigned int buildingHaloRedShift{50};
    };

    /**
     * What an options screen has staged but not yet saved.
     *
     * There are two such screens -- the front end's and the in-game one behind
     * F2 -- and they edit the same eleven settings into the same rwe.cfg, so
     * they stage, undo and write them through the same type. The defaults here
     * are the ones the Default button restores.
     */
    struct GameOptions
    {
        unsigned int soundVolume{100};
        unsigned int musicVolume{100};
        bool musicEnabled{true};
        std::string windowMode{"windowed"};
        bool shadows{true};
        unsigned int scrollSpeed{100};
        SoundMode soundMode{SoundMode::Stereo};
        UnitSpeechLevel unitSpeech{UnitSpeechLevel::Full};
        MusicTrackMode musicTrackMode{MusicTrackMode::Custom};
        unsigned int gamma{100};
        ShadingMode shading{ShadingMode::BuildingsOnly};
        bool antiAlias{true};
        bool buildingHalo{true};
        bool antiAliasUnits{true};
    };

    /** The settings as the config file last left them. */
    GameOptions optionsFromConfig(const GlobalConfig& config);

    /** The label the window mode button shows for a config value. */
    const char* windowModeDisplayName(const std::string& mode);

    /** Writes the eleven settings to rwe.cfg, leaving every other key in it alone. */
    void writeGameOptions(const std::filesystem::path& configPath, const GameOptions& options);
}
