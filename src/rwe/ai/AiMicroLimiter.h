#pragma once

#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace rwe
{
    /** Shared ground/air order budget. Uses simulation time, never wall time. */
    class AiMicroLimiter
    {
    public:
        static unsigned int ordersPerMinute(AiDifficulty difficulty)
        {
            switch (difficulty)
            {
                case AiDifficulty::Easy: return 60;
                case AiDifficulty::Standard: return 120;
                default: return 0; // Hard and Brutal have no additional cap.
            }
        }

        void filter(std::vector<PlayerCommand>& commands, std::size_t first, GameTime now, AiDifficulty difficulty)
        {
            const auto rate = ordersPerMinute(difficulty);
            if (rate == 0)
            {
                return;
            }
            constexpr unsigned int minute = 60u * SimTicksPerSecond;
            // Up to four orders at once, rather than spending a whole minute's
            // allowance in one tick. Queued patrol pairs stay together.
            constexpr std::uint64_t burst = 4u * minute;
            if (!initialized)
            {
                credit = burst;
                initialized = true;
            }
            else
            {
                credit = std::min(burst, credit + std::uint64_t(now.value - lastTick) * rate);
            }
            lastTick = now.value;
            while (!recent.empty() && now.value - recent.front() >= minute)
            {
                recent.pop_front();
            }

            std::map<unsigned int, std::vector<std::size_t>> byUnit;
            for (auto i = first; i < commands.size(); ++i)
            {
                auto unit = std::get_if<PlayerUnitCommand>(&commands[i]);
                auto order = unit ? std::get_if<PlayerUnitCommand::IssueOrder>(&unit->command) : nullptr;
                if (order && (std::holds_alternative<MoveOrder>(order->order)
                    || std::holds_alternative<AttackOrder>(order->order)
                    || std::holds_alternative<PatrolOrder>(order->order)
                    || std::holds_alternative<DgunOrder>(order->order)
                    || std::holds_alternative<GuardOrder>(order->order)))
                {
                    byUnit[unit->unit.value].push_back(i);
                }
            }
            std::vector<unsigned int> units;
            for (const auto& entry : byUnit)
            {
                units.push_back(entry.first);
            }
            // Previously unserved units go first, then the least recently
            // served. A low unit ID must not monopolize the shared allowance.
            auto priority = [&](unsigned int unit) {
                auto it = lastIssued.find(unit);
                return std::make_pair(it != lastIssued.end(), it == lastIssued.end() ? 0u : it->second);
            };
            std::stable_sort(units.begin(), units.end(), [&](auto a, auto b) { return priority(a) < priority(b); });
            std::vector<bool> keep(commands.size(), true);
            bool waiting = false;
            for (auto unit : units)
            {
                const auto& indices = byUnit.at(unit);
                const auto cost = std::uint64_t(indices.size()) * minute;
                if (!waiting && credit >= cost && recent.size() + indices.size() <= rate)
                {
                    credit -= cost;
                    lastIssued[unit] = now.value;
                    recent.insert(recent.end(), indices.size(), now.value);
                }
                else
                {
                    // Save up for an older multi-order patrol rather than
                    // letting cheap single orders starve it indefinitely.
                    waiting = true;
                    for (auto index : indices)
                    {
                        keep[index] = false;
                    }
                }
            }
            std::size_t index = 0;
            commands.erase(std::remove_if(commands.begin(), commands.end(), [&](const auto&) { return !keep[index++]; }), commands.end());
            // Rejected orders are regenerated from the next tactical pass's
            // world state, rather than replaying stale queued decisions.
        }

    private:
        bool initialized{false};
        unsigned int lastTick{0};
        std::uint64_t credit{0};
        std::deque<unsigned int> recent;
        std::map<unsigned int, unsigned int> lastIssued;
    };
}
