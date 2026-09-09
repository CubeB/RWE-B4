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
     * VISUALRT's Shading switch, widened from the original's Off|On.
     *
     * The original has two states because it has two whole rasterizer chains
     * and picks between them on one bit (0x458744); it draws no distinction
     * between a building and a mobile unit anywhere in the shaded path --
     * 0x459C70 never reads a "is a building" flag (TOTALA-EXE-SHADING.md
     * S:11, NOT FOUND). Splitting the switch by category is therefore a
     * deliberate divergence, recorded in TOTALA-EXE.md S:88, and it exists
     * because the two look different enough on screen to want separate
     * control: a building is a big slab that carries the banding well, and a
     * unit is small and moving and carries it badly.
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

        /** Screen scroll speed percentage, 25 to 200; 100 is the old fixed rate. */
        unsigned int scrollSpeed{100};

        /** 0 off, 1 mono, 2 stereo. */
        unsigned int soundMode{2};

        /** Unit voice acknowledgements: 0 off, 1 medium, 2 full. */
        unsigned int unitSpeech{2};

        /** Screen gamma percentage, 50 to 133 (the original's own range); 100 is untouched. */
        unsigned int gamma{100};

        /**
         * Model lighting, the VISUALS page's Shading switch: 0 off, 1 units
         * only, 2 buildings only, 3 both. On for everything in the original.
         */
        unsigned int shadingMode{3};

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
         * These are deliberately not on the options screen: VISUALRT has no
         * gadget for them and the GUI files are read-only game data. They
         * are rwe.cfg keys (shading-strength-units,
         * shading-strength-buildings) for anyone who does want it softer.
         * See TOTALA-EXE.md S:88.
         */
        unsigned int shadingStrengthUnits{25};
        unsigned int shadingStrengthBuildings{40};

        /** Edge anti-aliasing: the original supersamples the unit and box-filters it down. */
        bool antiAlias{true};

        /**
         * The purple halo on building edges, reproduced deliberately.
         *
         * It is a bug of the original's, and its author says so: the table he
         * box-filtered a building's double-size buffer down through "broke
         * when dealing with the edge and transparency". What stood for
         * transparent measures out of the shipped PALETTE.ALP as index 253,
         * plain magenta, and the colour is that row's mean. The width is in
         * output pixels and the strength is a percentage, because the artefact
         * does not survive translation on its own -- the original's is about
         * one pixel of a 640x480 screen, and one pixel of a modern display is a
         * far smaller share of a building. 0 strength leaves it out.
         *
         * **The width defaults to 3, not to the faithful 1.** At 1 it was
         * play-tested on 2026-09-09 and could not be seen at all, which is the
         * whole difficulty with this artefact: the faithful setting is
         * invisible and the visible setting is wider than the original's. 3 is
         * the width at which it reads as the halo people remember rather than
         * as a stray pixel; set it to 1 for the arithmetically faithful one and
         * expect to have to look for it.
         *
         * rwe.cfg keys building-halo-strength and building-halo-width; it
         * follows the anti-alias setting, since with no supersampling there is
         * no downsample for it to have come from. See worldPost.frag and
         * TOTALA-EXE.md S:101.
         */
        unsigned int buildingHaloStrength{55};
        unsigned int buildingHaloWidth{3};
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
        unsigned int gamma{100};
        ShadingMode shading{ShadingMode::Both};
        bool antiAlias{true};
    };

    /** The settings as the config file last left them. */
    GameOptions optionsFromConfig(const GlobalConfig& config);

    /** The label the window mode button shows for a config value. */
    const char* windowModeDisplayName(const std::string& mode);

    /** Writes the eleven settings to rwe.cfg, leaving every other key in it alone. */
    void writeGameOptions(const std::filesystem::path& configPath, const GameOptions& options);
}
