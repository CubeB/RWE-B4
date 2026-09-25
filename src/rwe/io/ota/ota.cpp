#include "ota.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <rwe/io/tdf/tdf.h>

namespace rwe
{
    OtaParseException::OtaParseException(const std::string& __arg) : runtime_error(__arg)
    {
    }

    OtaParseException::OtaParseException(const char* string) : runtime_error(string)
    {
    }

    OtaRecord parseOta(const TdfBlock& tdf)
    {
        auto block = tdf.findBlock("GlobalHeader");
        if (!block)
        {
            throw OtaParseException("Could not find GlobalHeader block");
        }

        return parseOtaGlobalHeader(*block);
    }

    OtaRecord parseOtaGlobalHeader(const TdfBlock& tdf)
    {
        OtaRecord r;

        tdf.read("missionname", r.missionName);

        tdf.readOrDefault("missiondescription", r.missionDescription);
        tdf.readOrDefault("planet", r.planet);
        tdf.readOrDefault("missionhint", r.missionHint);
        tdf.readOrDefault("brief", r.brief);
        tdf.readOrDefault("narration", r.narration);
        tdf.readOrDefault("glamour", r.glamour);
        tdf.readOrDefault("lineofsight", r.lineOfSight);
        tdf.readOrDefault("mapping", r.mapping);
        tdf.readOrDefault("tidalstrength", r.tidalStrength);
        tdf.readOrDefault("solarstrength", r.solarStrength);
        tdf.readOrDefault("lavaworld", r.lavaWorld);
        tdf.readOrDefault("killmul", r.killMul, 50);
        tdf.readOrDefault("timemul", r.timeMul);
        tdf.readOrDefault("minwindspeed", r.minWindSpeed);
        tdf.readOrDefault("maxwindspeed", r.maxWindSpeed);
        tdf.readOrDefault("gravity", r.gravity, 112);
        tdf.readOrDefault("numplayers", r.numPlayers);
        tdf.readOrDefault("size", r.size);
        tdf.readOrDefault("memory", r.memory);
        tdf.readOrDefault("useonlyunits", r.useOnlyUnits);

        // The campaign keys the mission reader adds (0x435F00-0x437300,
        // TOTALA-EXE-DATA.md S:105). A skirmish map names none of them and
        // takes the defaults.
        tdf.readOrDefault("maxunits", r.maxUnits, 200);
        tdf.readOrDefault("glamoursound", r.glamourSound);
        r.noMovie = tdf.extractInt("nomovie").value_or(0) != 0;
        r.noSeaLevelTrigger = tdf.extractInt("nosealeveltrigger").value_or(0) != 0;
        r.waterDoesDamage = tdf.extractInt("waterdoesdamage").value_or(0) != 0;
        tdf.readOrDefault("waterdamage", r.waterDamage);
        r.rules = parseOtaMissionRules(tdf);

        r.schemaCount = tdf.expectInt("SCHEMACOUNT");

        for (int i = 0; i < r.schemaCount; ++i)
        {
            auto schemaName = std::string("Schema ") + std::to_string(i);
            auto schemaBlock = tdf.findBlock(schemaName);
            if (!schemaBlock)
            {
                throw OtaParseException("Missing block: " + schemaName);
            }

            r.schemas.push_back(parseOtaSchema(*schemaBlock));
        }

        return r;
    }

