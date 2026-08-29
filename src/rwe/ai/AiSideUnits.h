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
    };

    /** Looks up the side's unit names; anything the game data does not define is left empty and never built. */
    AiSideUnits resolveAiSideUnits(const GameSimulation& sim, const std::string& side);

    /** True for the types the AI keeps for exploring rather than fighting. */
    bool isAiScoutType(const AiSideUnits& units, const std::string& unitType);
}
