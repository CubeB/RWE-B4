#include "ResourceSharing.h"

#include <algorithm>
#include <cmath>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MissionRules.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/util/Index.h>

namespace rwe
{
    namespace
    {
        /**
         * Which of the two resources a transfer moves. The original names its
         * two transfer routines the wrong way round; this names them by what
         * they move.
         */
        enum class Resource
        {
            Metal,
            Energy
        };

        float stockOf(const GamePlayerInfo& p, Resource r)
        {
            return r == Resource::Metal ? p.metal.value : p.energy.value;
        }

        float capacityOf(const GamePlayerInfo& p, Resource r)
        {
            return r == Resource::Metal ? p.maxMetal.value : p.maxEnergy.value;
        }

        bool sharingOn(const GamePlayerInfo& p, Resource r)
        {
            return r == Resource::Metal ? p.shareMetal : p.shareEnergy;
        }

        float reserveOf(const GamePlayerInfo& p, Resource r)
        {
            return r == Resource::Metal ? p.metalReserve.value : p.energyReserve.value;
        }

        /** Moves `amount` out of the donor's account and into the receiver's. */
        void transfer(GamePlayerInfo& donor, GamePlayerInfo& receiver, Resource r, float amount)
        {
            // Not addResourceDelta, which would also record the amount as
            // consumption the donor "desired", and not recordDesire for the
            // same reason: a gift is not spending on anything.
            if (r == Resource::Metal)
            {
                donor.acceptResource(Metal(-amount));
                receiver.acceptResource(Metal(amount));
            }
            else
            {
                donor.acceptResource(Energy(-amount));
                receiver.acceptResource(Energy(amount));
            }
        }

        /**
         * The automatic pass's transfer (0x464B30, 0x464C60; BT-27): limited to
         * what the donor actually has, taken out of its stockpile at once, and
         * added to the receiver's production figure, so it reaches the
         * receiver's stockpile at the next settle. A manual gift keeps going
         * through `transfer`, whose donor side is booked as a spend.
         */
        void transferFromStockpile(GamePlayerInfo& donor, GamePlayerInfo& receiver, Resource r, float amount)
        {
            if (r == Resource::Metal)
            {
                amount = std::min(amount, donor.metal.value);
                donor.metal -= Metal(amount);
                receiver.acceptResource(Metal(amount));
            }
            else
            {
                amount = std::min(amount, donor.energy.value);
                donor.energy -= Energy(amount);
                receiver.acceptResource(Energy(amount));
            }
        }

        bool isPlayerOf(const GameSimulation& sim, PlayerId id)
        {
            return id.value < sim.players.size();
        }

        void runAutomaticSharingFor(GameSimulation& sim, Index donorIndex, Resource r, float divisor)
        {
            auto& donor = sim.players[donorIndex];
            if (!sharingOn(donor, r))
            {
                return;
            }

            auto stock = stockOf(donor, r);
            auto reserve = reserveOf(donor, r);
            if (!(stock > reserve))
            {
                return;
            }

            // The last qualifying slot wins. Nothing compares the candidates
            // with each other.
            std::optional<Index> recipientIndex;
            for (Index i = 0; i < getSize(sim.players); ++i)
            {
                if (i == donorIndex)
                {
                    continue;
                }

                const auto& candidate = sim.players[i];
                if (candidate.status != GamePlayerStatus::Alive
                    || candidate.type != GamePlayerType::Human
                    || !simulatesLocally(sim, PlayerId(static_cast<unsigned int>(i)))
                    || !sim.arePlayersAllied(PlayerId(static_cast<unsigned int>(donorIndex)), PlayerId(static_cast<unsigned int>(i)))
                    || !(stockOf(candidate, r) < stock))
                {
                    continue;
                }

                recipientIndex = i;
            }

            if (!recipientIndex)
            {
                return;
            }

            auto& recipient = sim.players[*recipientIndex];
            auto offer = (stock - reserve) / divisor;

            // Never more than the donor holds: a reserve typed below zero
            // would otherwise make the surplus bigger than the stock.
            offer = std::min(offer, stock);

            auto free = capacityOf(recipient, r) - stockOf(recipient, r);
            offer = std::min(offer, free);
            if (!(offer > 0.0f))
            {
                return;
            }

            transferFromStockpile(donor, recipient, r, offer);
        }
    }

