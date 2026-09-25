#include "GlobalConfig.h"
#include <cctype>
#include <algorithm>

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

    MusicTrackMode nextStage(MusicTrackMode mode)
    {
        switch (mode)
        {
            case MusicTrackMode::PlayAll:
                return MusicTrackMode::Random;
            case MusicTrackMode::Random:
                return MusicTrackMode::Repeat;
            case MusicTrackMode::Repeat:
                return MusicTrackMode::Custom;
            default:
                return MusicTrackMode::PlayAll;
        }
    }

    MusicTrackType nextStage(MusicTrackType type)
    {
        switch (type)
        {
            case MusicTrackType::Building:
                return MusicTrackType::Battle;
            case MusicTrackType::Battle:
                return MusicTrackType::Victory;
            case MusicTrackType::Victory:
                return MusicTrackType::Defeat;
            case MusicTrackType::Defeat:
                return MusicTrackType::Unused;
            default:
                return MusicTrackType::Building;
        }
    }

    std::vector<unsigned int> parseMusicTrackTypes(const std::string& value)
    {
        std::vector<unsigned int> types;
        std::string field;
        auto flush = [&]() {
            if (!field.empty())
            {
                try
                {
                    types.push_back(std::min(4u, static_cast<unsigned int>(std::stoul(field))));
                }
                catch (const std::exception&)
                {
                    // A field that is not a number reads as the default type,
                    // so the list keeps its alignment with the album.
                    types.push_back(0u);
                }
                field.clear();
            }
        };
        for (auto c : value)
        {
            if (c == ',')
            {
                flush();
            }
            else if (!std::isspace(static_cast<unsigned char>(c)))
            {
                field.push_back(c);
            }
        }
        flush();
        return types;
    }

    std::string formatMusicTrackTypes(const std::vector<unsigned int>& types)
    {
        std::string out;
        for (std::size_t i = 0; i < types.size(); ++i)
        {
            if (i > 0)
            {
                out.push_back(',');
            }
            out += std::to_string(types[i]);
        }
        return out;
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

    std::vector<unsigned int> cameraZoomStages()
    {
        return {50u, 100u, 150u, 200u};
    }

    std::vector<std::string> cameraZoomLabels()
    {
        std::vector<std::string> labels;
        for (auto percent : cameraZoomStages())
        {
            labels.push_back("Zoom " + std::to_string(percent) + "%");
        }
        return labels;
    }

    unsigned int nextCameraZoom(unsigned int percent)
    {
        auto stages = cameraZoomStages();
        auto index = cameraZoomStageIndex(percent);
        return stages[(index + 1) % stages.size()];
    }

    unsigned int cameraZoomStageIndex(unsigned int percent)
    {
        auto stages = cameraZoomStages();
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            if (stages[i] == percent)
            {
                return static_cast<unsigned int>(i);
            }
        }
        // 100 is the default and the stage an unrecognised value reads as.
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            if (stages[i] == 100u)
            {
                return static_cast<unsigned int>(i);
            }
        }
        return 0u;
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
        options.musicTrackMode = static_cast<MusicTrackMode>(config.musicTrackMode);
        options.musicTrackTypes = config.musicTrackTypes;
        options.gamma = config.gamma;
        options.shading = static_cast<ShadingMode>(config.shadingMode);
        options.antiAlias = config.antiAlias;
        options.buildingHalo = config.buildingHalo;
        options.antiAliasUnits = config.antiAliasUnits;
        options.cameraZoom = config.cameraZoom;
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
                                         {"music-mode", std::to_string(static_cast<unsigned int>(options.musicTrackMode))},
                                         {"music-track-types", formatMusicTrackTypes(options.musicTrackTypes)},
                                         {"gamma", std::to_string(options.gamma)},
                                         {"shading-mode", std::to_string(static_cast<unsigned int>(options.shading))},
                                         {"anti-alias", options.antiAlias ? "true" : "false"},
                                         {"building-halo", options.buildingHalo ? "true" : "false"},
                                         {"anti-alias-units", options.antiAliasUnits ? "true" : "false"},
                                         {"camera-zoom", std::to_string(options.cameraZoom)},
                                     });
    }
}
