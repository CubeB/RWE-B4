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
        /** Level-one fighter: Freedom Fighter, Avenger. The answer to what flies. */
        std::string fighter;
        /** Level-one bomber: Thunder, Shadow. 130 metal that reaches an extractor behind a wall of towers. */
        std::string bomber;
        /** Air constructor: it flies, so no ground has to connect for it to reach a site. */
        std::string airConstructor;
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

        // Naval. §13.2 of docs/ai-architecture-proposal.md says a shipyard
        // "needs a cell on land, adjacent to water deep enough to float what
        // it builds" -- that is wrong against the shipped data. ARMSY/CORSY
        // are 8x8 with MinWaterDepth=30 and a yard map of water down both
        // long edges and across every row between; the yard stands IN the
        // water, not beside it. And the commander and both ordinary land
        // constructors (ARMCK/ARMCV, CORCK/CORCV) already carry it on page
        // two of their build menu, so a construction ship is never required
        // just to start a navy -- it exists to build the things a shipyard
        // cannot: the moho platform, the tidal generator, sonar, and the
        // seabed's own light laser tower and torpedo launcher.

        /** Shipyard. ARMSY 615 metal, CORSY 600, 8x8, MinWaterDepth=30 -- floats in the water it needs rather than standing beside it. Everything else on this list is built from one. */
        std::string shipyard;
        /** Construction ship. ARMCS 255 metal (MinWaterDepth=15), CORCS 260 (MinWaterDepth=30), 4x4. Builds the seabed structures a shipyard cannot, not the shipyard itself -- see above. */
        std::string constructionShip;
        /** Scout ship. ARMPT 100 metal, CORPT 95, 4x4, MinWaterDepth=6 -- the cheapest hull afloat and the shallowest draft, which is what makes it the one that can scout a coastline the others would run aground on. */
        std::string scoutShip;
        /** Destroyer. ARMROY 898 metal, CORROY 887, 4x4, MinWaterDepth=12. The level-one attack ship, and the first hull worth losing. */
        std::string destroyer;
        /** Sea transport. ARMTSHIP 919 metal (20 passenger slots), CORTSHIP 887 (24), 6x6, MinWaterDepth=12, transport size 3 -- what gets a ground army across water it cannot walk around. */
        std::string seaTransport;
        /** Submarine. ARMSUB 1151 metal (3x3), CORSUB 1199 (4x4), MinWaterDepth=20 -- the two do not even share a footprint. */
        std::string submarine;
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
