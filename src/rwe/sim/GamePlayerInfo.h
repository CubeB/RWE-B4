#pragma once

#include <optional>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/Metal.h>
#include <string>

namespace rwe
{
    enum class GamePlayerStatus
    {
        Alive,
        Dead
    };

    enum class GamePlayerType
    {
        Human,
        Computer
    };

    /**
     * Whether this machine decides the player's units or applies another
     * machine's results for them.
     *
     * TA is owner-authoritative: each machine simulates the units its own
     * players own and receives the rest as state. A Remote player's units run
     * no local orders, weapons, damage or economy; the results arrive from the
     * owner. Not hashed, so flipping it cannot move a lockstep sync hash, and
     * never produced by a load.
     */
    enum class PlayerSimulation
    {
        Local,
        Remote
    };

    struct GamePlayerInfo
    {
        std::optional<std::string> name;
        GamePlayerType type;
        PlayerColorIndex color;
        GamePlayerStatus status;
        std::string side;

        Metal metal;
        Energy energy;

        Metal maxMetal;
        Energy maxEnergy;

        Metal startingMetal;
        Energy startingEnergy;

        /**
         * Team from the lobby; players sharing one share sight and radar.
         * Nothing means the player has no allies but itself. Last of the
         * positional members so the existing aggregate initialisers, which
         * stop at startingEnergy, keep working.
         */
        std::optional<int> teamId{};

        /** Local by default; set Remote by a mode whose units another machine owns. */
        PlayerSimulation simulation{PlayerSimulation::Local};

        /**
         * The player's storage has a base of its own, startingMetal and
         * startingEnergy, on top of what its finished units hold, whether or
         * not it has a commander. That is the original's rule when bit 0 of
         * `player+0x149` is set (`0x401988`, TOTALA-EXE-ECONOMY.md "Storage
         * and overflow"). A mission sets it (issue #293): a mission player
         * usually has no commander, and without a base the stockpile the
         * schema gives it would be clamped away at the first settle. A
         * skirmish leaves it clear and keeps RWE's older rule, which gives
         * the commander the starting stockpile as its storage instead.
         */
        bool hasBaseStorage{false};

        /**
         * Whether the player has ever had a unit made for it (the dword at
         * record offset 0x140 in the original, BT-193). A seat that never
         * fielded anything blocks victory in a multiplayer game until it does,
         * and counts for nothing in a skirmish. Set by the first unit added
         * for the player; saved and hashed because the win test reads it.
         */
        bool hasCreatedUnits{false};

        /** BT-232: the agreed drop's unit wipe, in simulation time. Saved for replay keyframes. */
        std::optional<GameTime> removeUnitsAt{std::nullopt};

        bool metalStalled{false};
        bool energyStalled{false};

        /**
         * Resource sharing between allies (TOTALA-EXE-COMMANDS.md section 124).
         *
         * `shareMetal` and `shareEnergy` are the player's `+ShareMetal` and
         * `+ShareEnergy` toggles: while one is set, every two seconds the
         * player offers part of what it holds above its reserve to an allied
         * human (ResourceSharing.h). `metalReserve` and `energyReserve` are
         * absolute amounts, not fractions, set by `+SetShareMetal` and
         * `+SetShareEnergy`; a negative one is stored as typed.
         *
         * The original keeps these in a byte of the replicated player record
         * and does not save them, so they reset on a load. RWE saves and
         * hashes them: they steer the simulation, so a lockstep resume or a
         * replay keyframe that forgot them would diverge from the game it
         * came from. Every one has an initialiser because the sync hash
         * reads them from the moment the player exists.
         */
        bool shareMetal{false};
        bool shareEnergy{false};
        Metal metalReserve{0};
        Energy energyReserve{0};

        /**
         * When the player's air base list was last rebuilt, if it ever has
         * been. The original rebuilds it at most every 30 ticks from the
         * player update (0x40B2C0 from 0x465037) and draws `rand(30)` each
         * time; here the rebuild is taken when a pad query finds the last one
         * 30 or more ticks old (TOTALA-EXE-ECONOMY.md S:130). Only the
         * schedule and its draw are kept: the list itself is always read live,
         * so a pad completed or switched on inside the window is offered at
         * once where the original would make the aircraft wait. [open]
         * Hashed and saved, because it decides when the draw is taken.
         */
        std::optional<GameTime> airBasesRebuiltAt{std::nullopt};

        /** Enemy (or, with friendly fire, any) units this player's units have destroyed. */
        unsigned int unitsKilled{0};
        /** Units this player has lost, by any cause. */
        unsigned int unitsLost{0};

        /**
         * Everything the player has ever earned, and everything it earned with
         * nowhere to put it. The end-of-game chart's four middle columns --
         * Energy Produced, Metal Produced, Excess Energy, Excess Metal -- are
         * these four, read out of the player record at `player+0xAC`, `+0xB4`,
         * `+0xCC` and `+0xD4` in the original (0x41DD86-0x41DDBB). Excess is
         * measured where the original measures it: what the storage cap threw
         * away at the end of a second, not what a full bar refused to take
         * during one.
         */
        Metal metalProduced{0};
        Energy energyProduced{0};
        Metal metalExcess{0};
        Energy energyExcess{0};

        Metal desiredMetalConsumptionBuffer{0};
        Energy desiredEnergyConsumptionBuffer{0};

        Metal previousDesiredMetalConsumptionBuffer{0};
        Energy previousDesiredEnergyConsumptionBuffer{0};

        /** Everything the player's units and the player itself earned this second. */
        Metal metalProductionBuffer{0};
        Energy energyProductionBuffer{0};

        Metal previousMetalProductionBuffer{0};
        Energy previousEnergyProductionBuffer{0};

        /**
         * The player's own slice of the economy, for income and spending that
         * belongs to nobody in particular rather than to one of its units. The
         * original keeps an identical block hanging off the player at
         * `player+0xEC` and folds it into the same totals as the per-unit ones.
         */
        Metal metalRequestBuffer{0};
        Energy energyRequestBuffer{0};
        Metal metalDebt{0};
        Energy energyDebt{0};

        /**
         * Books a resource change against the player directly. Income is always
         * taken. Spending is refused while the player-level block still owes for
         * earlier work, on the same rule a unit follows; what is granted is only
         * a claim on the second's income, settled at the end of it.
         */
        bool addResourceDelta(const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal);
        void recordDesire(const Energy& energy);
        void recordDesire(const Metal& metal);
        void acceptResource(const Energy& energy);
        void acceptResource(const Metal& metal);
        bool inResourceDebt() const;
    };
}
