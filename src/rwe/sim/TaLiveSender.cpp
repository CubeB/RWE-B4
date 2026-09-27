#include "TaLiveSender.h"

#include <rwe/io/tad/tad_encoders.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MixedOwnership.h>
#include <rwe/sim/TaWireTape.h>
#include <rwe/sim/UnitState.h>
#include <utility>

namespace rwe
{
    struct TaLiveSender::Impl
    {
        Impl(const GameSimulation& simulation, TaLiveSenderSettings settings, TaPeerIds peerIds)
            : sender(settings.sender),
              tape(
                  simulation,
                  TaWireTapeSettings{settings.maxUnits, std::move(settings.unitLoadOrder)},
                  std::vector<PlayerId>{settings.sender},
                  std::move(peerIds))
        {
        }

        void unitCreated(const GameSimulation& simulation, UnitId unit)
        {
            auto unitRef = simulation.tryGetUnitState(unit);
            if (!unitRef || unitRef->get().owner != sender)
            {
                return;
            }

            if (tape.unitCreated(simulation, unit) != TaWireRefusal::None)
            {
                ++stats.unitsRefused;
            }
        }

        void buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit)
        {
            tape.buildStarted(simulation, builder, unit);
        }

        void shotFired(
            UnitId shooter,
            unsigned int weaponSlot,
            std::optional<UnitId> targetUnit,
            const SimVector& origin,
            const SimVector& aimPoint,
            const SimVector& direction)
        {
            tape.shotFired(shooter, weaponSlot, targetUnit, origin, aimPoint, direction);
        }

        void unitCaptured(const GameSimulation& simulation, UnitId unit, PlayerId newOwner)
        {
            if (tape.unitCaptured(simulation, unit, newOwner) != TaWireRefusal::None)
            {
                ++stats.unitsRefused;
            }
        }

        void endOfTick(const GameSimulation& simulation)
        {
            // The outbox first: a death names a unit the tick's sweep has
            // already taken out of the world, and the tape forgets the id it
            // held once the tick's records are built.
            drainOutbox(simulation);
            auto records = tape.endOfTick(simulation, sender);

            if (records.unitState.empty())
            {
                ++stats.unitStateSkipped;
            }

            // The order is the recorder's and the oracles': unit pass, 0x2c,
            // projectile pass, settle. A tick with nothing in it is no batch,
            // and the peer's clock is the 0x2c's serial in any case.
            std::vector<TadBytes> subPackets;
            subPackets.reserve(
                records.unitPass.size() + records.projectilePass.size() + records.settle.size() + out.damage.size() + out.deaths.size() + 1);
            subPackets.insert(subPackets.end(), records.unitPass.begin(), records.unitPass.end());
            if (!records.unitState.empty())
            {
                subPackets.push_back(std::move(records.unitState));
            }
            // The tape's own projectile records come first: they were queued
            // as the tick happened, where the outbox's are read at the end of
            // it. The only one a sender gets is the cause-4 death of a unit
            // that changed hands.
            subPackets.insert(subPackets.end(), records.projectilePass.begin(), records.projectilePass.end());
            subPackets.insert(subPackets.end(), out.damage.begin(), out.damage.end());
            subPackets.insert(subPackets.end(), out.deaths.begin(), out.deaths.end());
            subPackets.insert(subPackets.end(), records.settle.begin(), records.settle.end());

            out.damage.clear();
            out.deaths.clear();

            if (subPackets.empty())
            {
                return;
            }

            ++stats.batches;
            pending.push_back(TaLiveBatch{static_cast<uint32_t>(simulation.gameTime.value), std::move(subPackets)});
        }

