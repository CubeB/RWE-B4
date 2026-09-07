#pragma once

#include <string>

namespace rwe
{
    struct GameSimulation;

    /** The unit types one side's AI builds, resolved once from the player's side. */
    struct AiSideUnits
    {
        std::string metalExtractor;
        std::string solar;
        std::string lab;
        std::string constructor;
        std::string raider;
        std::string rocketKbot;
        std::string lightLaserTower;
        std::string radar;
        std::string metalMaker;

        // Eyes and lift.
        std::string airPlant;
        std::string scoutPlane;
        std::string airTransport;
        std::string vehiclePlant;
        std::string scoutVehicle;
        std::string tank;

        // Anti-air. Both sides carry a cheap missile tower a constructor can
        // put up (ARMRL "Defender" at 79 metal, CORRL "Pulverizer" at 76) and
        // a level-1 anti-air kbot the first lab can already build (ARMJETH
        // "Jethro", CORCRASH "Crasher"). Nothing here needs a second factory
        // or a tech step, which is what makes answering aircraft affordable
        // at the point the AI first sees one.
        std::string antiAirTower;
        std::string antiAirKbot;

        // Level two. The tech tree is one advanced plant per domain, each
        // unlocked by that domain's own level-one constructor -- the kbot
        // constructor builds the advanced kbot lab, and NOT the commander,
        // whose pages stop at the level-one plants. See §15.2 of
        // docs/ai-architecture-proposal.md, which reads the tree out of the
        // shipped build menus rather than guessing at it.

        /** Advanced kbot lab: the tech step, and the only source of the advanced constructor. */
        std::string advancedLab;
        /** Advanced constructor. 300 metal and 5784 energy, which is the cheap half of the AI's economy. */
        std::string advancedConstructor;
        /** The level-two assault kbot the advanced lab turns out: Zeus, or the much tougher Can. */
        std::string advancedAssault;
        /** Heavy laser tower. Level one can already build it; the AI never has. */
        std::string heavyLaserTower;
        /** Heavy plasma turret: Guardian, Punisher. */
        std::string heavyPlasmaTower;
        /** Advanced radar. 125 metal for several times the coverage. */
        std::string advancedRadar;
        /** Moho extractor. Wants a free patch, so see §15.5 for why it seldom fires yet. */
        std::string mohoExtractor;
        /** Fusion plant, at 5130 metal the one genuinely enormous thing on the list. */
        std::string fusion;
    };

    /** Looks up the side's unit names; anything the game data does not define is left empty and never built. */
    AiSideUnits resolveAiSideUnits(const GameSimulation& sim, const std::string& side);

    /** True for the types the AI keeps for exploring rather than fighting. */
    bool isAiScoutType(const AiSideUnits& units, const std::string& unitType);

    /**
     * True for mobile anti-air, which is held back to cover the base rather
     * than marched off with the army -- an escort that leaves is not cover.
     */
    bool isAiAntiAirType(const AiSideUnits& units, const std::string& unitType);
}
