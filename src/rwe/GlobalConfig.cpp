#include "GlobalConfig.h"

#include <rwe/util.h>

namespace rwe
{
    SoundMode nextStage(SoundMode mode)
    {
        switch (mode)
        {
            case SoundMode::Off:
                return SoundMode::Mono;
            case SoundMode::Mono:
                return SoundMode::Stereo;
            default:
                return SoundMode::Off;
        }
    }

    UnitSpeechLevel nextStage(UnitSpeechLevel level)
    {
        switch (level)
        {
            case UnitSpeechLevel::Off:
                return UnitSpeechLevel::Medium;
            case UnitSpeechLevel::Medium:
                return UnitSpeechLevel::Full;
            default:
                return UnitSpeechLevel::Off;
        }
    }

    ShadingMode nextStage(ShadingMode mode)
    {
        switch (mode)
        {
            case ShadingMode::Off:
                return ShadingMode::UnitsOnly;
            case ShadingMode::UnitsOnly:
                return ShadingMode::BuildingsOnly;
            case ShadingMode::BuildingsOnly:
                return ShadingMode::Both;
            default:
                return ShadingMode::Off;
        }
    }

    const char* shadingModeDisplayName(ShadingMode mode)
    {
        switch (mode)
        {
            case ShadingMode::Off:
                return "Off";
            case ShadingMode::UnitsOnly:
                return "Units";
            case ShadingMode::BuildingsOnly:
                return "Buildings";
            default:
                return "Both";
        }
    }

    std::vector<std::string> shadingModeLabels()
    {
        return {
            shadingModeDisplayName(ShadingMode::Off),
            shadingModeDisplayName(ShadingMode::UnitsOnly),
            shadingModeDisplayName(ShadingMode::BuildingsOnly),
            shadingModeDisplayName(ShadingMode::Both)};
    }

    bool shadingModeCoversUnits(ShadingMode mode)
    {
        return mode == ShadingMode::UnitsOnly || mode == ShadingMode::Both;
    }

    bool shadingModeCoversBuildings(ShadingMode mode)
    {
        return mode == ShadingMode::BuildingsOnly || mode == ShadingMode::Both;
    }

    GameOptions optionsFromConfig(const GlobalConfig& config)
    {
        GameOptions options;
        options.soundVolume = config.soundVolume;
        options.musicVolume = config.musicVolume;
        options.musicEnabled = config.musicEnabled;
        options.windowMode = config.windowMode;
        options.shadows = config.shadows;
        options.scrollSpeed = config.scrollSpeed;
        options.soundMode = static_cast<SoundMode>(config.soundMode);
        options.unitSpeech = static_cast<UnitSpeechLevel>(config.unitSpeech);
        options.gamma = config.gamma;
        options.shading = static_cast<ShadingMode>(config.shadingMode);
        options.antiAlias = config.antiAlias;
        return options;
    }

    const char* windowModeDisplayName(const std::string& mode)
    {
        if (mode == "borderless")
        {
            return "Borderless";
        }
        if (mode == "fullscreen")
        {
            return "Fullscreen";
        }
        return "Windowed";
    }

    void writeGameOptions(const std::filesystem::path& configPath, const GameOptions& options)
    {
        updateConfigFile(configPath, {
                                         {"sound-volume", std::to_string(options.soundVolume)},
                                         {"music-volume", std::to_string(options.musicVolume)},
                                         {"music", options.musicEnabled ? "true" : "false"},
                                         {"window-mode", options.windowMode},
                                         {"shadows", options.shadows ? "true" : "false"},
                                         {"scroll-speed", std::to_string(options.scrollSpeed)},
                                         {"sound-mode", std::to_string(static_cast<unsigned int>(options.soundMode))},
                                         {"unit-speech", std::to_string(static_cast<unsigned int>(options.unitSpeech))},
                                         {"gamma", std::to_string(options.gamma)},
                                         {"shading-mode", std::to_string(static_cast<unsigned int>(options.shading))},
                                         {"anti-alias", options.antiAlias ? "true" : "false"},
                                     });
    }
}