    OtaSchema parseOtaSchema(const TdfBlock& tdf)
    {
        OtaSchema s;
        tdf.read("Type", s.type);
        tdf.readOrDefault("aiprofile", s.aiProfile, std::string("DEFAULT"));
        tdf.readOrDefault("SurfaceMetal", s.surfaceMetal, 3);
        tdf.readOrDefault("MohoMetal", s.mohoMetal, 30);
        tdf.readOrDefault("HumanMetal", s.humanMetal, 1000);
        tdf.readOrDefault("ComputerMetal", s.computerMetal, 1000);
        tdf.readOrDefault("HumanEnergy", s.humanEnergy, 1000);
        tdf.readOrDefault("ComputerEnergy", s.computerEnergy, 1000);
        tdf.readOrDefault("MeteorWeapon", s.meteorWeapon);
        tdf.readOrDefault("MeteorRadius", s.meteorRadius);
        tdf.readOrDefault("MeteorDensity", s.meteorDensity);
        tdf.readOrDefault("MeteorDuration", s.meteorDuration);
        tdf.readOrDefault("MeteorInterval", s.meteorInterval);

        auto featuresBlockOption = tdf.findBlock("features");
        if (featuresBlockOption)
        {
            auto featuresBlock = &featuresBlockOption->get();
            int i = 0;
            auto block = featuresBlock->findBlock("feature" + std::to_string(i));
            while (block)
            {
                s.features.push_back(parseOtaFeature(*block));

                i += 1;
                block = featuresBlock->findBlock("feature" + std::to_string(i));
            }
        }

        // A mission's starting units, [unit0], [unit1], ... until one is
        // missing (0x436C7E).
        if (auto unitsBlock = tdf.findBlock("units"))
        {
            for (int i = 0;; ++i)
            {
                auto block = unitsBlock->get().findBlock("unit" + std::to_string(i));
                if (!block)
                {
                    break;
                }
                s.units.push_back(parseOtaMissionUnit(*block));
            }
        }

        auto specialsBlockOption = tdf.findBlock("specials");
        if (specialsBlockOption)
        {
            auto specialsBlock = &specialsBlockOption->get();
            int i = 0;
            auto block = specialsBlock->findBlock("special" + std::to_string(i));
            while (block)
            {
                s.specials.push_back(parseOtaSpecial(*block));

                i += 1;
                block = specialsBlock->findBlock("special" + std::to_string(i));
            }
        }

        return s;
    }

    namespace
    {
        /** sscanf's `%[a-zA-Z]`: the leading run of letters. */
        std::string scanLetters(const std::string& text, std::size_t& at)
        {
            auto start = at;
            while (at < text.size() && std::isalpha(static_cast<unsigned char>(text[at])))
            {
                ++at;
            }
            return text.substr(start, at - start);
        }

