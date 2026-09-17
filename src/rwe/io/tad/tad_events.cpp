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
        result.weaponSlot = s[35];
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

    namespace
    {
        /**
         * Reads the way 0x415DC0 does: fields least significant bit first,
         * out of the subpacket taken as one little-endian integer.
         *
         * Reading past the end yields zeros and latches `overrun`, so a decoder
         * can read a whole record and ask once whether it was all there.
         */
        class TadBitReader
        {
        public:
            explicit TadBitReader(const TadBytes& bytes) : bytes(bytes) {}

            /** Reads `width` bits, width at most 32. */
            uint32_t read(unsigned int width)
            {
                if (position + width > bytes.size() * 8)
                {
                    overrun = true;
                    position = bytes.size() * 8;
                    return 0;
                }

                uint64_t value = 0;
                for (unsigned int i = 0; i < width; ++i, ++position)
                {
                    auto bit = (bytes[position / 8] >> (position % 8)) & 1u;
                    value |= static_cast<uint64_t>(bit) << i;
                }
                return static_cast<uint32_t>(value);
            }

            std::size_t bitsRead() const { return position; }

            std::size_t bitsLeft() const { return bytes.size() * 8 - position; }

            bool overrun{false};

        private:
            const TadBytes& bytes;
            std::size_t position{0};
        };

        uint32_t take(TadBitReader& r, unsigned int width)
        {
            return r.read(width);
        }

        TadPosition takePosition(TadBitReader& r)
        {
            auto x = static_cast<int32_t>(take(r, 32));
            auto y = static_cast<int32_t>(take(r, 32));
            auto z = static_cast<int32_t>(take(r, 32));
            return TadPosition{x, y, z};
        }

        int16_t takeS16(TadBitReader& r)
        {
            return static_cast<int16_t>(take(r, 16));
        }

        /** The navigator's serialiser, 0x44F4A0. */
        TadGroundPath takeGroundPath(TadBitReader& r)
        {
            TadGroundPath path;
            path.blocked = take(r, 1) != 0;
            auto count = take(r, 2);
            for (uint32_t i = 0; i < count; ++i)
            {
                auto x = takeS16(r);
                auto z = takeS16(r);
                path.waypoints.push_back(TadWaypoint{x, z});
            }
            return path;
        }

        /** The move goal's serialiser, 0x44DDC0. */
        TadMoveGoal takeMoveGoal(TadBitReader& r)
        {
            TadMoveGoal goal;
            goal.flags = static_cast<uint8_t>(take(r, 8));
            if (goal.flags & 0x01)
            {
                goal.unknown10 = takeS16(r);
                goal.attachedUnitId = static_cast<uint16_t>(take(r, 16));
            }
            if (goal.flags & 0x10)
            {
                goal.tolerance = takeS16(r);
            }
            if (goal.flags & 0x08)
            {
                goal.altitude = takeS16(r);
            }
            if (goal.flags & 0x40)
            {
                goal.headingOffset = takeS16(r);
            }
            if (goal.flags & 0x20)
            {
                goal.position = takePosition(r);
            }
            return goal;
        }

        /** The moving goal's serialiser, 0x44E930. */
        TadMovingGoal takeMovingGoal(TadBitReader& r)
        {
            TadMovingGoal goal;
            auto hasHeading = take(r, 1) != 0;
            goal.position = takePosition(r);
            goal.velocity = takePosition(r);
            if (hasHeading)
            {
                goal.heading = static_cast<uint16_t>(take(r, 16));
            }
            return goal;
        }

        /** The aircraft mover's serialiser, 0x4908C0. Nothing for the unused kind 3. */
        std::optional<TadAirMover> takeAirMover(TadBitReader& r)
        {
            TadAirMover mover;
            switch (take(r, 2))
            {
                case 0:
                    break;
                case 1:
                    mover.goal = takeMoveGoal(r);
                    break;
                case 2:
                    mover.goal = takeMovingGoal(r);
                    break;
                default:
                    return std::nullopt;
            }
            mover.movementMode = static_cast<uint8_t>(take(r, 2));
            return mover;
        }

        /** The full-state record, 0x48B200. */
        TadUnitSync takeUnitSync(TadBitReader& r, const TadUnitStateLayout& layout, uint16_t index)
        {
            TadUnitSync sync{};
            sync.index = index;
            sync.typeIndex = static_cast<uint16_t>(take(r, layout.typeIndexBits));
            if (sync.typeIndex == 0)
            {
                return sync;
            }

            sync.health = static_cast<uint16_t>(take(r, 16));
            sync.buildProgress = static_cast<uint8_t>(take(r, 8));
            sync.flags10E = static_cast<uint8_t>(take(r, 8));
            sync.motionState = static_cast<uint8_t>(take(r, 2));

            if (take(r, 1) != 0)
            {
                auto carrier = static_cast<uint16_t>(take(r, 15));
                auto piece = static_cast<int8_t>(take(r, 8));
                sync.carried = TadCarried{carrier, piece};
                return sync;
            }

            sync.position = takePosition(r);
            auto y = takeS16(r);
            auto z = takeS16(r);
            auto x = takeS16(r);
            sync.rotation = TadRotation{x, y, z};

            // The speed is written only when the unit has a mover, which the
            // receiver knows from its own copy of the unit and a reader of the
            // stream does not. It does not need to: this record is the last
            // thing in the subpacket, so padding leaves at most seven bits and
            // a speed leaves at least thirty-two.
            if (r.bitsLeft() >= 32)
            {
                sync.speed = static_cast<int32_t>(take(r, 32));
            }
            return sync;
        }
    }

    TadUnitStateLayout tadUnitStateLayout(const std::vector<bool>& canFlyInLoadOrder, uint16_t maxUnits)
    {
        // 0x42D65B: shift the type count right until it is gone, counting.
        TadUnitStateLayout layout;
        layout.maxUnits = maxUnits;
        layout.typeIndexBits = static_cast<unsigned int>(std::bit_width(canFlyInLoadOrder.size()));
        layout.canFly.push_back(false);
        layout.canFly.insert(layout.canFly.end(), canFlyInLoadOrder.begin(), canFlyInLoadOrder.end());
        return layout;
    }

    std::optional<TadUnitState> tadDecodeUnitState(const TadBytes& s, const TadUnitStateLayout& layout)
    {
        if (s.size() < 7 || static_cast<TadSubPacketCode>(s[0]) != TadSubPacketCode::UnitStatAndMove
            || readU16(&s[1]) != s.size() || layout.typeIndexBits == 0 || layout.maxUnits == 0)
        {
            return std::nullopt;
        }

        TadBitReader r(s);
        r.read(24);

        {
            TadUnitState state;
            state.tick = take(r, 32);

            while (!r.overrun)
            {
                auto index = static_cast<uint16_t>(take(r, 16));
                if (index == 0xffff)
                {
                    break;
                }

                TadUnitUpdate update;
                update.index = index;
                update.typeIndex = static_cast<uint16_t>(take(r, layout.typeIndexBits));
                if (update.typeIndex == 0 || update.typeIndex >= layout.canFly.size())
                {
                    return std::nullopt;
                }

                if (layout.canFly[update.typeIndex])
                {
                    auto mover = takeAirMover(r);
                    if (!mover)
                    {
                        return std::nullopt;
                    }
                    update.mover = std::move(*mover);
                }
                else
                {
                    update.mover = takeGroundPath(r);
                }
                state.updates.push_back(std::move(update));
            }

            if (take(r, 1) != 0)
            {
                // The builder always sets this bit (0x48B83F). Which unit follows
                // is not on the wire: it is the tick modulo maxUnits (0x48B835).
                state.sync = takeUnitSync(r, layout, static_cast<uint16_t>(state.tick % layout.maxUnits));
                if (state.sync->typeIndex >= layout.canFly.size())
                {
                    return std::nullopt;
                }
            }

            // Every bit accounted for: what is left is the padding of the last
            // byte, and nothing more.
            if (r.overrun || (r.bitsRead() + 7) / 8 != s.size())
            {
                return std::nullopt;
            }

            return state;
        }
    }

    uint16_t tadUnitIdOfIndex(unsigned int block, uint16_t index, uint16_t maxUnits)
    {
        return static_cast<uint16_t>(block * maxUnits + index + 1u);
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
