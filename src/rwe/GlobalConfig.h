#pragma once

#include <filesystem>
#include <string>

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

    /** Both buttons cycle through their three stages and wrap, as the original's do. */
    SoundMode nextStage(SoundMode mode);
    UnitSpeechLevel nextStage(UnitSpeechLevel level);

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

        /** Model lighting, the VISUALS page's Shading switch; on in the original. */
        bool shading{true};

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
        bool shading{true};
        bool antiAlias{true};
    };

    /** The settings as the config file last left them. */
    GameOptions optionsFromConfig(const GlobalConfig& config);

    /** The label the window mode button shows for a config value. */
    const char* windowModeDisplayName(const std::string& mode);

    /** Writes the eleven settings to rwe.cfg, leaving every other key in it alone. */
    void writeGameOptions(const std::filesystem::path& configPath, const GameOptions& options);
}
