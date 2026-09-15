#include "tad_events.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <rwe/io/tad/tad_headers.h>

namespace rwe
{
    namespace
    {
        uint16_t readU16(const uint8_t* p)
        {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }

        int16_t readS16(const uint8_t* p)
        {
            return static_cast<int16_t>(readU16(p));
        }

        uint32_t readU32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0])
                | (static_cast<uint32_t>(p[1]) << 8)
                | (static_cast<uint32_t>(p[2]) << 16)
                | (static_cast<uint32_t>(p[3]) << 24);
        }

        int32_t readS32(const uint8_t* p)
        {
            return static_cast<int32_t>(readU32(p));
        }

        /** The wire's floats are IEEE-754 single, little end first. */
        float readF32(const uint8_t* p)
        {
            return std::bit_cast<float>(readU32(p));
        }

        TadPosition readPosition(const uint8_t* p)
        {
            return TadPosition{readS32(p), readS32(p + 4), readS32(p + 8)};
        }

        TadRotation readRotation(const uint8_t* p)
        {
            return TadRotation{readS16(p), readS16(p + 2), readS16(p + 4)};
        }

        /**
         * Whether a subpacket is the code we want at the length the size table
         * says it must be. Both halves matter: the length table is what keeps
         * the walk in step, so a subpacket that disagrees with it is one the
         * walker had already lost, and decoding it would invent a fact.
         */
        bool is(const TadBytes& s, TadSubPacketCode code, std::size_t size)
        {
            return s.size() == size && static_cast<TadSubPacketCode>(s[0]) == code;
        }
    }

    double tadFixedToDouble(int32_t value)
    {
        return static_cast<double>(value) / 65536.0;
    }

    std::optional<unsigned int> tadOwnerBlockOfUnitId(uint16_t unitId, uint16_t maxUnits)
    {
        if (unitId == 0 || maxUnits == 0)
        {
            return std::nullopt;
        }

        return static_cast<unsigned int>((unitId - 1) / maxUnits);
    }

    std::optional<TadBuildStarted> tadDecodeBuildStarted(const TadBytes& s)
    {
        if (!is(s, TadSubPacketCode::UnitBuildStarted, 23))
        {
            return std::nullopt;
        }

        TadBuildStarted result;
        result.typeIndex = readU16(&s[1]);
        result.unitId = readU16(&s[3]);
        result.position = readPosition(&s[5]);
        result.rotation = readRotation(&s[17]);
        return result;
    }

    std::optional<TadBuildFinished> tadDecodeBuildFinished(const TadBytes& s)
    {
        if (!is(s, TadSubPacketCode::UnitBuildFinished, 5))
        {
            return std::nullopt;
        }

        return TadBuildFinished{readU16(&s[1]), readU16(&s[3])};
    }

    std::optional<TadDamage> tadDecodeDamage(const TadBytes& s)
    {
        if (!is(s, TadSubPacketCode::UnitTakeDamage, 9))
        {
            return std::nullopt;
        }

        return TadDamage{readU16(&s[1]), readU16(&s[3]), readU16(&s[5]), readU16(&s[7])};
    }

    std::optional<TadDeath> tadDecodeDeath(const TadBytes& s)
    {
        if (!is(s, TadSubPacketCode::UnitKilled, 11))
        {
            return std::nullopt;
        }

        return TadDeath{readU16(&s[1]), readU32(&s[3]), readU16(&s[7]), s[9], s[10]};
    }

    std::optional<TadShot> tadDecodeShot(const TadBytes& s)
    {
        if (!is(s, TadSubPacketCode::WeaponFired, 36))
        {
            return std::nullopt;
        }

        TadShot result;
        result.origin = readPosition(&s[1]);
        result.target = readPosition(&s[13]);
        result.rotation = readRotation(&s[25]);
        result.targetId = readU16(&s[31]);
        result.shooterId = readU16(&s[33]);
        result.unknown = s[35];
        return result;
    }

    std::optional<TadScriptCall> tadDecodeScriptCall(const TadBytes& s)
    {
        if (!is(s, TadSubPacketCode::UnitStartScript, 22))
        {
            return std::nullopt;
        }

        TadScriptCall result;
        result.unitId = readU16(&s[1]);
        result.scriptIndex = readU16(&s[3]);
        result.argCount = s[5];
        for (int i = 0; i < 4; ++i)
        {
            result.args[i] = readS32(&s[6 + 4 * i]);
        }
        return result;
    }

    std::optional<TadResourceStats> tadDecodeResourceStats(const TadBytes& s)
    {
        if (!is(s, TadSubPacketCode::PlayerResourceInfo, 58))
        {
            return std::nullopt;
        }

        TadResourceStats result;
        std::memcpy(result.prefix, &s[1], sizeof(result.prefix));
        result.metalStored = readF32(&s[18]);
        result.energyStored = readF32(&s[22]);
        result.metalStorage = readF32(&s[26]);
        result.energyStorage = readF32(&s[30]);
        for (int i = 0; i < 3; ++i)
        {
            result.energyCounters[i] = readF32(&s[34 + 4 * i]);
            result.metalCounters[i] = readF32(&s[46 + 4 * i]);
        }
        return result;
    }

    uint32_t TadUnitTable::fingerprint() const
    {
        // FNV-1a over the restricted block's ids in ascending order. Nothing to
        // do with TA's own arithmetic: it exists only so that two recordings of
        // the same data set compare equal, which they do -- one value across the
        // twelve TA: Escalation demos and another for the one ProTA demo.
        uint32_t hash = 2166136261u;
        std::vector<uint32_t> ids;
        ids.reserve(restricted.size());
        for (const auto& entry : restricted)
        {
            ids.push_back(entry.id);
        }
        std::sort(ids.begin(), ids.end());

        for (auto id : ids)
        {
            for (int i = 0; i < 4; ++i)
            {
                hash ^= (id >> (8 * i)) & 0xffu;
                hash *= 16777619u;
            }
        }
        return hash;
    }

    std::optional<std::string> TadUnitTable::knownDataSet() const
    {
        // Two data sets are on hand and no more. An unrecognised fingerprint is
        // not an error -- it is the answer to docs/TA-DEMOS.md's question of
        // whether the unit table can identify a mod, and the answer is that it
        // can identify one it has seen before.
        switch (fingerprint())
        {
            case 0xfe0c3549u:
                return "TA: Escalation 10.2";
            case 0x8ff0ef4du:
                return "ProTA 4.8";
            default:
                return std::nullopt;
        }
    }

    std::optional<TadUnitTable> tadDecodeUnitTable(const TadBytes& record)
    {
        const std::size_t entrySize = 14;
        if (record.empty() || record.size() % entrySize != 0)
        {
            return std::nullopt;
        }

        TadUnitTable table;
        for (std::size_t i = 0; i < record.size(); i += entrySize)
        {
            const auto* p = &record[i];
            if (static_cast<TadSubPacketCode>(p[0]) != TadSubPacketCode::UnitData)
            {
                return std::nullopt;
            }

            // Bytes 2-5 are zero in every record of every demo read so far.
            TadUnitTableEntry entry{p[1], readU32(p + 6), readU32(p + 10)};
            (entry.sub == 3 ? table.restricted : table.listed).push_back(entry);
        }

        return table;
    }

    std::optional<TadSpeed> tadDecodeSpeed(const TadBytes& s)
    {
        if (!is(s, TadSubPacketCode::Speed, 3))
        {
            return std::nullopt;
        }

        return TadSpeed{readU16(&s[1])};
    }

    std::vector<std::string> tadUnitLoadOrder(std::vector<std::string> unitFileStems)
    {
        for (auto& name : unitFileStems)
        {
            for (auto& c : name)
            {
                // The corpus is already upper case throughout; this is here so
                // that a data set which is not does not silently reorder.
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }

        std::sort(unitFileStems.begin(), unitFileStems.end());
        return unitFileStems;
    }

    std::optional<std::string> tadUnitNameForTypeIndex(
        const std::vector<std::string>& loadOrder,
        uint16_t typeIndex)
    {
        // Numbered from one: index 0 does not appear anywhere in the corpus.
        if (typeIndex == 0 || typeIndex > loadOrder.size())
        {
            return std::nullopt;
        }

        return loadOrder[typeIndex - 1];
    }
}
