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