        /** sscanf's `%[a-zA-Z0-9_.]`, after any spaces. */
        std::string scanName(const std::string& text, std::size_t& at)
        {
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
            {
                ++at;
            }
            auto start = at;
            while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '_' || text[at] == '.'))
            {
                ++at;
            }
            return text.substr(start, at - start);
        }

        /** sscanf's ` %f` or ` %d`: nothing when the text does not start (after spaces) with a number. */
        std::optional<float> scanNumber(const std::string& text, std::size_t& at)
        {
            auto p = at;
            while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p])))
            {
                ++p;
            }
            const char* begin = text.c_str() + p;
            char* end = nullptr;
            auto value = std::strtof(begin, &end);
            if (end == begin)
            {
                return std::nullopt;
            }
            at = p + static_cast<std::size_t>(end - begin);
            return value;
        }

        /** Up to `count` numbers, stopping at the first that is not there, as sscanf does. */
        std::vector<float> scanNumbers(const std::string& text, std::size_t& at, int count)
        {
            std::vector<float> numbers;
            for (int i = 0; i < count; ++i)
            {
                auto n = scanNumber(text, at);
                if (!n)
                {
                    break;
                }
                numbers.push_back(*n);
            }
            return numbers;
        }

        /** `%[a-zA-Z],%i[,%i,%i]`: a unit type and up to three integers after it. */
        std::optional<std::pair<std::string, std::vector<int>>> scanTypeAndIntegers(const std::string& text, int count)
        {
            std::size_t at = 0;
            auto type = scanLetters(text, at);
            if (type.empty())
            {
                return std::nullopt;
            }
            std::vector<int> values;
            for (int i = 0; i < count; ++i)
            {
                if (at >= text.size() || text[at] != ',')
                {
                    break;
                }
                ++at;
                auto n = scanNumber(text, at);
                if (!n)
                {
                    break;
                }
                values.push_back(static_cast<int>(*n));
            }
            return std::make_pair(type, values);
        }

        std::optional<OtaUnitTypeAndNumber> readTypeAndNumber(const TdfBlock& tdf, const std::string& key)
        {
            auto value = tdf.findValue(key);
            if (!value)
            {
                return std::nullopt;
            }
            auto scanned = scanTypeAndIntegers(value->get(), 1);
            if (!scanned || scanned->second.size() < 1)
            {
                return std::nullopt;
            }
            return OtaUnitTypeAndNumber{scanned->first, scanned->second[0]};
        }

        std::optional<std::string> readName(const TdfBlock& tdf, const std::string& key)
        {
            auto value = tdf.findValue(key);
            if (!value || value->get().empty())
            {
                return std::nullopt;
            }
            return value->get();
        }
    }

    std::vector<MissionOrder> parseInitialMission(const std::string& text)
    {
        std::vector<MissionOrder> orders;
        std::size_t at = 0;
        while (at < text.size())
        {
            // Each order runs to the next comma (0x487C3D).
            auto comma = text.find(',', at);
            auto piece = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            at = comma == std::string::npos ? text.size() : comma + 1;

            std::size_t p = 0;
            while (p < piece.size() && std::isspace(static_cast<unsigned char>(piece[p])))
            {
                ++p;
            }
            if (p >= piece.size())
            {
                continue;
            }
            auto letter = static_cast<char>(std::tolower(static_cast<unsigned char>(piece[p])));
            ++p;

            MissionOrder order;
            switch (letter)
            {
                case 'm':
                    order.kind = MissionOrder::Kind::Move;
                    order.numbers = scanNumbers(piece, p, 2);
                    break;
                case 'p':
                    order.kind = MissionOrder::Kind::Patrol;
                    order.numbers = scanNumbers(piece, p, 3);
                    break;
                case 'u':
                    order.kind = MissionOrder::Kind::Unload;
                    order.numbers = scanNumbers(piece, p, 2);
                    break;
                case 'a':
                {
                    // A point when both numbers scan (0x487F4F); otherwise the
                    // same text again as a unit type name (0x487FB3).
                    auto start = p;
                    auto numbers = scanNumbers(piece, p, 2);
                    if (numbers.size() == 2)
                    {
                        order.kind = MissionOrder::Kind::AttackPoint;
                        order.numbers = numbers;
                    }
                    else
                    {
                        p = start;
                        order.kind = MissionOrder::Kind::AttackType;
                        order.name = scanName(piece, p);
                    }
                    break;
                }
                case 'g':
                    order.kind = MissionOrder::Kind::Guard;
                    order.name = scanName(piece, p);
                    break;
                case 'i':
                    order.kind = MissionOrder::Kind::Link;
                    order.name = scanName(piece, p);
                    break;
                case 'o':
                    order.kind = MissionOrder::Kind::StandingOrders;
                    order.numbers = scanNumbers(piece, p, 2);
                    break;
                case 'w':
                {
                    // "wa" waits to be attacked; anything else is a timed wait.
                    auto q = p;
                    while (q < piece.size() && std::isspace(static_cast<unsigned char>(piece[q])))
                    {
                        ++q;
                    }
                    if (q < piece.size() && std::tolower(static_cast<unsigned char>(piece[q])) == 'a')
                    {
                        order.kind = MissionOrder::Kind::WaitForAttack;
                    }
                    else
                    {
                        order.kind = MissionOrder::Kind::Wait;
                        order.numbers = scanNumbers(piece, p, 2);
                    }
                    break;
                }
                case 'b':
                    // `bw n` is the silo's form (0x488005 tests the second
                    // letter); anything else is a unit to build.
                    if (p < piece.size() && std::tolower(static_cast<unsigned char>(piece[p])) == 'w')
                    {
                        ++p;
                        order.kind = MissionOrder::Kind::BuildWeapon;
                        order.numbers = scanNumbers(piece, p, 1);
                    }
                    else
                    {
                        order.kind = MissionOrder::Kind::Build;
                        order.name = scanName(piece, p);
                        order.numbers = scanNumbers(piece, p, 3);
                    }
                    break;
                case 'd':
                    order.kind = MissionOrder::Kind::SelfDestruct;
                    break;
                case 's':
                    order.kind = MissionOrder::Kind::MakeSelectable;
                    break;
                default:
                    // A letter the table does not know: 0x487E50 is only the
                    // loop going round, so nothing happens.
                    order.kind = MissionOrder::Kind::Skip;
                    break;
            }
            orders.push_back(std::move(order));
        }
        return orders;
    }

    OtaMissionRules parseOtaMissionRules(const TdfBlock& tdf)
    {
        OtaMissionRules r;
        tdf.readOrDefault("KillEnemyCommander", r.killEnemyCommander);
        tdf.readOrDefault("DestroyAllUnits", r.destroyAllUnits);
        tdf.readOrDefault("KillAllMobileUnits", r.killAllMobileUnits);
        r.buildUnitType = readName(tdf, "BuildUnitType");
        r.captureUnitType = readName(tdf, "CaptureUnitType");
        r.killAllOfType = readName(tdf, "KillAllOfType");
        r.killUnitType = readTypeAndNumber(tdf, "KillUnitType");
        if (auto value = tdf.findValue("MoveUnitToRadius"))
        {
            auto scanned = scanTypeAndIntegers(value->get(), 3);
            if (scanned && scanned->second.size() == 3)
            {
                r.moveUnitToRadius = OtaMoveUnitToRadius{scanned->first, scanned->second[0], scanned->second[1], scanned->second[2]};
            }
        }
        r.unitTypePassesX = readTypeAndNumber(tdf, "UnitTypePassesX");
        r.unitTypePassesZ = readTypeAndNumber(tdf, "UnitTypePassesZ");
        tdf.readOrDefault("VictoryTimerRunsOut", r.victoryTimerRunsOut);

        tdf.readOrDefault("CommanderKilled", r.commanderKilled);
        tdf.readOrDefault("AllUnitsKilled", r.allUnitsKilled);
        r.allUnitsKilledOfType = readName(tdf, "AllUnitsKilledOfType");
        r.unitTypeKilled = readTypeAndNumber(tdf, "UnitTypeKilled");
        tdf.readOrDefault("DeathTimerRunsOut", r.deathTimerRunsOut);
        tdf.readOrDefault("AnyUnitPassesX", r.anyUnitPassesX, -1);
        tdf.readOrDefault("AnyUnitPassesZ", r.anyUnitPassesZ, -1);
        return r;
    }

    OtaMissionUnit parseOtaMissionUnit(const TdfBlock& tdf)
    {
        OtaMissionUnit u;
        tdf.readOrDefault("Unitname", u.unitName);
        tdf.readOrDefault("Ident", u.ident);
        tdf.readOrDefault("InitialMission", u.initialMission);
        u.orders = parseInitialMission(u.initialMission);
        tdf.readOrDefault("XPos", u.xPos);
        tdf.readOrDefault("YPos", u.yPos);
        tdf.readOrDefault("ZPos", u.zPos);
        tdf.readOrDefault("Angle", u.angle);
        tdf.readOrDefault("Player", u.player);
        tdf.readOrDefault("HealthPercentage", u.healthPercentage, 100);
        tdf.readOrDefault("BuildPriority", u.buildPriority);
        tdf.readOrDefault("CreationCountdown", u.creationCountdown);
        // The four flags and the group share one byte at +0x23; the group
        // is its low four bits.
        u.initialGroup = tdf.extractInt("InitialGroup").value_or(0) & 0xF;
        u.missionCriticalUnit = tdf.extractInt("MissionCriticalUnit").value_or(0) != 0;
        u.aiIgnore = tdf.extractInt("AiIgnore").value_or(0) != 0;
        u.aiPriorityTarget = tdf.extractInt("AiPriorityTarget").value_or(0) != 0;
        u.immunity = tdf.extractInt("Immunity").value_or(0) != 0;
        // Kills is on every shipped [unit] and read by nothing.
        return u;
    }

    OtaFeature parseOtaFeature(const TdfBlock& tdf)
    {
        OtaFeature f;
        tdf.read("Featurename", f.featureName);
        tdf.read("XPos", f.xPos);
        tdf.read("ZPos", f.zPos);
        return f;
    }

    OtaSpecial parseOtaSpecial(const TdfBlock& tdf)
    {
        OtaSpecial s;
        tdf.read("specialwhat", s.specialWhat);
        tdf.read("XPos", s.xPos);
        tdf.read("ZPos", s.zPos);
        return s;
    }

    std::optional<OtaSpecial> findStartPosition(const OtaSchema& schema, int n)
    {
        auto key = "StartPos" + std::to_string(n);
        auto it = std::find_if(schema.specials.begin(), schema.specials.end(), [&key](const OtaSpecial& s) { return s.specialWhat == key; });
        if (it == schema.specials.end())
        {
            return std::nullopt;
        }
        return *it;
    }

    int countStartPositions(const OtaSchema& schema)
    {
        int count = 0;
        for (int n = 1; n <= 10; ++n)
        {
            if (findStartPosition(schema, n))
            {
                ++count;
            }
        }
        return count;
    }
}
