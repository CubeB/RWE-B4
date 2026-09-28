#include "AiUnitDefIndex.h"
#include <algorithm>
#include <functional>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>

namespace rwe
{
    namespace
    {
        // Enough for one AI's army at a quarter load from the first tick, and
        // doubled on demand: a game with a hundred distinct types on one side
        // is not a thing the data has, but the table is not allowed to be the
        // reason it falls over.
        const unsigned int InitialSlotCount = 64u;

        /**
         * Whether this type is one of the side's own warships.
         *
         * Six string comparisons that used to be made afresh for every unit of
         * every one of the side's hulls, on every tick. The answer cannot
         * differ between two hulls of the same type.
         */
        bool isSideNavalUnit(const AiSideUnits& sideUnits, const std::string& unitType)
        {
            return (!sideUnits.destroyer.empty() && unitType == sideUnits.destroyer)
                || (!sideUnits.submarine.empty() && unitType == sideUnits.submarine)
                || (!sideUnits.cruiser.empty() && unitType == sideUnits.cruiser)
                || (!sideUnits.battleship.empty() && unitType == sideUnits.battleship)
                || (!sideUnits.antiAirShip.empty() && unitType == sideUnits.antiAirShip)
                || (!sideUnits.scoutShip.empty() && unitType == sideUnits.scoutShip);
        }
    }

    void AiUnitDefIndex::beginPass()
    {
        // The rows keep their name buffers, so a pass after the first does not
        // allocate: clear() destroys nothing that has capacity to spare.
        entries.clear();
        if (slots.empty())
        {
            grow(InitialSlotCount);
        }
        else
        {
            std::fill(slots.begin(), slots.end(), 0u);
        }
    }

    AiUnitDefFacts AiUnitDefIndex::resolve(const std::string& unitType, const GameSimulation& sim, const AiSideUnits& sideUnits)
    {
        std::hash<std::string> hash;
        unsigned int index = static_cast<unsigned int>(hash(unitType)) & (static_cast<unsigned int>(slots.size()) - 1u);
        for (;;)
        {
            const unsigned int occupant = slots[index];
            if (occupant == 0u)
            {
                break;
            }
            Entry& entry = entries[occupant - 1u];
            if (entry.name == unitType)
            {
                return entry.facts;
            }
            index = (index + 1u) & (static_cast<unsigned int>(slots.size()) - 1u);
        }

        // Not one of this pass's types yet, so ask the game's own map -- and
        // let it throw for a type it does not define, exactly as the scan did
        // before this table existed.
        const auto& definition = sim.unitDefinitions.at(unitType);

        AiUnitDefFacts facts;
        facts.definition = &definition;
        facts.buildTime = definition.buildTime;
        facts.isMobile = definition.isMobile;
        facts.builder = definition.builder;
        facts.onOffable = definition.onOffable;
        facts.makesMetal = definition.makesMetal.value > 0.0f;
        facts.commander = definition.commander;
        facts.transport = definition.isTransport();
        facts.canFly = definition.canFly;
        facts.canAttack = definition.canAttack;
        facts.armed = !definition.weapon1.empty() || !definition.weapon2.empty();
        facts.navalCombatUnit = isSideNavalUnit(sideUnits, unitType);
        facts.scoutType = isAiScoutType(sideUnits, unitType);
        facts.antiAirType = isAiAntiAirType(sideUnits, unitType);

        // A quarter load keeps the probe chains to a node or two, and keeps the
        // table a size the CPU holds. Done before the row is pushed, so the
        // rehashing cannot leave `index` pointing into the old table.
        if ((entries.size() + 1u) * 4u >= slots.size())
        {
            grow(static_cast<unsigned int>(slots.size()) * 2u);
            index = static_cast<unsigned int>(hash(unitType)) & (static_cast<unsigned int>(slots.size()) - 1u);
            while (slots[index] != 0u)
            {
                index = (index + 1u) & (static_cast<unsigned int>(slots.size()) - 1u);
            }
        }

        entries.push_back(Entry{unitType, facts});
        slots[index] = static_cast<unsigned int>(entries.size());
        return facts;
    }

    void AiUnitDefIndex::grow(unsigned int slotCount)
    {
        slots.assign(slotCount, 0u);
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            insert(i);
        }
    }

    void AiUnitDefIndex::insert(std::size_t entryIndex)
    {
        std::hash<std::string> hash;
        unsigned int index = static_cast<unsigned int>(hash(entries[entryIndex].name)) & (static_cast<unsigned int>(slots.size()) - 1u);
        while (slots[index] != 0u)
        {
            index = (index + 1u) & (static_cast<unsigned int>(slots.size()) - 1u);
        }
        slots[index] = static_cast<unsigned int>(entryIndex + 1u);
    }
}
