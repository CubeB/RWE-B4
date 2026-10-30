#include "GlobalConfig.h"
#include <cctype>
#include <algorithm>
#include <cmath>

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

    std::vector<unsigned int> uiScaleStages()
    {
        return {0u, 100u, 200u, 300u};
    }

    std::vector<std::string> uiScaleLabels()
    {
        return {"UI Auto", "UI 1x", "UI 2x", "UI 3x"};
    }

    unsigned int nextUiScale(unsigned int setting)
    {
        auto stages = uiScaleStages();
        auto index = uiScaleStageIndex(setting);
        return stages[(index + 1) % stages.size()];
    }

    namespace
    {
        unsigned int wholeUiScale(float scale)
        {
            return std::clamp(static_cast<unsigned int>(std::lround(std::max(0.0f, scale))), 1u, 3u);
        }
    }

    unsigned int uiScaleStageIndex(unsigned int setting)
    {
        // A percentage between the stages -- 150 and 250 were stages once --
        // reads as the whole step it rounds to, which is also what it draws at.
        return setting == 0u ? 0u : wholeUiScale(static_cast<float>(setting) / 100.0f);
    }

    unsigned int requestedUiScale(unsigned int setting, float contentScale)
    {
        return wholeUiScale(setting != 0u ? static_cast<float>(setting) / 100.0f : contentScale);
    }

    unsigned int largestFittingUiScale(int frameWidth, int frameHeight)
    {
        // The sidebar alone is 480 tall, so a scale that leaves less than
        // 640x480 of layout crops the HUD rather than enlarging it.
        return static_cast<unsigned int>(std::max(1, std::min(frameWidth / 640, frameHeight / 480)));
    }

    float resolveUiScale(unsigned int setting, float contentScale, int frameWidth, int frameHeight)
    {
        return static_cast<float>(std::min(requestedUiScale(setting, contentScale), largestFittingUiScale(frameWidth, frameHeight)));
    }

    float cameraZoomToSlider(unsigned int percent)
    {
        auto clamped = std::clamp(percent, MinCameraZoom, MaxCameraZoom);
        return static_cast<float>(clamped - MinCameraZoom) / static_cast<float>(MaxCameraZoom - MinCameraZoom);
    }

    unsigned int cameraZoomFromSlider(float position)
    {
        auto span = static_cast<float>(MaxCameraZoom - MinCameraZoom);
        auto offset = std::lround(std::clamp(position, 0.0f, 1.0f) * span);
        return MinCameraZoom + static_cast<unsigned int>(offset);
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
        options.uiScale = config.uiScale;
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

    std::vector<std::string> windowModeStages()
    {
        return {"windowed", "borderless", "fullscreen"};
    }

    unsigned int windowModeStageIndex(const std::string& mode)
    {
        auto stages = windowModeStages();
        // "bordered" is the name the key had before it was called windowed;
        // main.cpp still accepts it, so the button shows it as stage zero.
        auto wanted = mode == "bordered" ? std::string("windowed") : mode;
        for (unsigned int i = 0; i < stages.size(); ++i)
        {
            if (stages[i] == wanted)
            {
                return i;
            }
        }
        // Anything else is what a fresh or hand-edited rwe.cfg gets, and the
        // startup path treats it as windowed too.
        return 0;
    }

    std::string nextWindowMode(const std::string& mode)
    {
        auto stages = windowModeStages();
        return stages[(windowModeStageIndex(mode) + 1) % stages.size()];
    }

    std::vector<std::string> windowModeLabels()
    {
        std::vector<std::string> labels;
        for (const auto& stage : windowModeStages())
        {
            labels.emplace_back(windowModeDisplayName(stage));
        }
        return labels;
    }

    std::optional<OptionsPage> optionsPageFromName(const std::string& name)
    {
        if (name == "SOUNDS" || name == "SOUNDSRT")
        {
            return OptionsPage::Sound;
        }
        if (name == "MUSIC" || name == "MUSICRT")
        {
            return OptionsPage::Music;
        }
        if (name == "SPEEDS" || name == "SPEEDSRT")
        {
            return OptionsPage::Speed;
        }
        if (name == "VISUALS" || name == "VISUALRT")
        {
            return OptionsPage::Visual;
        }
        return std::nullopt;
    }

    GameOptions optionsWithPageFrom(const GameOptions& base, const GameOptions& source, OptionsPage page)
    {
        auto result = base;
        switch (page)
        {
            case OptionsPage::Sound:
                result.soundVolume = source.soundVolume;
                result.soundMode = source.soundMode;
                result.unitSpeech = source.unitSpeech;
                break;
            case OptionsPage::Music:
                result.musicVolume = source.musicVolume;
                result.musicEnabled = source.musicEnabled;
                result.musicTrackMode = source.musicTrackMode;
                result.musicTrackTypes = source.musicTrackTypes;
                break;
            case OptionsPage::Speed:
                result.scrollSpeed = source.scrollSpeed;
                result.uiScale = source.uiScale;
                break;
            case OptionsPage::Visual:
                result.windowMode = source.windowMode;
                result.shadows = source.shadows;
                result.shading = source.shading;
                result.antiAlias = source.antiAlias;
                result.buildingHalo = source.buildingHalo;
                result.antiAliasUnits = source.antiAliasUnits;
                result.gamma = source.gamma;
                result.cameraZoom = source.cameraZoom;
                break;
        }
        return result;
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
                                         {"ui-scale", std::to_string(options.uiScale)},
                                     });
    }

    void writeSwitchAlt(const std::filesystem::path& configPath, bool enabled)
    {
        updateConfigFile(configPath, {{SwitchAltConfigKey, enabled ? "true" : "false"}});
    }
}