    bool applyPlayerShareCommand(GameSimulation& sim, PlayerId issuer, const PlayerShareCommand& command)
    {
        if (!isPlayerOf(sim, issuer))
        {
            return false;
        }

        auto& player = sim.players[issuer.value];

        switch (command.kind)
        {
            case PlayerShareCommand::Kind::ToggleMetal:
                player.shareMetal = !player.shareMetal;
                return true;

            case PlayerShareCommand::Kind::ToggleEnergy:
                player.shareEnergy = !player.shareEnergy;
                return true;

            case PlayerShareCommand::Kind::SetMetalReserve:
            {
                // Negative numbers are accepted and stored as typed, and the
                // reserve does not enable sharing: both as in the original.
                if (!std::isfinite(command.amount))
                {
                    return false;
                }
                player.metalReserve = Metal(std::min(player.maxMetal.value, command.amount));
                return true;
            }

            case PlayerShareCommand::Kind::SetEnergyReserve:
            {
                if (!std::isfinite(command.amount))
                {
                    return false;
                }
                player.energyReserve = Energy(std::min(player.maxEnergy.value, command.amount));
                return true;
            }

            case PlayerShareCommand::Kind::Give:
            {
                if (player.status != GamePlayerStatus::Alive || !command.target)
                {
                    return false;
                }

                auto target = *command.target;
                if (!isPlayerOf(sim, target) || target == issuer)
                {
                    return false;
                }

                if (!simulatesLocally(sim, issuer) || !simulatesLocally(sim, target))
                {
                    return false;
                }

                // A negative amount is ignored, which the original does not
                // do (see the header). NaN and infinity are not amounts.
                if (!std::isfinite(command.amount) || !(command.amount > 0.0f))
                {
                    return false;
                }

                auto resource = command.giveMetal ? Resource::Metal : Resource::Energy;
                auto amount = std::min(command.amount, stockOf(player, resource));
                if (!(amount > 0.0f))
                {
                    return false;
                }

                transfer(player, sim.players[target.value], resource, amount);
                return true;
            }

            case PlayerShareCommand::Kind::SetMetalStock:
            case PlayerShareCommand::Kind::SetEnergyStock:
            {
                // +NoMetal/+NoEnergy: the original assigns the stockpile
                // outright, with no capacity or sign check, to the named
                // player (argument one, zero-based) or the local one when it
                // is absent. The command is replicated so every peer agrees
                // (#910, BT-GAME-CONSOLE-COMMANDS-PARITY section 5).
                auto target = command.target.value_or(issuer);
                if (!isPlayerOf(sim, target) || !std::isfinite(command.amount))
                {
                    return false;
                }
                auto& stock = sim.players[target.value];
                if (command.kind == PlayerShareCommand::Kind::SetMetalStock)
                {
                    stock.metal = Metal(command.amount);
                }
                else
                {
                    stock.energy = Energy(command.amount);
                }
                return true;
            }

            case PlayerShareCommand::Kind::AtmGrant:
            {
                // +ATM: +1000 metal and +1000 energy to the viewed player.
                // RWE has no separate viewed player, so the recipient is the
                // issuer. No storage cap, as in the original (#912).
                if (player.status != GamePlayerStatus::Alive)
                {
                    return false;
                }
                auto target = command.target.value_or(issuer);
                if (!isPlayerOf(sim, target))
                {
                    return false;
                }
                auto& recipient = sim.players[target.value];
                recipient.metal = Metal(recipient.metal.value + 1000.0f);
                recipient.energy = Energy(recipient.energy.value + 1000.0f);
                return true;
            }
        }

        return false;
    }

    void runAutomaticSharing(GameSimulation& sim)
    {
        // The same freeze the settle honours: a mission countdown holds the
        // whole economy, and a transfer that would settle is part of it.
        if (sim.missionRules && sim.missionRules->economyFrozen())
        {
            return;
        }

        if (sim.gameTime % GameTime(2 * SimTicksPerSecond) != GameTime(0))
        {
            return;
        }

        for (Index i = 0; i < getSize(sim.players); ++i)
        {
            const auto& donor = sim.players[i];
            if (donor.status != GamePlayerStatus::Alive
                || donor.type != GamePlayerType::Human
                || !simulatesLocally(sim, PlayerId(static_cast<unsigned int>(i))))
            {
                continue;
            }

            // Metal then energy, each on its own: a third of the surplus of
            // one, a half of the surplus of the other.
            runAutomaticSharingFor(sim, i, Resource::Metal, 3.0f);
            runAutomaticSharingFor(sim, i, Resource::Energy, 2.0f);
        }
    }
}
