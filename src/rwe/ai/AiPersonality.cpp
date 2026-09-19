#include "AiPersonality.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <rwe/util.h>
#include <rwe/util/SimpleLogger.h>
#include <sstream>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        std::string lowered(std::string s)
        {
            for (auto& c : s)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        }

        std::string trimmed(const std::string& s)
        {
            auto begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
            {
                return std::string();
            }
            auto end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        }

        std::vector<AiPersonality> makeBuiltIns()
        {
            std::vector<AiPersonality> all;

            all.push_back(AiPersonality{"Balanced", "The computer player as it is tuned: expands, defends what it takes, and attacks in waves.", std::nullopt, {}});

            // Raiders early and often, and nothing spent standing still:
            // the lab comes up after two extractors and two solars, towers
            // are left out, and the first wave goes at five.
            all.push_back(AiPersonality{
                "Rush",
                "Raids early and keeps raiding: a small first wave, no towers, and no second tier.",
                std::nullopt,
                {
                    {"openingMetalExtractorCount", "2"},
                    {"openingSolarCount", "2"},
                    {"targetDefenceCount", "0"},
                    {"heavyDefenceCount", "0"},
                    {"outpostDefenceCount", "1"},
                    {"outpostTowerIncomeStep", "0"},
                    {"attackArmySize", "5"},
                    {"retreatArmySize", "2"},
                    {"reinforcementGroupSize", "2"},
                    {"labRaiderShare", "4"},
                    {"labRocketKbotShare", "1"},
                    {"labArtilleryKbotShare", "0"},
                    {"raidingParties", "1"},
                    {"raidPartySize", "3"},
                    {"techLevelTwo", "0"},
                    {"fortifyWhereAttacked", "0"},
                }});

            // Holds its ground: towers at home and at every cluster, the
            // lines fortified, and the army kept until it is large.
            all.push_back(AiPersonality{
                "Turtle",
                "Builds towers everywhere, fortifies them, and attacks late with a large army.",
                std::nullopt,
                {
                    {"targetDefenceCount", "6"},
                    {"heavyDefenceCount", "4"},
                    {"outpostDefenceCount", "5"},
                    {"outpostTowerIncomeStep", "5"},
                    {"outpostDefenceMax", "12"},
                    {"outpostDefenceMinExtractors", "1"},
                    {"fortifyTowers", "1"},
                    {"baseAntiAirTowerCount", "2"},
                    {"attackArmySize", "20"},
                    {"retreatArmySize", "6"},
                    {"raidingParties", "0"},
                }});

            // Straight for the second tier, whatever the faction: the tech
            // step is taken on a small income and whether or not the
            // advanced army is worth more per metal, and two advanced
            // constructors carry it.
            all.push_back(AiPersonality{
                "Tech Rush",
                "Goes for the second tier early, on a small income and a small army.",
                std::nullopt,
                {
                    {"techLevelTwo", "1"},
                    {"techMinMetalIncome", "6"},
                    {"techMinArmyValueRatio", "0"},
                    {"techSaveUpSeconds", "480"},
                    {"targetAdvancedConstructorCount", "2"},
                    {"targetDefenceCount", "1"},
                    {"attackArmySize", "12"},
                    {"labRaiderShare", "1"},
                }});

            all.push_back(AiPersonality{"Easy", "The easy difficulty, played as tuned.", AiDifficulty::Easy, {}});
            all.push_back(AiPersonality{"Medium", "The standard difficulty, played as tuned.", AiDifficulty::Standard, {}});
            all.push_back(AiPersonality{"Hard", "The hard difficulty, played as tuned.", AiDifficulty::Hard, {}});
            return all;
        }
    }

    std::optional<AiDifficulty> aiDifficultyFromName(const std::string& name)
    {
        auto n = lowered(trimmed(name));
        if (n == "idle" || n == "none" || n == "off")
        {
            return AiDifficulty::Idle;
        }
        if (n == "easy")
        {
            return AiDifficulty::Easy;
        }
        if (n == "standard" || n == "medium" || n == "normal")
        {
            return AiDifficulty::Standard;
        }
        if (n == "hard")
        {
            return AiDifficulty::Hard;
        }
        if (n == "brutal")
        {
            return AiDifficulty::Brutal;
        }
        return std::nullopt;
    }

    const std::vector<AiPersonality>& builtInAiPersonalities()
    {
        static const std::vector<AiPersonality> all = makeBuiltIns();
        return all;
    }

    AiPersonality parseAiPersonality(const std::string& text, const std::string& fallbackName)
    {
        AiPersonality personality;
        personality.name = fallbackName;
        std::istringstream in(text);
        std::string line;
        int lineNumber = 0;
        while (std::getline(in, line))
        {
            ++lineNumber;
            auto content = trimmed(line);
            if (content.empty() || content[0] == '#' || content[0] == ';')
            {
                continue;
            }
            auto equals = content.find('=');
            if (equals == std::string::npos)
            {
                throw std::runtime_error("line " + std::to_string(lineNumber) + " is not key=value: " + content);
            }
            auto key = trimmed(content.substr(0, equals));
            auto value = trimmed(content.substr(equals + 1));
            auto lowerKey = lowered(key);
            if (lowerKey == "name")
            {
                personality.name = value;
            }
            else if (lowerKey == "description")
            {
                personality.description = value;
            }
            else if (lowerKey == "difficulty")
            {
                auto difficulty = aiDifficultyFromName(value);
                if (!difficulty)
                {
                    throw std::runtime_error("line " + std::to_string(lineNumber) + ": no such difficulty: " + value);
                }
                personality.difficulty = difficulty;
            }
            else
            {
                // Tried on a scratch profile here, so a misspelt knob is
                // caught when the file is read rather than when a game is
                // loading.
                AiTuningProfile scratch;
                bool known = false;
                try
                {
                    known = applyAiTuning(scratch, key, value);
                }
                catch (const std::exception&)
                {
                    throw std::runtime_error("line " + std::to_string(lineNumber) + ": " + key + " cannot be " + value);
                }
                if (!known)
                {
                    throw std::runtime_error("line " + std::to_string(lineNumber) + ": no such AI knob: " + key);
                }
                personality.knobs.emplace_back(key, value);
            }
        }
        if (personality.name.empty())
        {
            throw std::runtime_error("the personality has no name");
        }
        return personality;
    }

    std::optional<std::filesystem::path> aiPersonalityDirectory()
    {
        auto local = getLocalDataPath();
        if (!local)
        {
            return std::nullopt;
        }
        return *local / "ai";
    }

    std::vector<AiPersonality> loadAiPersonalities(const std::optional<std::filesystem::path>& directory)
    {
        auto all = builtInAiPersonalities();
        if (!directory)
        {
            return all;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(*directory, ec))
        {
            return all;
        }
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(*directory, ec))
        {
            if (entry.is_regular_file(ec) && lowered(entry.path().extension().string()) == ".txt")
            {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files)
        {
            std::ifstream in(path, std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            try
            {
                auto personality = parseAiPersonality(buffer.str(), path.stem().string());
                auto existing = std::find_if(all.begin(), all.end(), [&](const AiPersonality& p) { return lowered(p.name) == lowered(personality.name); });
                if (existing != all.end())
                {
                    *existing = personality;
                }
                else
                {
                    all.push_back(personality);
                }
            }
            catch (const std::exception& e)
            {
                LOG_WARN << "AI personality " << path.string() << " skipped: " << e.what();
            }
        }
        return all;
    }

    std::optional<AiPersonality> findAiPersonality(const std::vector<AiPersonality>& personalities, const std::string& name)
    {
        auto wanted = lowered(trimmed(name));
        for (const auto& personality : personalities)
        {
            if (lowered(personality.name) == wanted)
            {
                return personality;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> applyAiPersonality(AiTuningProfile& profile, const AiPersonality& personality)
    {
        for (const auto& [knob, value] : personality.knobs)
        {
            if (!applyAiTuning(profile, knob, value))
            {
                return knob;
            }
        }
        return std::nullopt;
    }
}
