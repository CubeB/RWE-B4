#pragma once

#include <rwe/sim/SimScalar.h>
#include <string>

namespace rwe
{
    enum class AiDifficulty
    {
        /**
         * Does nothing at all: builds nothing, scouts nothing, never attacks.
         *
         * Not a difficulty so much as a way of getting the computer player out
         * of the way. A skirmish against an Idle opponent gives you a real
         * game with a real second player, real fog and a real commander to go
         * and find, but with nothing happening that you did not cause -- which
         * is what you want when the thing being tested is a shader, a unit, or
         * an interface, and an AI building a base would only be noise.
         */
        Idle,
        Easy,
        Standard,
        Hard,
        Brutal,
    };

    /**
     * Every knob the AI reads. One instance per AI player; the four
     * difficulty tiers are just different sets of values.
     */
    struct AiTuningProfile
    {
        std::string name{"DEFAULT"};
        AiDifficulty difficulty{AiDifficulty::Standard};

        // --- Opening (commander only) ---
        int openingMetalExtractorCount{3};
        int openingSolarCount{4};

        // --- Expansion targets once a factory is up ---
        int targetSolarCount{10};
        int targetMetalExtractorCount{8};
        /**
         * Measured, not guessed: more construction kbots made the AI weaker,
         * monotonically. On the same map and seed, one constructor produced
         * an army of 11 and three produced an army of 1, because each extra
         * builder costs 120 metal and splits an already oversubscribed build
         * budget across one more nanoframe. Build power was never the
         * constraint; metal was.
         */
        int targetConstructorCount{1};
        int targetDefenceCount{2};
        /**
         * Anti-air towers kept whether or not anything has flown over. Cheap
         * insurance: a Defender is 79 metal against a bomber that costs many
         * times that, and the first bomber run arrives before anyone has
         * scouted it.
         */
        int baseAntiAirTowerCount{1};
        /** Anti-air towers wanted once enemy aircraft are actually in the picture. */
        int reactiveAntiAirTowerCount{3};
        /**
         * Mobile anti-air wanted once enemy aircraft are in the picture, and
         * none before. Towers cover the base; these cover what the towers do
         * not, and they cost a factory slot the army would otherwise use.
         */
        int antiAirMobileCount{2};
        int targetRadarCount{1};
        int targetMetalMakerCount{2};
        /**
         * Metal makers switch off below this share of energy storage and back
         * on above the other one. Two marks rather than one because a single
         * threshold makes them flap on and off every tick at the boundary.
         */
        int metalMakerOffBelowPercent{25};
        int metalMakerOnAbovePercent{70};
        int targetAirPlantCount{1};
        int targetVehiclePlantCount{1};
        /**
         * Labs added beyond the first while the metal store is full.
         *
         * A full store is income thrown away, and measured over thirty
         * minutes that is where the AI ended up: both sides at the cap from
         * minute fifteen to the end, the commander lending a hand at the
         * one lab and the one lab unable to spend it. Production, not
         * metal, was the ceiling by then, and another factory is what a
         * player buys with a surplus.
         */
        int surplusLabCount{1};
        /** Dedicated scouts kept alive: planes from the air plant, fast vehicles from the vehicle plant. */
        int targetScoutPlaneCount{1};
        int targetScoutVehicleCount{1};
        /** Air transports built once there is ground the base cannot walk to. */
        int targetTransportCount{1};
        /** Give up on a ferry that has not finished in this many seconds. */
        int ferryTimeoutSeconds{120};

        // --- Site search ---
        /**
         * How far a builder looks for a patch to stand a metal extractor on.
         *
         * Separate from maxMexSearchRadius, which turned out to be doing two
         * jobs: it is also the ring count chooseBuildSite lays every OTHER
         * structure out in, so raising the one number to find further metal
         * would have sprawled the base to match. Painted Desert's nearest
         * unclaimed patches sit at 724, 944 and 1056 world units, all outside
         * the 512 this used to share.
         */
        SimScalar nearMexSearchRadius{1200_ss};
        SimScalar maxMexSearchRadius{512_ss};
        SimScalar expansionMexSearchRadius{2048_ss};
        SimScalar buildSiteGridSpacing{16_ss};
        SimScalar defenceDistanceFromBase{160_ss};

        /**
         * How long a builder will wait for the stockpile to reach the price
         * of the thing it wants most, before giving up on it and building
         * something cheaper instead. While it waits it reclaims.
         *
         * The AI used to start whatever came next and let the economy sort
         * it out, which the economy does by stalling everything in equal
         * measure: measured, it sat committed at two and a half times its
         * income for the first ten minutes and an air plant that takes 39
         * seconds took 183. A player saves up for the expensive things, and
         * so does this.
         */
        int saveUpSeconds{60};

        // --- Cadence (ticks) ---
        int buildPlannerTickInterval{30};
        int threatMapTickInterval{30};
        int scoutTickInterval{60};
        int tacticalTickInterval{15};

        // --- Army ---
        int scoutCount{1};
        /** Attack once this many combat units are at the rally point. */
        int attackArmySize{8};
        /** Fall back to the rally point when the attacking army drops below this. */
        int retreatArmySize{3};
        /** Enemies this close to the base anchor trigger a defence. */
        SimScalar defendRadius{900_ss};
        /** How far from a known enemy an army unit will pick a fight. */
        SimScalar engageRadius{450_ss};
        /** Rally point sits this far from the base anchor, towards the enemy. */
        SimScalar rallyDistance{220_ss};
        /** Weighting of enemy anti-ground threat against economic value when choosing targets. */
        SimScalar threatAversion{1_ss};

        /**
         * When set the controller returns from tick() before doing anything.
         * See AiDifficulty::Idle.
         */
        bool idle{false};

        // --- Cheats (Brutal) ---
        bool cheatModeOmniscient{false};
        SimScalar resourceCheatMultiplier{1_ss};
    };

    AiTuningProfile makeDefaultStandardProfile();
    AiTuningProfile makeDefaultBrutalProfile();
    AiTuningProfile makeIdleProfile();
    AiTuningProfile makeProfileForDifficulty(AiDifficulty difficulty);
    const char* aiDifficultyName(AiDifficulty difficulty);
}
