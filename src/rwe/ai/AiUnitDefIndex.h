#pragma once

#include <string>
#include <vector>

namespace rwe
{
    struct AiSideUnits;
    struct GameSimulation;
    struct UnitDefinition;

    /**
     * What the AI's per-tick unit scan needs to know about a unit
     * definition, worked out once per unit TYPE rather than once per unit.
     *
     * Every field here is read out of the definition or compared against the
     * side's own unit names, and none of them can differ between two units of
     * the same type -- which is the whole reason this is a separate thing. The
     * scan asks all of them of every unit it owns on every tick, and the only
     * thing that told it the answers was
     * `sim.unitDefinitions.at(unit.unitType)`: a hash of the type name, a walk
     * of the bucket chain, a comparison against the key, and then a read of
     * fields scattered over a UnitDefinition that is several hundred bytes
     * wide. A game with three hundred units of a dozen types paid that three
     * hundred times to be told twelve things.
     *
     * `definition` is kept because two of the readers want more than this:
     * a builder's draw is estimated from the frame's own definition, which is
     * a question about a different unit type and cannot be answered from a
     * flag.
     */
    struct AiUnitDefFacts
    {
        /** The definition itself, for a reader that wants more than the flags. */
        const UnitDefinition* definition{nullptr};
        /** `definition->buildTime`, which is all `isBeingBuilt` asks. */
        unsigned int buildTime{0};
        bool isMobile{false};
        bool builder{false};
        bool onOffable{false};
        bool makesMetal{false};
        bool commander{false};
        /** `UnitDefinition::isTransport()`: it can carry something. */
        bool transport{false};
        bool canFly{false};
        bool canAttack{false};
        /** It names a weapon in its first or second slot, so it can shoot. */
        bool armed{false};
        /** One of the side's own hulls by name: destroyer, submarine, cruiser, battleship, anti-air ship, scout ship. */
        bool navalCombatUnit{false};
        /** `isAiScoutType`: one of the side's own scouts. */
        bool scoutType{false};
        /** `isAiAntiAirType`: the side's own mobile anti-air. */
        bool antiAirType{false};
    };

    /**
     * Resolves a unit type name to its AiUnitDefFacts, once per type per pass.
     *
     * A small open-addressed table, sized for the handful of types one AI owns
     * and re-used across passes, so that the walk is a hash of a short name and
     * a comparison against a row that is already in cache. A game's definition
     * map is not: the shipped data alone is a hundred and eighty-nine
     * definitions of several hundred bytes each, so every miss in it is a walk
     * into memory the CPU has to fetch.
     *
     * WHY IT IS EMPTIED EVERY PASS AND NOT KEPT. The obvious cheaper thing is
     * to resolve each type once for the whole game, and in a game that is
     * exactly right: the definitions are read at load and never written again.
     * But the facts also answer questions about the SIDE -- the hulls, the
     * scouts, the anti-air are the side's own unit names -- and a test may
     * hand the AI a definition it then edits, which is how half this file's
     * neighbours are written. A table that survived the tick would hand the
     * next one the answer it gave before the edit. Emptying it costs one fill
     * of a few hundred words, and it makes staleness a thing the code cannot
     * express: whatever the index says this pass, it was worked out from the
     * definitions as they stand now.
     *
     * A pass that empties it is `EconomyManager::refresh`, which is the first
     * thing `AiPlayerController::tick` runs. Anything that wants these facts
     * later in the same tick may read them; anything that wants them in a later
     * pass must call `beginPass` itself.
     */
    class AiUnitDefIndex
    {
    public:
        /** Forgets everything resolved so far. Call once at the top of a pass. */
        void beginPass();

        /**
         * The facts for this unit type, resolved and remembered on first ask.
         *
         * Throws `std::out_of_range` for a type the game data does not define,
         * because `GameSimulation::unitDefinitions::at` does, and a scan that
         * quietly invented an answer for a type that is not there would be a
         * different AI rather than a faster one.
         *
         * Returned by value: the table rehashes as it grows, so a reference
         * into it would dunder the next resolve, and this is called once per
         * unit in the pass.
         */
        AiUnitDefFacts resolve(const std::string& unitType, const GameSimulation& sim, const AiSideUnits& sideUnits);

        /** How many types this pass has resolved. For tests and the log. */
        std::size_t resolvedCount() const { return entries.size(); }

    private:
        void grow(unsigned int slotCount);
        void insert(std::size_t entryIndex);

        struct Entry
        {
            std::string name;
            AiUnitDefFacts facts;
        };

        /** Slot contents are one more than the entry index, so that zero is empty. */
        std::vector<unsigned int> slots;
        std::vector<Entry> entries;
    };
}
