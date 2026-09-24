#include "tad_encoders.h"

#include <algorithm>
#include <bit>
#include <rwe/io/tad/tad_headers.h>

namespace rwe
{
    namespace
    {
        void writeU8(TadBytes& bytes, uint8_t value)
        {
            bytes.push_back(value);
        }

        void writeU16(TadBytes& bytes, uint16_t value)
        {
            writeU8(bytes, static_cast<uint8_t>(value & 0xff));
            writeU8(bytes, static_cast<uint8_t>(value >> 8));
        }

        void writeU32(TadBytes& bytes, uint32_t value)
        {
            for (unsigned int i = 0; i < 4; ++i)
            {
                writeU8(bytes, static_cast<uint8_t>((value >> (8 * i)) & 0xff));
            }
        }

        void writeS16(TadBytes& bytes, int16_t value)
        {
            writeU16(bytes, static_cast<uint16_t>(value));
        }

        void writeS32(TadBytes& bytes, int32_t value)
        {
            writeU32(bytes, static_cast<uint32_t>(value));
        }

        /** The wire's floats are IEEE-754 single, little-end first. */
        void writeF32(TadBytes& bytes, float value)
        {
            writeU32(bytes, std::bit_cast<uint32_t>(value));
        }

        void writePosition(TadBytes& bytes, const TadPosition& position)
        {
            writeS32(bytes, position.x);
            writeS32(bytes, position.y);
            writeS32(bytes, position.z);
        }

        void writeRotation(TadBytes& bytes, const TadRotation& rotation)
        {
            writeS16(bytes, rotation.x);
            writeS16(bytes, rotation.y);
            writeS16(bytes, rotation.z);
        }
    }