        /**
         * The outbox, which is where this machine's own hits and deaths are
         * recorded: a `0x0b` for a hit on a unit the peer simulates and a
         * `0x0c` for one of our own. The simulation only ever fills it, so
         * draining it is the whole of this side's ownership of it, and a
         * record naming a unit that cannot be named costs that record alone.
         */
        void drainOutbox(const GameSimulation& simulation)
        {
            if (!simulation.mixedOwnershipOutbox)
            {
                return;
            }
            auto& outbox = *simulation.mixedOwnershipOutbox;

            for (const auto& hit : outbox.damage)
            {
                auto victimId = tape.idOf(hit.victim);
                if (!victimId)
                {
                    ++stats.recordsDroppedNoId;
                    continue;
                }
                uint16_t attackerId = 0;
                if (hit.attacker)
                {
                    attackerId = tape.idOf(*hit.attacker).value_or(0u);
                }
                out.damage.push_back(tadEncodeDamage(TadDamage{
                    *victimId,
                    attackerId,
                    static_cast<uint16_t>(hit.damage > 0xffffu ? 0xffffu : hit.damage),
                    // Not remaining health and not identified (tad_events.h);
                    // RWE has nothing to derive it from.
                    0}));
            }

            for (const auto& death : outbox.deaths)
            {
                auto unitId = tape.idOf(death.unit);
                if (!unitId)
                {
                    ++stats.recordsDroppedNoId;
                    continue;
                }

                uint32_t killerDplayId = 0xffffffffu;
                if (death.killer)
                {
                    if (auto unitRef = simulation.tryGetUnitState(*death.killer))
                    {
                        killerDplayId = tape.dplayIdOf(unitRef->get().owner).value_or(0xffffffffu);
                    }
                }

                uint16_t killerId = 0;
                if (death.killer)
                {
                    killerId = tape.idOf(*death.killer).value_or(0u);
                }
                out.deaths.push_back(tadEncodeDeath(TadDeath{
                    *unitId,
                    killerDplayId,
                    killerId,
                    static_cast<uint8_t>(death.severity > 255u ? 255u : death.severity),
                    death.causeAndLevel}));
            }

            outbox.clear();
        }

        /** What the outbox produced this tick, in the order it goes out. */
        struct Outgoing
        {
            std::vector<TadBytes> damage;
            std::vector<TadBytes> deaths;
        };

        PlayerId sender;
        TaWireTape tape;
        Outgoing out;
        std::vector<TaLiveBatch> pending;
        TaLiveSenderStats stats;
    };

    TaLiveSender::TaLiveSender(const GameSimulation& simulation, TaLiveSenderSettings settings, TaPeerIds peerIds)
        : impl(std::make_unique<Impl>(simulation, std::move(settings), std::move(peerIds)))
    {
    }

    TaLiveSender::~TaLiveSender() = default;

    void TaLiveSender::unitCreated(const GameSimulation& simulation, UnitId unit)
    {
        impl->unitCreated(simulation, unit);
    }

    void TaLiveSender::unitRemoved(UnitId unit)
    {
        impl->tape.unitRemoved(unit);
    }

    void TaLiveSender::buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit)
    {
        impl->buildStarted(simulation, builder, unit);
    }

    void TaLiveSender::shotFired(
        const GameSimulation& /*simulation*/,
        UnitId shooter,
        unsigned int weaponSlot,
        std::optional<UnitId> targetUnit,
        const SimVector& origin,
        const SimVector& aimPoint,
        const SimVector& direction)
    {
        impl->shotFired(shooter, weaponSlot, targetUnit, origin, aimPoint, direction);
    }

    void TaLiveSender::unitCaptured(const GameSimulation& simulation, UnitId unit, PlayerId newOwner)
    {
        impl->unitCaptured(simulation, unit, newOwner);
    }

    void TaLiveSender::endOfTick(const GameSimulation& simulation)
    {
        impl->endOfTick(simulation);
    }

    std::optional<TaLiveBatch> TaLiveSender::takeBatch()
    {
        if (impl->pending.empty())
        {
            return std::nullopt;
        }
        auto batch = std::move(impl->pending.front());
        impl->pending.erase(impl->pending.begin());
        return batch;
    }

    const TaLiveSenderStats& TaLiveSender::stats() const
    {
        return impl->stats;
    }
}
