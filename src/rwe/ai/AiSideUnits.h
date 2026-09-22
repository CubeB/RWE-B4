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
        /**
         * Advanced aircraft plant: ARMAAP, CORAAP. What the gunship comes
         * from, and the only reason the AI has to build one.
         *
         * Reached the way the advanced kbot lab is reached, which is not the
         * way the name suggests: page 3 of the LEVEL-ONE constructor's own
         * menu carries the advanced plant of its own kind -- ARMCK3 offers
         * ARMALAB and ARMCA3 offers ARMAAP. So the air constructor the AI
         * already builds is the one unit that can put this up, and no
         * tier-two constructor is needed first. Read out of the shipped
         * menus with ui_probe rather than assumed.
         */
        std::string advancedAirPlant;

        /**
         * Gunship: ARMBRAWL, CORAPE. The air arm's anti-ground body, and the
         * only thing the AI owns that can shoot at a ridge line from above
         * it.
         *
         * The bomber makes one pass at one building. A gunship stands off a
         * target and keeps firing, which is what an army does and what an
         * army stopped by terrain cannot do: reported from a replay on
         * Crystal Maze, where ground units "spend a lot of time attempting
         * to shoot at enemy units through elevation" and the aircraft were
         * "underutilised". LineOfFire.h is the ground arm's half of that
         * answer; this is the other half, and it is the one that does not
         * need the ground to cooperate.
         *
         * Level two, on the advanced plant's page and not the level-one
         * plant's -- checked, because the first version of this named the
         * level-one plant and would have been dead code.
         */
        std::string gunship;

        /**
         * Air repair pad: ARMASP, CORASP. Level two, on page 2 of the
         * advanced constructor's menu.
         *
         * The simulation has done the whole of this for some time and the AI
         * has never built one: a damaged aircraft below three quarters health
         * looks for a pad inside 3840, breaks off whatever it is doing, lands,
         * is mended and goes back to it (`sim/airbase.test.cpp`). Without a
         * pad, every aircraft the AI owns is a one-way trade -- which is most
         * of why its air arm has never been worth what it cost.
         */
        std::string airRepairPad;
        /** Air constructor: it flies, so no ground has to connect for it to reach a site. */
        std::string airConstructor;
        std::string vehiclePlant;
        std::string scoutVehicle;
        std::string tank;
        /**
         * The rest of the level-one line, which the factories make only when
         * the profile gives them a share: the missile truck that outranges a
         * Flash or an Instigator several times over, the medium tank that
         * holds ground where the light one raids, and the plasma kbot that
         * lobs into a crowd.
         */
        std::string missileTruck;
        std::string mediumTank;
        std::string artilleryKbot;
        /**
         * Dragon's teeth: an 11-metal block with 3600 hit points and no
         * weapon, on the construction kbot's, vehicle's and aircraft's third
         * page and not on the commander's. A line of them stops a raider or
         * makes it walk round, which is the whole of what it is for.
         */
        std::string dragonsTeeth;

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
        // just to start a navy.
        //
        // The rest of what this comment used to say was wrong, and the
        // shipped build menus corrected it. They were read out of
        // rev31.gp3, which is what a real load resolves to:
        // CompositeVirtualFileSystem::readFile returns the FIRST match and
        // addToVfs adds the extensions in reverse, so the v3.1 patch
        // outranks ccdata.ccx, which outranks totala1.hpi.
        //
        // The construction ship does NOT gate the seabed structures. Page
        // three of the commander's own menu (ARMCOM3/CORCOM3) carries the
        // tidal generator, sonar, the underwater metal extractor, the
        // floating metal maker and underwater storage; page four
        // (ARMCOM4/CORCOM4) is the torpedo launcher by itself. The
        // commander builds every one of them directly, and ARMCS/CORCS
        // duplicates that list rather than unlocking it.
        //
        // The shipyard claim above is verified and stands: ARMCOM2, ARMCK2
        // and ARMCV2 each carry ARMSY, and ARMCS1 carries it as well.
        //
        // What does NOT follow, and is the constraint that matters here, is
        // that a constructor can build what the commander builds. ARMCK's
        // three pages carry no tidal generator, no sonar and no torpedo
        // launcher, and neither the patch nor the expansion ships an ARMCK
        // menu to add one. The water structures below are the commander's
        // and the construction ship's alone. buildPriorities needs no gate
        // for that -- want() already filters on buildTree.canBuild, so the
        // jobs fall to whichever builder has the button -- but it does mean
        // a base that has lost its commander and has no construction ship
        // will quietly stop wanting any of them.

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

        // The second naval tier. The advanced shipyard is on the construction
        // SHIP's first page (ARMCS1.GUI/CORCS1.GUI) and on nobody else's that
        // the AI owns on a water map -- not the commander's -- so a side with
        // no construction ship never reaches any of this.

        /** Advanced shipyard. ARMASY 2524 metal, CORASY 2460, 8x8, MinWaterDepth=30 -- the shipyard's footprint and draught, so it is sited the same way. */
        std::string advancedShipyard;
        /** Cruiser. ARMCRUS 1719 metal, CORCRUS 1724, 5x5, MinWaterDepth=30. A long gun AND a depth charge, so it is the one surface hull that answers a submarine. */
        std::string cruiser;
        /** Battleship. ARMBATS 4404 metal (MinWaterDepth=30), CORBATS 4181 (MinWaterDepth=15), 6x6. Outranges everything afloat and most of what stands on a shore. */
        std::string battleship;
        // Aircraft for a map with no ground to put an air plant on. It is a
        // chain and every link is the only one: the advanced shipyard builds
        // the advanced construction sub, which alone has the seaplane
        // platform's button (Armacsub1.gui / Coracsub1.gui, Core
        // Contingency), which builds the seaplanes.

        /** Advanced construction sub. ARMACSUB 695 metal, CORACSUB 690, MinWaterDepth=20. */
        std::string advancedConstructionSub;
        /** Seaplane platform. ARMPLAT 2223 metal, CORPLAT 2305, 7x7, MinWaterDepth=30 -- inside a shipyard site's 8x8, so it is sited as one. */
        std::string seaplanePlatform;
        /** Seaplane fighter. ARMSFIG 187 metal, CORSFIG 182. */
        std::string seaplaneFighter;
        /** Torpedo seaplane. ARMSEAP 557 metal, CORSEAP 545. A torpedo, so it can only hurt what is in the water. */
        std::string torpedoSeaplane;

        /** Anti-air ship. ARMAAS 1358 metal, CORARCH 1314, 3x3, MinWaterDepth=30. Three missile mounts and nothing else -- the fleet's only answer to a torpedo bomber. */
        std::string antiAirShip;

        // Water structures, all of them on the COMMANDER's own build pages
        // -- see the note above for why that is not what this file used to
        // say. Values below are out of the shipped FBIs, and the two sides
        // agree on less than you would expect, so neither footprint nor
        // depth may be assumed from the other.

        /**
         * Tidal generator. ARMTIDE 3x3 and 82 metal, CORTIDE 4x4 and 81,
         * both MinWaterDepth=20 and TidalGenerator=1.
         *
         * The case for it on a water map is not marginal. A solar collector
         * is 5x5 and 145 metal (CORSOLAR 141) with MaxWaterDepth=0, so it
         * wants the same dry ground the base, the factories and the
         * extractors are already competing for -- and on a 92% water map
         * there is barely any. The tidal generator is cheaper, smaller, and
         * stands in the water nobody else wants. Nine cells against
         * twenty-five on the Arm side; sixteen against twenty-five on Core's.
         */
        std::string tidalGenerator;

        /**
         * Sonar station. ARMSONAR/CORSONAR, 2x2 and 20 metal both sides,
         * MinWaterDepth 8 and 10 respectively.
         *
         * The cheapest building either side owns, and it MAKES energy (9 and
         * 8) rather than costing any to run. SonarDistance 1180/1223 is the
         * only way the AI can see a submarine at all.
         */
        std::string sonar;

        /**
         * Torpedo launcher. ARMTL 3x3 and 804 metal, CORTL 3x3 and 831 --
         * and MinWaterDepth=1, so it sits in the shallows off a shore rather
         * than out in the deep where the shipyard goes.
         *
         * Expensive enough that it must not be built speculatively: it costs
         * about what a destroyer does, and a destroyer can go somewhere.
         */
        std::string torpedoLauncher;

        /**
         * Underwater metal extractor -- the "moho platform" the note above
         * used to call it. ARMUWMEX 3x3 and 130 metal at MinWaterDepth=19,
         * CORUWMEX 125 at depth 10, and both ExtractsMetal=0.001.
         *
         * That last figure is the point: it is the SAME extraction rate as
         * the ordinary extractor, which is 3x3 and 50 metal. So this is
         * strictly the worse buy -- two and a half times the metal and four
         * times the energy for the same trickle -- and it is wanted only
         * where the cheap one cannot go.
         *
         * Which is far more often than it sounds. Censused across all 52
         * shipped maps, 29 of them have metal under water, and on three the
         * figure is EVERY patch they have: Brain Coral (1170 of 1170), Icy
         * Bergs (1584 of 1584) and Polyp Fields (1080 of 1080). On those the
         * AI cannot extract a gram of metal without this unit. Depths run 47
         * to 85, comfortably past either side's requirement.
         *
         * The other 23 have none at all -- Hundred Isles is 92% water with
         * 531 patches and not one of them wet -- because a Total Annihilation
         * map puts its metal on land unless the designer meant otherwise. So
         * BuildManager counts the submerged patches once and asks that,
         * rather than asking how much water there is: the two are barely
         * related, and Crystal Maze is 3% water with 36 submerged patches.
         *
         * Listed AFTER the ordinary extractor in buildPriorities on purpose:
         * the planner takes the first thing it can find a site for, so the
         * dry patches are taken first and this is reached exactly when they
         * have run out.
         */
        std::string underwaterMetalExtractor;

        /**
         * Floating metal maker. ARMFMKR 3x3 and no metal at all (1480
         * energy), CORFMKR the same for 1530, both MinWaterDepth=11 and
         * EnergyUse=60.
         *
         * The same relationship to metalMaker as the extractor above has to
         * its own: ARMMAKR is also free in metal and burns the same sixty
         * energy a second, but at 687 energy to build and MaxWaterDepth=0 it
         * needs dry ground. So the floating one is the fallback for a base
         * that has none, and is listed after it for the same reason.
         */
        std::string floatingMetalMaker;
        /**
         * Geothermal plant. ARMGEO / CORGEO, on the construction kbot's,
         * vehicle's and aircraft's third page and not the commander's. 250
         * energy for no wind, no tide and no sun -- but only on a vent, and a
         * map has a handful or none.
         */
        std::string geothermal;

        // Storage, and the reactor that goes under the sea. Button pages read
        // out of the shipped guis and download menus, 2026-09-18:
        //
        //   ARMMSTOR 305 metal, +1000 metal    } page one of every level-one
        //   ARMESTOR 240 metal, +3000 energy   } constructor and the commander
        //   ARMUWMS  360 metal, +1500 metal, MinWaterDepth=31   } commander page
        //   ARMUWES  284 metal, +4000 energy, MinWaterDepth=30  } three, hovercraft
        //                                        and seaplane constructors, and the
        //                                        construction SHIP by download menu
        //   ARMUWFUS 7085 metal, 1150 energy, MinWaterDepth=34 -- the advanced
        //            construction SUB only (Armacsub1.gui), as the platform is.
        //
        // Core the same with CORMSTOR/CORESTOR/CORUWMS (depth 40)/CORUWES
        // (31)/CORUWFUS (7210 metal, 1200 energy, depth 15).

        /** Metal storage. */
        std::string metalStorage;
        /** Energy storage. */
        std::string energyStorage;
        /** Underwater metal storage. */
        std::string underwaterMetalStorage;
        /** Underwater energy storage. */
        std::string underwaterEnergyStorage;
        /** Underwater fusion plant. Counts as a reactor wherever the land one is counted. */
        std::string underwaterFusion;
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