    TadBytes tadEncodeBuildStarted(const TadBuildStarted& buildStarted)
    {
        TadBytes bytes;
        bytes.reserve(23);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::UnitBuildStarted));
        writeU16(bytes, buildStarted.typeIndex);
        writeU16(bytes, buildStarted.unitId);
        writePosition(bytes, buildStarted.position);
        writeRotation(bytes, buildStarted.rotation);
        return bytes;
    }

    TadBytes tadEncodeDamage(const TadDamage& damage)
    {
        TadBytes bytes;
        bytes.reserve(9);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::UnitTakeDamage));
        writeU16(bytes, damage.victimId);
        writeU16(bytes, damage.attackerId);
        writeU16(bytes, damage.damage);
        writeU16(bytes, damage.unknown);
        return bytes;
    }

    TadBytes tadEncodeDeath(const TadDeath& death)
    {
        TadBytes bytes;
        bytes.reserve(11);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::UnitKilled));
        writeU16(bytes, death.unitId);
        writeU32(bytes, death.killerDplayId);
        writeU16(bytes, death.killerId);
        writeU8(bytes, death.severity);
        writeU8(bytes, death.causeAndLevel);
        return bytes;
    }

    TadBytes tadEncodeShot(const TadShot& shot)
    {
        TadBytes bytes;
        bytes.reserve(36);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::WeaponFired));
        writePosition(bytes, shot.origin);
        writePosition(bytes, shot.target);
        writeRotation(bytes, shot.rotation);
        writeU16(bytes, shot.targetId);
        writeU16(bytes, shot.shooterId);
        writeU8(bytes, shot.weaponSlot);
        return bytes;
    }

    TadBytes tadEncodeScriptCall(const TadScriptCall& scriptCall)
    {
        TadBytes bytes;
        bytes.reserve(22);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::UnitStartScript));
        writeU16(bytes, scriptCall.unitId);
        writeU16(bytes, scriptCall.scriptIndex);
        writeU8(bytes, scriptCall.argCount);
        for (auto arg : scriptCall.args)
        {
            writeS32(bytes, arg);
        }
        return bytes;
    }

    TadBytes tadEncodeBuildFinished(const TadBuildFinished& buildFinished)
    {
        TadBytes bytes;
        bytes.reserve(5);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::UnitBuildFinished));
        writeU16(bytes, buildFinished.unitId);
        writeU16(bytes, buildFinished.builderId);
        return bytes;
    }

    TadBytes tadEncodeSpeed(const TadSpeed& speed)
    {
        TadBytes bytes;
        bytes.reserve(3);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::Speed));
        writeU16(bytes, speed.value);
        return bytes;
    }

    TadBytes tadEncodeResourceStats(const TadResourceStats& stats)
    {
        TadBytes bytes;
        bytes.reserve(58);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::PlayerResourceInfo));
        for (auto value : stats.prefix)
        {
            writeU8(bytes, value);
        }
        writeF32(bytes, stats.metalStored);
        writeF32(bytes, stats.energyStored);
        writeF32(bytes, stats.metalStorage);
        writeF32(bytes, stats.energyStorage);
        for (auto value : stats.energyCounters)
        {
            writeF32(bytes, value);
        }
        for (auto value : stats.metalCounters)
        {
            writeF32(bytes, value);
        }
        return bytes;
    }

    namespace
    {
        /**
         * The writer at 0x415C10, reproduced: fields least significant bit
         * first, packed into little-endian 32-bit words. That is one bit
         * stream -- bit n at byte n/8, bit n%8 -- because a word written
         * little-end first continues at the next byte's bit 0.
         */
        class TadBitWriter
        {
        public:
            /** Writes `width` bits of `value`, width at most 32. */
            void write(uint32_t value, unsigned int width)
            {
                for (unsigned int i = 0; i < width; ++i)
                {
                    auto bit = position + i;
                    if (bit / 8 == bytes.size())
                    {
                        bytes.push_back(0);
                    }
                    bytes[bit / 8] |= static_cast<uint8_t>(((value >> i) & 1u) << (bit % 8));
                }
                position += width;
            }

            /**
             * The body, padded with zeros to its last byte. The reader ignores
             * the pad; the length counts it, because ceil(bits/8) does.
             */
            TadBytes takeBytes()
            {
                return std::move(bytes);
            }

        private:
            TadBytes bytes;
            std::size_t position{0};
        };

        void writePosition(TadBitWriter& writer, const TadPosition& position)
        {
            writer.write(static_cast<uint32_t>(position.x), 32);
            writer.write(static_cast<uint32_t>(position.y), 32);
            writer.write(static_cast<uint32_t>(position.z), 32);
        }

        /** The navigator's serialiser, 0x44F4A0. */
        void writeGroundPath(TadBitWriter& writer, const TadGroundPath& path)
        {
            writer.write(path.blocked ? 1 : 0, 1);

            // The navigator holds up to twenty waypoints and sends three; the
            // original writes min(nav+0x5C, 3), so more than three here is a
            // caller bug and truncating is the faithful thing to do with it.
            auto count = static_cast<uint32_t>(std::min<std::size_t>(path.waypoints.size(), 3));
            writer.write(count, 2);
            for (uint32_t i = 0; i < count; ++i)
            {
                writer.write(static_cast<uint16_t>(path.waypoints[i].x), 16);
                writer.write(static_cast<uint16_t>(path.waypoints[i].z), 16);
            }
        }

        /**
         * The move goal's serialiser, 0x44DDC0.
         *
         * The flag byte goes out as it stands, extra bits and all, because the
         * layout of everything after it is decided by those bits alone: a set
         * bit with no optional behind it still claims its field, and writes
         * zeros into it.
         */
        void writeMoveGoal(TadBitWriter& writer, const TadMoveGoal& goal)
        {
            writer.write(goal.flags, 8);
            if (goal.flags & 0x01)
            {
                writer.write(static_cast<uint16_t>(goal.unknown10.value_or(0)), 16);
                writer.write(goal.attachedUnitId.value_or(0), 16);
            }
            if (goal.flags & 0x10)
            {
                writer.write(static_cast<uint16_t>(goal.tolerance.value_or(0)), 16);
            }
            if (goal.flags & 0x08)
            {
                writer.write(static_cast<uint16_t>(goal.altitude.value_or(0)), 16);
            }
            if (goal.flags & 0x40)
            {
                writer.write(static_cast<uint16_t>(goal.headingOffset.value_or(0)), 16);
            }
            if (goal.flags & 0x20)
            {
                writePosition(writer, goal.position.value_or(TadPosition{}));
            }
        }

        /** The moving goal's serialiser, 0x44E930. */
        void writeMovingGoal(TadBitWriter& writer, const TadMovingGoal& goal)
        {
            writer.write(goal.heading ? 1 : 0, 1);
            writePosition(writer, goal.position);
            writePosition(writer, goal.velocity);
            if (goal.heading)
            {
                writer.write(*goal.heading, 16);
            }
        }

        /** The aircraft mover's serialiser, 0x4908C0. */
        void writeAirMover(TadBitWriter& writer, const TadAirMover& mover)
        {
            if (std::holds_alternative<std::monostate>(mover.goal))
            {
                writer.write(0, 2);
            }
            else if (auto* goal = std::get_if<TadMoveGoal>(&mover.goal))
            {
                writer.write(1, 2);
                writeMoveGoal(writer, *goal);
            }
            else
            {
                writer.write(2, 2);
                writeMovingGoal(writer, std::get<TadMovingGoal>(mover.goal));
            }

            writer.write(mover.movementMode, 2);
        }

        /**
         * The full-state record, 0x48B200. Writes a type index of zero for an
         * empty slot and stops, which is all that record is then.
         */
        void writeUnitSync(TadBitWriter& writer, const TadUnitSync& sync, const TadUnitStateLayout& layout)
        {
            writer.write(sync.typeIndex, layout.typeIndexBits);
            if (sync.typeIndex == 0)
            {
                return;
            }

            writer.write(sync.health, 16);
            writer.write(sync.buildProgress, 8);
            writer.write(sync.flags10E, 8);
            writer.write(sync.motionState, 2);

            if (sync.carried)
            {
                writer.write(1, 1);
                writer.write(sync.carried->carrierId, 15);
                writer.write(static_cast<uint8_t>(sync.carried->piece), 8);
                return;
            }

            writer.write(0, 1);
            writePosition(writer, sync.position);

            // Sent as y, z, x (unit+0x66, +0x68, +0x64) and stored the usual
            // way round, so that a reader gets the 0x09's own rotation back.
            writer.write(static_cast<uint16_t>(sync.rotation.y), 16);
            writer.write(static_cast<uint16_t>(sync.rotation.z), 16);
            writer.write(static_cast<uint16_t>(sync.rotation.x), 16);

            if (sync.speed)
            {
                writer.write(static_cast<uint32_t>(*sync.speed), 32);
            }
        }
    }

    TadBytes tadEncodeUnitState(const TadUnitState& state, const TadUnitStateLayout& layout)
    {
        if (layout.typeIndexBits == 0 || layout.maxUnits == 0)
        {
            return {};
        }

        TadBitWriter writer;
        writer.write(state.tick, 32);

        for (const auto& update : state.updates)
        {
            if (update.typeIndex == 0 || update.typeIndex >= layout.canFly.size())
            {
                return {};
            }

            writer.write(update.index, 16);
            writer.write(update.typeIndex, layout.typeIndexBits);

            // Which serialiser an entry uses is not on the wire; the receiver
            // rebuilds it from its own copy of the type, so the layout is what
            // decides here too.
            if (layout.canFly[update.typeIndex])
            {
                auto* mover = std::get_if<TadAirMover>(&update.mover);
                if (!mover)
                {
                    return {};
                }
                writeAirMover(writer, *mover);
            }
            else
            {
                auto* path = std::get_if<TadGroundPath>(&update.mover);
                if (!path)
                {
                    return {};
                }
                writeGroundPath(writer, *path);
            }
        }

        writer.write(0xffff, 16);

        if (state.sync)
        {
            if (state.sync->typeIndex >= layout.canFly.size())
            {
                return {};
            }

            // The builder always sets this bit (0x48B83F).
            writer.write(1, 1);
            writeUnitSync(writer, *state.sync, layout);
        }
        else
        {
            writer.write(0, 1);
        }

        auto body = writer.takeBytes();
        auto length = static_cast<uint16_t>(body.size() + 3);

        TadBytes bytes;
        bytes.reserve(body.size() + 3);
        writeU8(bytes, static_cast<uint8_t>(TadSubPacketCode::UnitStatAndMove));
        writeU16(bytes, length);
        bytes.insert(bytes.end(), body.begin(), body.end());
        return bytes;
    }
}
