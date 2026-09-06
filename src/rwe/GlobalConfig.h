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
         * These are deliberately not on the options screen: VISUALRT has no
         * gadget for them and the GUI files are read-only game data. They are
         * rwe.cfg keys for anyone who wants to tune the look.
         *
         * The defaults were picked by measurement, not by eye. Shading a
         * solar collector on Coast To Coast at 100 doubles the share of its
         * pixels darker than luminance 16, from 8.8% unshaded to 18.4%, while
         * leaving the mean untouched -- that is the "too much shadow" the
         * change was made to answer, and it comes from row 0 being a true
         * black landing on an already dark texture. At 40 the excess is
         * roughly halved, to 12.4%, and the model still reads with its lit
         * side clearly lighter than its shaded side. Units sit lower again
         * because their banding moves as they turn. See TOTALA-EXE.md S:88.
         */
        unsigned int shadingStrengthUnits{25};
        unsigned int shadingStrengthBuildings{40};

        /** Edge anti-aliasing: the original supersamples the unit and box-filters it down. */
        bool antiAlias{true};
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
