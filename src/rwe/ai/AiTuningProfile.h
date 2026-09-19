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
         * Towers put up at extractor clusters beyond the base's own cover,
         * counted separately from targetDefenceCount: a tower further than
         * defendRadius from the base anchor is an outpost tower.
         *
         * The AI used to expand and defend nothing but the base, so every
         * outlying extractor was a free kill: in the thirty-minute games
         * measured one side lost fifty-four extractors and finished twenty-
         * five, and the sites it lost were ordered again seven and ten
         * times over. Bounded because each is a hundred-odd metal that
         * shoots at nothing until a raid comes.
         */
        int outpostDefenceCount{3};
        /**
         * Extractors a cluster needs before it earns a tower of its own;
         * the cluster with the most gets it first, and a raid counts for
         * this minimum on its own.
         *
         * One looks right -- a lone extractor is what an outpost mostly is,
         * and Painted Desert's outlying patches sit further apart than a
         * tower reaches -- and measured worse. Three towers at 262 metal
         * apiece, planted one to a patch, cost the constructor its middle
         * game: at one the AI finished with 9.9 extractors and was ahead in
         * 3 games of 8, at two with 13.1 and ahead in 6 of 8, never losing a
         * commander in either. The lone patches are cheaper to lose and
         * rebuild than to garrison, and a raid still earns a tower on its
         * own.
         */
        int outpostDefenceMinExtractors{2};
        /**
         * How long a raid on an extractor keeps a tower wanted there. The
         * blackboard remembers a loss for a minute, which serves the
         * replace-what-was-lost rule; the planner only runs for an idle
         * builder, and a builder is rarely idle within a minute of a raid,
         * so the raid was mostly forgotten before anyone could answer it.
         */
        int outpostRaidMemorySeconds{300};
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
         * Makers beyond targetMetalMakerCount, paid for out of energy that is
         * otherwise thrown away: with the store near full and generation
         * running ahead of demand by more than a maker draws (and a quarter
         * again), another is wanted, up to this many, whether or not metal
         * is short. Zero keeps the old rule, two makers and only in a metal
         * stall. arena-analyse.py is what asked for it: a side at its energy
         * cap for 71% of a game, 490 energy a second against 12 metal, with
         * one maker standing.
         *
         * Measured twice at 6 and off both times. ARM against ARM it was
         * inside the noise. ARM against CORE on Great Divide, eight seeds,
         * where CORE's geothermal plant is what makes that surplus, the
         * tuned side came to 41.9 units and an army of 12.4 against 51.4
         * and 17.9 for the untuned side of the same games: the builder
         * time the makers took was worth more than the metal they made.
         */
        int maxSurplusMetalMakerCount{0};
        /**
         * The vehicle plant ahead of the air plant, except where the map needs
         * aircraft. The plan had it the other way round, the vehicle plant
         * waiting on an air plant that itself waits to be affordable: CORE on
         * Great Divide reached its vehicle plant at 18:35 where ARM reached
         * its own at 9:18, in whichever seat it sat.
         *
         * Measured there over ten seeds, each against its own control. The
         * five with CORE tuned: two outright wins and a 79-to-28 lead where
         * the controls had ARM ahead every time, one level game where the
         * control lost its commander, and one the other way. The five with
         * ARM tuned: never worse. So it is on.
         */
        bool vehiclePlantFirst{true};
        /**
         * Past the opening, a solar collector only when energy is wanted:
         * not while the store is four fifths full with generation ahead of
         * demand. targetSolarCount is then a ceiling and not a quota. Six
         * games measured a quarter to a third of ALL energy produced thrown
         * away at a full store, which is several hundred metal of
         * collectors in the first ten minutes that bought nothing.
         *
         * On. Measured in Arm mirror games on Great Divide, eight seeds with
         * their controls, forty-minute cap: the side with it finished on
         * 73.4 units, an army of 27 and 24.8 metal a second against 38, 11.8
         * and 13 for the side without, and died once where the other died
         * three times. maxSurplusMetalMakerCount in the same run was within
         * the noise alone (47.1 units against 44.5) and made this one WORSE
         * when combined with it (56.6 units, army level), so it stays at
         * zero: the cure for wasted energy was not to make it.
         */
        bool solarOnDemand{true};
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
        /**
         * Whether a full metal store buys more of the base: more factories
         * as income grows, the tech step whatever techLevelTwo says, and a
         * fleet past navalFleetSize.
         *
         * Every target above is where the base starts. Watched in two long
         * replays on water maps, it was also where the base stopped: at
         * ninety minutes and at two hours both sides sat on a full store at
         * fifty to a hundred metal a second with idle builders and the same
         * two shipyards they had at minute ten, and neither game ended. A
         * store that stays full is income thrown away, and what a player
         * does with it is build the means to spend it.
         */
        bool surplusExpansion{true};
        /** An armed enemy this close to the commander, seen lately, puts it in danger; so does any loss of hit points. Zero switches commander safety off. */
        SimScalar commanderDangerRadius{450_ss};
        /** Combat units and hulls within this distance of an endangered commander go to it. */
        SimScalar commanderGuardRadius{1500_ss};
        /** Radar contacts count as incoming inside this many defendRadius of the base, when closing. Zero switches the radar warning off. */
        float radarWarningRings{2.5f};
        /** Contacts that must be closing before the army forms up; one blip is a scout. */
        int radarWarningMinContacts{2};
        /** Seconds a hull may fire at one target without hurting it before it is moved to try from somewhere else. Zero switches it off. */
        int navalStalledAttackSeconds{12};
        /**
         * How far from a known armed enemy, or from where a building of ours
         * was lost lately, a builder will still put something down. The
         * extractor search has long refused patches under enemy guns
         * (mexAvoidsEnemyGunsRadius); this is the same caution for
         * everything else a builder is sent to build, and for ground we have
         * just been thrown off. Zero switches it off.
         */
        SimScalar builderAvoidsContestedRadius{450_ss};
        /** Centre-to-centre distance kept between our own shipyards, so what one launches is not launched into the next. A yard is 128 across. */
        SimScalar shipyardSpacing{448_ss};
        /** One more factory of a kind is allowed for every this much metal income a second, while the store is full. */
        int surplusFactoryIncomeStep{20};
        /** And never more than this many of one kind, however rich. */
        int surplusFactoryCap{4};
        /** With the store full, the fleet's size targets are multiplied by this. */
        int surplusFleetMultiplier{3};

        // --- Level two ---
        /**
         * Whether the AI may tech at all. Whether it then does is a separate
         * question, answered per side by techMinArmyValueRatio -- Arm
         * declines, Core accepts.
         *
         * Off, and this is a close call rather than a plain one. Everything
         * about reaching the tier now works: the advanced lab is judged on
         * what the side's own units are worth, every spare builder assists
         * the frame (without which it never finished at all), and the tech
         * step outranks the second factory. Core reaches the Can in every
         * game where it once reached it in none.
         *
         * But it is a poor bargain in the length of game a person actually
         * plays. At the thirty-minute cap the teching side finishes with the
         * smaller army every time. Given ninety minutes it becomes a gamble:
         * eight mirror games gave four wins, three losses and a draw, the
         * teching side was eliminated twice and eliminated nobody, and where
         * it survived it finished with three times the army -- 355 against
         * 108, 315 against 76. That is a real edge bought with a real risk,
         * and the risk lands in the first twenty minutes, which is most of a
         * normal game.
         *
         * So it stayed off until the tier arrived early enough to stop being
         * a gamble. §15.7 says what was left to try.
         *
         * ON since 2026-09-19. What changed is what the tier buys: the moho,
         * the reactor and the factory hold that pays for them came after the
         * measurement above, which priced level two as a Zeus against a
         * Peewee and nothing else. Re-measured on Great Divide, ten seeds,
         * Arm against Core, each with its own control and the alliances
         * alternated: Arm's games were identical to their controls -- its
         * own techMinArmyValueRatio still declines -- and of Core's five,
         * which it lost five times out of five untuned, two became wins
         * with Cans and mohos on the field and three were unchanged because
         * Core was dead before the lab was due. Never worse, in ten games.
         */
        bool techLevelTwo{true};
        /**
         * Metal income before teching is worth considering, per second.
         *
         * Not a full store, which is what this first asked for and what
         * S:14.14 used for the surplus lab. A full store stopped being the
         * signal the moment expansion had somewhere to go: the AI now spends
         * everything it earns on extractors, and metal sat at or above four
         * fifths of storage in eleven samples out of a hundred and eighty,
         * so an advanced lab was never once ordered in eight games. Income
         * is the honest measure of whether the base can carry the tier.
         */
        int techMinMetalIncome{10};
        /**
         * How much better a level-two assault unit must be per metal than
         * the best level-one one before the tier is worth its factory.
         *
         * The two sides are nothing alike here, which is why this is a ratio
         * read off the unit data rather than a decision baked into the
         * build order. Hit points times damage a second, over metal: an Arm
         * Peewee scores 283 and a Zeus 407, so teching buys Arm 1.44 times
         * the army for a 2007-metal lab and does not repay it inside a
         * normal game -- measured, sixteen games, and it lost every variant.
         * A Core A.K. scores 164 and a Can 2800 hit points and 232 damage a
         * second, 1544, so Core gets 9.4 times the army and repays the lab
         * before the first Can is finished.
         *
         * At 1.5, Arm does not tech and Core does, which is what the
         * measurements say each of them should do. It is calibrated to those
         * measurements rather than derived, and a side whose ratio sits near
         * it deserves its own run before the number is trusted for it.
         */
        float techMinArmyValueRatio{1.5f};
        /**
         * How long a builder will save for a level-two building, as against
         * saveUpSeconds for everything else.
         *
         * An advanced lab is 2007 metal, which at the AI's income is three
         * or four minutes of saving, and the ordinary minute-long window
         * rejects it outright -- the planner then skips past it to something
         * cheap, for ever. The projection in canAfford still has to say the
         * saving will actually get there, so a base whose income is all
         * committed elsewhere gives up rather than parking a builder for
         * four minutes on a promise it cannot keep.
         */
        int techSaveUpSeconds{240};
        /**
         * Advanced labs wanted. One is the tech step and the source of the
         * advanced constructor; a second is only worth it once the first is
         * saturated, which the AI cannot yet tell.
         */
        int targetAdvancedLabCount{1};
        /**
         * Advanced constructors kept. The same lesson as
         * targetConstructorCount applies -- build power was never the
         * constraint -- so one, and the level-one constructors carry on
         * with the level-one plan beside it.
         */
        int targetAdvancedConstructorCount{1};
        /** Advanced radar. 125 metal for several times the coverage, so it comes first of the level-two buildings. */
        int targetAdvancedRadarCount{1};
        /**
         * Heavy towers of each kind. The heavy laser is 584 metal and has
         * been reachable by the level-one constructor all along; the AI has
         * never built one because nothing asked for it.
         */
        int heavyDefenceCount{2};
        /**
         * Fusion plants. 5130 metal is minutes of the whole economy, and it
         * is wanted only when energy is actually the thing running out,
         * which with the current maker count is seldom. Expect it late or
         * never until metal makers scale.
         */
        int targetFusionCount{1};
        /** Dedicated scouts kept alive: planes from the air plant, fast vehicles from the vehicle plant. */
        int targetScoutPlaneCount{1};
        int targetScoutVehicleCount{1};
        /** Air transports built once there is ground the base cannot walk to. */
        int targetTransportCount{1};
        /** Give up on a ferry that has not finished in this many seconds. */
        int ferryTimeoutSeconds{120};

        // --- Naval ---
        /**
         * Warships (destroyer, submarine and the scout ship together) the
         * AI keeps once a shipyard stands, on a map where the water is
         * worth a fleet -- MapCharacter::Water in full, and halved on a
         * Mixed map with ground of ours the base cannot reach, since half
         * the fighting there is still on land and a full fleet's metal
         * competes with the army that does it. See BuildManager's
         * navalFleetTarget.
         *
         * This is the kill switch: zero means the naval branch of
         * buildPriorities never fires, no shipyard is ever wanted, and
         * ArmyManager's land army is unaffected either way, because ships
         * were never sorted into it in the first place -- see
         * AiBlackboard::navalCombatUnits. Zero restores the AI to exactly
         * what it built on a water map before a navy existed.
         */
        int navalFleetSize{6};
        /**
         * Shipyards wanted, once the fleet target above is not zero.
         *
         * One used to be the whole navy's factory, and one cannot deliver the
         * fleet this profile asks for. A shipyard's WorkerTime is 100, the
         * same as the kbot lab's, and a single factory gets through roughly
         * 110,000 BuildTime in a whole 1800-second game -- while
         * navalFleetSize=9 with targetSubmarineCount=3 means a Skeeter, a
         * Hulk, six Crusaders and three Lurkers, which is 126,628 BuildTime
         * from the shipped FBI data. The target was out of reach by
         * arithmetic before a shot was fired, and the yard is only standing
         * for the last 40% of the game besides.
         *
         * Two rather than three, measured over twenty 1800s games on Hundred
         * Isles with the seats dealt, one side tuned against the other left
         * alone: destroyers per game-with-a-yard went 0.70 -> 1.82 at two and
         * 0.89 -> 2.09 at three. Two built 18 yards to three's 19 and more
         * submarines (3 against 2), with the same best fleet of five. Three
         * buys about one extra yard in twenty games and meets "cannot afford
         * ARMSY" more often, so the smaller step takes nearly all of it.
         *
         * A land map is unaffected without needing its own gate: the want in
         * buildPriorities is behind navalFleetTarget, which is zero on
         * MapCharacter::Land.
         */
        int targetShipyardCount{2};
        /**
         * Tidal generators wanted on a map where the water is worth
         * something -- the same navalFleetTarget gate the shipyard is
         * behind, so this is dead on a land map and dead when
         * navalFleetSize is zeroed.
         *
         * This is the water map's solar collector, and the shipped numbers
         * are not close. ARMSOLAR is 5x5 and 145 metal (CORSOLAR 141) with
         * MaxWaterDepth=0, so it must have dry ground -- the same ground the
         * base, the factories and every extractor are already competing for,
         * and on a 92% water map there is hardly any. ARMTIDE is 3x3 and 82
         * metal (CORTIDE 4x4 and 81) at MinWaterDepth=20, so it is cheaper,
         * smaller, and stands where nothing else wants to.
         *
         * Only the commander and the construction ship carry the button
         * (ARMCOM3/CORCOM3); no constructor page has it. want() filters on
         * buildTree.canBuild, so the job falls to the commander by itself.
         */
        int targetTidalCount{6};
        /**
         * Sonar stations wanted, behind the same gate.
         *
         * One is almost free: ARMSONAR/CORSONAR are 2x2 and 20 metal, the
         * cheapest building either side owns, and they MAKE energy (9 and 8)
         * rather than costing any. SonarDistance 1180/1223 is also the only
         * way the AI can see a submarine at all -- it already builds
         * submarines and has never been able to see one.
         */
        int targetSonarCount{1};
        /**
         * Torpedo launchers wanted -- and unlike the two above, this one is
         * gated on an enemy hull having actually been SEEN, not merely on
         * the map being wet.
         *
         * ARMTL is 804 metal and CORTL 831, which is a destroyer's price for
         * something that cannot move. Building one because the map is watery
         * is exactly the defence that is not worth what it costs; building
         * one because there are enemy ships in the water is a Guardian's
         * worth of reasoning. MinWaterDepth=1 puts it in the shallows off a
         * shore rather than out where the shipyard goes.
         *
         * Unmeasured: the count and the gate are both first guesses, and the
         * arena has not been run on them yet.
         */
        int targetTorpedoLauncherCount{1};
        /**
         * Whether the shipyard is wanted EARLY on a map where ships matter --
         * directly after the first lab, above the anti-air, the metal maker,
         * the radar, the towers, the advanced lab, the air plant and the
         * vehicle plant -- rather than thirteenth in buildPriorities where it
         * used to sit.
         *
         * The reasoning is that on a map where navalFleetTarget is non-zero,
         * the yard is what the lab is on land: the factory that makes the only
         * units able to reach the enemy at all. Queuing it behind ten solars,
         * eight extractors, radar and two tower types meant the yard the whole
         * naval plan depends on was laid down nearly last. Yard timing is what
         * was left of the fleet shortfall after its other two causes were
         * fixed -- targetShipyardCount making the fleet arithmetically
         * unreachable, and an uncapped water-blind lab outproducing the yards
         * 55 to 1 -- and with both of those fixed the best fleet was still
         * five of nine.
         *
         * It exists as a knob, defaulting to the new behaviour, for one
         * reason: a code change cannot be measured by the arena, because
         * -tune's control arm runs the same binary and would contain the
         * change too. Setting it to 0 restores the old ordering exactly, which
         * makes "-tune earlyShipyard=0" a genuine control measuring this one
         * thing -- the same device, and the same wording, as navalFleetSize=0
         * restoring today's behaviour exactly.
         *
         * No effect on a land map without needing its own gate: the want is
         * behind navalFleetTarget, which is zero on MapCharacter::Land.
         *
         * The late want further down buildPriorities is deliberately kept
         * whatever this is set to. want() de-duplicates, so the pair costs
         * nothing, and the late one still catches the case where the early
         * affordability test -- the OPENING solars and extractors, not the
         * full targets the air plant waits for -- has not been met yet.
         */
        bool earlyShipyard{true};
        /**
         * Land combat units the AI will go on making while there is
         * ground it cannot walk to. Above this every factory that makes
         * them goes quiet -- the kbot lab its raiders and rocket kbots,
         * the vehicle plant its tanks, the advanced lab its assault kbots
         * -- and the income goes to the yards instead. Scouts, builders
         * and anti-air are exempt: they are not the raiding army, and a
         * base that cannot replace a constructor or answer aircraft has
         * been capped into helplessness rather than steered.
         *
         * The lab branch of planFactories is the only production branch
         * with no idea what kind of map it is on: the air plant asks
         * airMatters, the shipyard asks navalFleetTarget, and the lab
         * asks nothing and makes raiders for ever. Measured over twenty
         * 1800s games on Hundred Isles, a 92% water map, with the seats
         * dealt: 1879 land units started against 34 hulls, about 55 to
         * one, on a map where not one of those kbots can reach the enemy.
         * Raising targetShipyardCount lifted hull starts from 5 to 34 and
         * left that ratio almost where it was, because the yards and the
         * lab draw on the same income and only one of them ever stops
         * asking.
         *
         * Gated on hasUnreachableGround rather than on map character, so
         * it is the same signal the ferry and the air transport already
         * read: a land army is worth building right up until there is
         * nowhere for it to walk.
         *
         * Zero -- the default -- is off, and it is off after two separate
         * measurements rather than one. Played at 12 against its own
         * absence, twenty 1800s games a map with the seats dealt, the
         * capped side against the untouched one:
         *
         *   Hundred Isles  (92% water)  hulls 33 v 23, yards 29 v 17,
         *                               died 4 of 20 v 7, income 12.4 v 8.0
         *   Coast To Coast (54% water)  hulls 80 v 51, but army 29.5 v 87.1
         *                               and income 9.8 v 11.2, nobody died
         *   Evad River     (23% mixed)  hulls 22 v 19 -- nothing -- while the
         *                               army fell to 19.4 v 84.3 and losses
         *                               rose to 45.4 v 27.4
         *   The Cold Place (20% mixed)  inert: no unreachable ground in any
         *                               of the twenty, so it never fired
         *
         * So it pays where the water dominates and does not where it does
         * not, and on Evad it bought no fleet at all while costing most of
         * the army. The conclusion that used to end here -- that a default
         * would have to be gated on how much water there is, not merely on
         * whether some ground is out of reach -- is what
         * isolatedLandArmyCapMinWaterFraction is, and the gate stands.
         *
         * THE DEFAULT DOES NOT. Turned on at 12 above that gate on
         * 2026-09-17 and measured the same day, ten games a map with the
         * seats dealt, the capped arm against the same binary uncapped, on
         * Hundred Isles at 92% water -- which is the map the gate exists to
         * admit, so this is the friendliest case it has:
         *
         *   900s    army 6.4 v 16.0, income 10.8 v 10.2, nobody died
         *   1800s   army 16.3 v 32.0, income 13.8 v 12.8, nobody died
         *
         * and in both runs THE HULL COUNTS WERE IDENTICAL: ARMROYx4,
         * ARMSUBx2 and ARMSYx2 in every 1800s game, on both sides of every
         * pair. The cap halves the land army and buys no fleet whatever
         * with the savings, because the fleet was never short of metal --
         * navalFleetSize is 6 and both arms already reach it, so the freed
         * income has nowhere to go.
         *
         * Which is the honest reading of the first table too, in hindsight:
         * what it measured was a fleet starved by YARD TIMING, and the
         * shipyard hoist (44dff154) and targetShipyardCount=2 have since
         * fixed that at the source. The cap was compensating for a bug that
         * no longer exists. Raising navalFleetSize is the lever that would
         * make the freed metal worth having, and until something does, this
         * stays off.
         *
         * The harm this was held back for did not appear: the case where
         * the cap fires while the enemy is still walkable happened in 0 of
         * 40 games across both mixed maps the shipped set has. On Evad the
         * enemy was across water in all twenty anyway; on The Cold Place
         * there was no unreachable ground at all. Rare, not impossible --
         * and with the knob off it cannot bite regardless.
         */
        int isolatedLandArmyCap{0};
        /**
         * How much of the map must be water before the cap above is allowed
         * to fire, as a fraction of the heightmap.
         *
         * This is the gate the measurement above said a default would need,
         * and it is deliberately NOT MapCharacter::Water. That threshold is
         * 0.40 (MixedMapWaterFraction is 0.12, WaterMapWaterFraction 0.40),
         * which would take in Coast To Coast at 54% -- the one map where
         * capping measurably HURT, army 29.5 against 87.1 and income 9.8
         * against 11.2. Reusing the map character here would have shipped
         * the regression the table already found.
         *
         * 0.80 puts the line between the two measured points rather than on
         * either of them: it admits Hundred Isles (92%), Anteer Strait (94%)
         * and Caldera's Rim (98%), and excludes Coast To Coast (54%), Kill
         * The Middle (41%), Evad River (23%) and The Cold Place (20%). Two
         * Continents at 75% falls just outside and is the untested case, so
         * it is excluded rather than assumed.
         *
         * Set it to 1.1 to make the cap unreachable on any map, which is the
         * old zero-default behaviour without having to know what the cap
         * itself was set to.
         */
        float isolatedLandArmyCapMinWaterFraction{0.80f};
        /**
         * Scout ships kept once a shipyard stands. Cheap eyes on the water
         * the way a scout plane is cheap eyes on the ground -- ARMPT is 100
         * metal, the cheapest hull afloat, and the first thing the yard
         * produces.
         */
        int targetScoutShipCount{1};
        /**
         * Sea transports wanted once bb.wantsTransport says a crossing is
         * actually needed -- the same signal TransportManager sets for the
         * air transport, read here rather than guessed at, so a shipyard
         * never speculatively lays one down before there is an army to
         * ferry. Kept as its own count instead of sharing
         * targetTransportCount because the two draw on different factories
         * and answer different crossings: the air plant's answers ground
         * the base cannot walk to at all, this one answers water in the
         * way.
         */
        int targetSeaTransportCount{1};
        /**
         * Construction ships the shipyard makes once the map has metal under
         * its water. 0 leaves the commander to mine the sea by itself.
         *
         * Nothing queued one before this. sideUnits.constructionShip resolved
         * on both sides, and its only reader picked a movement class for the
         * naval reachability layer. On a map with no dry ground there is no
         * lab and so no constructor either, which left the commander as the
         * only builder the AI would ever own: one unit walking the seabed,
         * against Brain Coral's 1170 submerged patches, reaching three to
         * seven extractors in fifteen minutes.
         *
         * Gated on submerged metal rather than on how wet the map is, for the
         * reason the underwater extractor is: the census of all 52 shipped
         * maps showed that the water fraction says little about what lies
         * under it.
         *
         * Measured, and by a wide margin: over ten games on Brain Coral, ARM
         * against ARM with seats alternating, the side building two finished
         * with 32.9 units, 26.3 buildings, 17.1 underwater extractors and
         * 27.4 metal a second, against 20.0, 14.8, 5.4 and 10.2 for the side
         * that did not -- ahead in every one of the ten games.
         *
         * It first measured the other way, and the reason is worth keeping.
         * RWE was not reading the menu entries in the download directory, and the
         * construction ship's second and third pages -- the underwater
         * extractor among them -- are nothing but those. So in RWE the ship
         * had one page and no extractor on it; it found nothing on the plan it
         * could build, and since the planner serves one builder per pass,
         * every pass spent on an idle ship was one the commander did not get.
         * The side building them ended with FEWER extractors, 3.5 to 5.9. It
         * was the menu, not the idea: see DownloadMenus.h. Reported from play,
         * the construction ship "should have three pages of build options."
         */
        int targetConstructionShipCount{2};
        /**
         * Submarines wanted, out of navalFleetSize, once the destroyer core
         * below is standing. A submarine's only weapon is a waterweapon
         * (TOTALA-EXE.md and S:13.2), so it cannot answer anything on land
         * or in the air; it is a specialist added to a fleet that can
         * already fight, not a substitute for one.
         */
        int targetSubmarineCount{2};
        /**
         * Destroyers wanted before a submarine is worth building, so the
         * early fleet is all generalist hulls. A submarine bought before
         * the fleet can already win a surface fight is metal that cannot
         * shoot back at whatever is shelling the coast.
         */
        int submarineMinDestroyerCount{3};
        /**
         * Advanced shipyards wanted, once a shipyard stands and the income
         * below is met. Zero leaves the navy at its first tier, which is what
         * it was before this knob.
         *
         * The reasoning is the land tech step's: a destroyer fleet stops
         * scaling. A cruiser outranges a destroyer and carries the depth
         * charge a destroyer does not, a battleship outranges the torpedo
         * launcher that is the first tier's whole coastal defence, and the
         * anti-air ship is the only thing afloat that can answer a torpedo
         * bomber. Only the construction ship has the button, so this does
         * nothing for a side that has not built one.
         *
         * Measured: ten games on Brain Coral, ARM against ARM over 1500
         * seconds, seats alternating. The side with the yard built it in
         * every game, with 1.3 cruisers and 0.8 battleships, and finished
         * with 44.0 units and an army of 6.6 having lost 30.2, against 35.5,
         * 3.9 and 148.8 lost without it -- on the same metal, 35.7 a second
         * against 37.3. It does not cost the economy; it stops the fleet
         * being fed to the other side a destroyer at a time.
         */
        int targetAdvancedShipyardCount{1};
        /**
         * Whether a construction ship is offered the base's whole plan or
         * only an outpost's -- the extractor, and the advanced shipyard above
         * when its gate is met.
         *
         * OFF, because the arena said so. Ten games on Brain Coral, ARM
         * against ARM over 1500 seconds, seats alternating: with ships kept
         * to extractors a side finished with 44.0 units, 37.6 buildings, 28.1
         * underwater extractors and 52.3 metal a second; offered the whole
         * plan, 34.3, 27.7, 18.4 and 32.6, and lost twice as much. A ship
         * that stops to put up a tidal generator or a second yard is a ship
         * not taking metal, and on that map metal is what everything else is
         * bought with.
         */
        bool navalBuildersPlanForBase{false};
        /**
         * Metal a second before the advanced shipyard is asked for. ARMASY is
         * 2524 metal and the cheapest thing it builds that fights is 1358;
         * below this the yard would stand idle, and the 240-second tech
         * save-up window would starve the first-tier fleet to pay for it.
         */
        int navalTechMinMetalIncome{15};
        /** Cruisers wanted from the advanced shipyard. */
        int targetCruiserCount{4};
        /** Battleships wanted from the advanced shipyard. One is asked for after every two cruisers, so the escort exists before the thing it escorts. */
        int targetBattleshipCount{2};
        /** Anti-air ships wanted, and only once enemy aircraft have been seen -- the same rule the anti-air kbot and the fighter follow. */
        int targetAntiAirShipCount{2};
        /**
         * Seaplane platforms wanted once an advanced shipyard stands and
         * there is no air plant -- the map had no ground for one. Zero
         * switches the whole seaplane chain off, the construction sub with
         * it. About 2900 metal before the first aircraft, so it waits for a
         * full store like the rest of surplusExpansion.
         */
        int targetSeaplanePlatformCount{1};
        /**
         * Hold the factories while the first moho and the first reactor are
         * paid for. Level two was reached in play and never spent: the lab
         * went up, the factories went on taking every unit of metal as it
         * arrived, and a 1508-metal moho was skipped as unaffordable on 26
         * metal a second for the rest of the game. With this on, once a
         * builder that can build them stands, factories make only builders
         * until both exist -- unless the base is under attack, the army is
         * under tierTwoReserveMinArmySize, or tierTwoReserveMaxSeconds have
         * gone by, so a map with no patch left cannot hold production for
         * ever.
         */
        bool tierTwoEconomyReserve{true};
        int tierTwoReserveMinArmySize{6};
        /**
         * Whether the hold also covers the advanced lab itself, from this
         * many seconds into the game (zero: it does not). With teching
         * allowed the lab was still not ORDERED until the half hour, for the
         * moho's reason -- 2007 metal is never affordable while the
         * factories spend income as it arrives -- and a tier that arrives at
         * minute thirty is the gamble techLevelTwo's note describes.
         */
        int tierTwoReserveCoversLabAfterSeconds{0};
        int tierTwoReserveMaxSeconds{480};
        /** Underwater fusion plants wanted once an advanced construction sub can be had. Zero switches them off. */
        int targetUnderwaterFusionCount{1};
        /**
         * Storage wanted, metal and energy each, counting the underwater
         * kind with the land kind. One of each goes up with level two --
         * a commander's 1000 metal cannot hold the price of anything the
         * tier sells -- and the rest only when the store is found full,
         * which is income being thrown away. Zero switches storage off.
         */
        int targetMetalStorageCount{2};
        int targetEnergyStorageCount{2};
        /** Fighters are matched to the most enemy aircraft seen at once, up to this many. targetFighterCount is the floor. */
        int maxReactiveFighterCount{8};
        /** Torpedo seaplanes wanted from it. They fly in pairs at least, as the bombers do. */
        int targetTorpedoSeaplaneCount{6};
        /**
         * Hulls that will call the attack on their own, with no land army.
         *
         * armySize counts combatUnits, and warships are deliberately kept
         * out of those: every gather/attack/raid rule in ArmyManager is
         * written against combatUnits, and a hull in there would be rallied
         * and marched at a land target. The cost of that decision is this --
         * a side whose whole strength is afloat never leaves Boom, and the
         * phase is what gates the army ferry, so on an island map the ferry
         * cannot run however many ships are standing.
         *
         * Folding hulls into armySize was the alternative, and is rejected:
         * it would change what the anti-air and destroyer tests pin, it
         * needs both of armySize's write sites changed (EconomyManager and
         * ScoutManager), and it contradicts what navalFleetSize's own
         * documentation promises about the land army being unaffected.
         *
         * A borrowed naval scout does not count towards this, for the same
         * reason armySize subtracts the land scout: eyes are not strength,
         * and an AI that counted them would attack earlier for having built
         * them.
         *
         * Zero -- the default -- is exactly the behaviour before this knob
         * existed, and it stays there because the arena could not exercise
         * it. Twenty 1800s games on Hundred Isles, ten with it at 3 and ten
         * with it off: both players reached Boom -> Attack in 10 games of 10
         * in BOTH runs, and in neither run was there a single game where a
         * player failed to reach Attack. On that map armySize crosses
         * attackArmySize unaided, both seats having land to fight on, so the
         * condition this widens is never the binding one. Raising it wants a
         * map where a side really is fleet-only, which the shipped set may
         * not contain: of seventeen maps probed, nine are 0% water and six
         * are above 40%, and even at 92% both sides field a land army.
         */
        int attackNavalSize{0};
        /**
         * How many hulls, not counting the one lent to scouting, before the
         * fleet goes looking for something to sink. 0 keeps it at home.
         *
         * Deliberately separate from attackNavalSize above, which is a term
         * in the whole AI's phase decision: raising that to make the navy
         * fight would flip a mixed map's LAND army into Attack as a side
         * effect of having built three ships. The navy judges its own
         * readiness and leaves the phase alone.
         *
         * This exists because until it did, `updateNavy` had no offensive
         * branch whatever -- a hull shot what came within engageRadius and
         * otherwise held station at the shipyard, so a navy that was never
         * met at home never fought at all. Watched on Brain Coral: the yard
         * turns out warships and they sit beside it for the rest of the
         * game while the enemy does as it likes.
         *
         * Three is a first guess and has not been through the arena. It is
         * the smallest number that is a fleet rather than a scout, and small
         * enough to matter on a map whose economy supports few hulls.
         */
        int navalAttackFleetSize{3};

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
        /**
         * The ring count chooseBuildSite lays every structure out in -- so
         * this is a cap on how far the base may sprawl, not on how far metal
         * is looked for (nearMexSearchRadius above does that job).
         *
         * Stays at 512. Raising it outright was tried and withdrawn: see
         * buildSiteFallbackRadius below, which fixes the case that needed
         * fixing without charging every map for it.
         */
        SimScalar maxMexSearchRadius{512_ss};
        /**
         * How far the site search is allowed to reach when the normal budget
         * above turns up NOTHING -- not even a crowded site.
         *
         * 512 is sixteen tiles, and on an island start that disc frequently
         * had no room for a 6x6 lab. The planner takes the first want it can
         * site, so a lab with nowhere to stand meant no lab, no factory, no
         * army, and a side that never left Boom -- while the shipyard, the
         * one siting that scans MapIntel globally rather than a ring, got
         * built anyway. That read as an AI preferring a navy. It was an AI
         * that could not fit a factory.
         *
         * Raising maxMexSearchRadius to 2048 fixed it and was measured, 20
         * games at 1800s, each slot run both ways. On Hundred Isles it took
         * army 65 against 5, and 30.2 against 3.4 swapped. But on Painted
         * Desert the swap refused it: in one slot the wide setting took 57.6
         * units and army 22.2 against 66.6 and 24.8 and lost two games
         * outright, while in the other the two were a wash -- about 5% worse
         * on land, averaged. The unswapped land run alone had looked fine,
         * which is exactly why it was swapped.
         *
         * So the widening is conditional instead. A base with room never
         * reaches it, because the normal scan succeeds first; the cramped
         * island start does. It also bounds the cost of the deeper scan to
         * the case where the cheap one already failed. Set equal to or below
         * maxMexSearchRadius to switch it off entirely.
         */
        SimScalar buildSiteFallbackRadius{2048_ss};
        /**
         * How far from the base a constructor will go for a patch, nearest
         * first, once nothing near it is free.
         *
         * This was 2048, and it is why the extractor count sat at eight to
         * ten from the seventh minute to the sixteenth in every thirty-minute
         * game measured on Painted Desert: the eleven patches inside 2200
         * of the start were all taken by then, the next ring sits at
         * 2400-3200, and the only builder that ever reached one was a
         * constructor that happened to be standing far enough out for its
         * own near search to catch it. The bases there are 8260 apart, so
         * 4096 reaches the midfield and no further; the side test below
         * keeps it out of the enemy's half.
         */
        SimScalar expansionMexSearchRadius{4096_ss};
        /**
         * How far from home the commander will go for a patch, in either
         * search. It is the game, and it is planned first because it is
         * unit zero, so without a leash of its own it takes the far patches
         * the constructors are for -- and the near search is from wherever
         * it stands, so it chains outward from each one to the next.
         * Measured without this, both sides' commanders were building
         * extractors 3300-3900 from their start in the midfield, and the
         * one that was caught there lost the game with it. Painted Desert's
         * eleven start-area patches lie within 2200; this reaches the ten.
         */
        SimScalar commanderMexSearchRadius{1600_ss};
        /**
         * A patch this close to a known armed enemy on the ground is not
         * taken. The nearest free patch is still the nearest free patch
         * after the frame on it has been shot, so without this a builder
         * puts the same frame down again and again: measured, one
         * commander ordered one site 232 times in five hundred seconds,
         * each frame living a second or two. A light laser tower reaches
         * 430; this covers it with a little over. Zero switches it off.
         */
        SimScalar mexAvoidsEnemyGunsRadius{450_ss};
        /**
         * Whether the expansion search may only take patches on ground the
         * AI has explored or has under radar.
         *
         * Off, because a player is shown every metal spot on the map from
         * the start and the AI is being held to a stricter standard than
         * the human it plays. Measured with it on, the plateau above lasted
         * until the scout plane -- which the AI does not build until its
         * eighth extractor is up -- had flown over the next ring of
         * patches. Kept as a knob so the two can be played against each
         * other.
         */
        bool expansionNeedsExploredGround{false};
        /**
         * Whether the expansion search stays on our side of the map once
         * the enemy's base has been found: a patch nearer theirs than ours
         * is left alone. A player does not send a constructor to claim the
         * patch outside the enemy's front door, and with the radius above
         * reaching the midfield something has to say so. Nothing is ruled
         * out before the enemy is found: an earlier version also kept off
         * patches nearer any other declared start position, and on Crystal
         * Maze, which declares ten, that discarded 405 of the 468 patch
         * cells in reach and held the AI to twelve extractors against the
         * other side's fifty.
         */
        bool expansionStaysOnOurSide{true};
        /**
         * Whether the commander is judged by its OWN movement class, rather
         * than the constructor's, when asking what ground it can reach.
         *
         * On, because it is the truth: ARMCOM's TANKDS2 wades to water depth
         * 100 and climbs slope 32 where ARMCK's TANKSH2 stops at 12 and 15,
         * and the ground labelling is built for the constructor. Measured
         * off, on Hundred Isles over twenty games: 504 of the 531 patch
         * cells in range refused as unreachable, three extractors, and a
         * commander idle for two thirds of the game. The one side that got
         * out did it by accident -- the shipyard is the only build whose
         * site search is not bounded by a radius, so it walks the commander
         * to the coast -- and was then classified as a stranded outpost
         * builder, so it took 26 patches and still never built a factory.
         * Off restores both of those behaviours, which is what makes it
         * worth keeping as a knob.
         */
        bool commanderUsesOwnReachability{true};
        /**
         * Whether a warship may be borrowed to go and find the enemy while
         * none is known.
         *
         * On, because on an island map nothing else can: measured on Hundred
         * Isles with it off, both sides ended every one of twenty games with
         * `known enemies 0`, which holds the AI out of the Attack phase
         * however large its army grows -- 15 to 17 units against a threshold
         * of 8 -- and so keeps the army ferry switched off for the whole
         * game. The cost is one hull doing something other than fighting,
         * and only until the enemy is found.
         */
        bool navalScouting{true};
        SimScalar defenceDistanceFromBase{160_ss};
        /**
         * How far out from the base anchor the radar's post stands, towards
         * the enemy. A radar in the middle of the base sees what the
         * buildings already see; its worth is the ground beyond them, so it
         * goes on the far side of the buildings, which on the maps measured
         * stand within about 300 of the anchor.
         */
        SimScalar radarDistanceFromBase{320_ss};
        /**
         * Site defences by what they would cover rather than on the nearest
         * free ring. Measured on Painted Desert, the nearest-ring rule put
         * three laser towers within 64 units of each other and two
         * Defenders side by side: each new tower went where the last one
         * had, because the ring around the same post was still the nearest
         * free ground. Off, the old rule; a knob so the arena can play the
         * two against each other.
         */
        bool spreadDefences{true};
        /**
         * Face the next tower towards where our buildings have actually
         * been lost from lately (bb.recentLosses, weighted by age within
         * the existing LossMemoryTicks window) in preference to
         * enemyBasePosition.
         *
         * chooseDefenceSite used to push every non-anti-air tower towards
         * enemyBasePosition regardless of where an attack had actually come
         * from, which docs/ai-architecture-proposal.md S:13.3 flags by name
         * and says recentLosses "was built with it in mind" for. That
         * target is also frequently unset -- PerceptionManager clears
         * enemyBasePosition every refresh and only sets it again once an
         * enemy building is actually in sight, so it reads empty for the
         * whole opening and again whenever contact is lost -- and its
         * fallback is the world origin, not a real threat direction. A raid
         * that lands on the flank now tilts the post that way instead;
         * false restores the previous behaviour exactly, and is also what
         * this falls back to of its own accord whenever there is no loss
         * inside the window, which includes every opening.
         */
        bool defenceFacesRecentLosses{true};
        /**
         * The most a tower may cost against the current metal income before
         * it is judged not worth its metal, expressed as seconds of that
         * income -- a light laser tower at 90 seconds of a ten-a-second
         * economy is 900 metal's worth of allowance against a much cheaper
         * building, so this is deliberately generous and meant to catch a
         * starved economy rather than veto an ordinary one.
         *
         * Reasoned rather than measured: nothing here has been played, so
         * treat the actual number as a starting point for the arena rather
         * than a settled answer. Zero switches the test off -- the count
         * thresholds and metalShort are all that gate a tower, which is
         * every behaviour before this knob existed. A reading of zero
         * income (nothing produced yet, or a test rig that never modelled
         * any) is treated as unmeasured rather than as "no income", so the
         * gate does not fire before there is anything to judge it against.
         * A tower answering an actual raid -- outpostDefencePlan::raided, or
         * an armed enemy already inside defendRadius -- bypasses the test
         * outright, the same way those already bypass metalShort: this
         * knob is about declining a speculative tower, not about refusing
         * to rebuild one that was just shot down.
         */
        int defenceValueMaxPaybackSeconds{90};
        /**
         * Extra payback seconds an outpost tower earns per extractor the
         * site would cover, added to defenceValueMaxPaybackSeconds. A
         * tower guarding three extractors is worth more than one guarding
         * a single lonely patch, and without this the value test could not
         * tell them apart.
         */
        int outpostDefenceValueSecondsPerExtractor{45};

        /**
         * How many combat units are detached to stand over a builder
         * placing something away from the base -- an outpost tower is the
         * usual case, since nothing else ever went there with it. Modelled
         * on raidPartySize, except a guard is sent even under strength: it
         * stands rather than walks into a fight it chose, so a guard of one
         * is still worth having where a raid of one was called a gift.
         * Zero switches the whole feature off.
         *
         * **Off by default, because it was measured and it costs more than
         * it saves.** Painted Desert, ten games, each slot run both ways:
         * with the guard off a side took 71.7 units and army 27.2 against
         * 44.6 and 15.1 with it on, and 76.9 and 34.5 against 68.0 and 29.0
         * in the other slot -- worse in both. The release reasons say why:
         * 69 of 81 guards ended with the builder having simply finished and
         * only 6 with it lost, so two units were being taken off an army
         * averaging 7.4 to escort jobs nothing was threatening.
         *
         * The mechanism is kept rather than deleted because the idea is
         * right and the trigger is what is wrong -- it is raw distance, with
         * no notion of danger. Triggering on the influence map instead wants
         * the ThreatMap threaded into BuildManager, which is the one manager
         * that does not receive it. Set this to 2 or 3 to play the old
         * behaviour against its own absence.
         */
        int buildSiteGuardSize{0};
        /**
         * How far a build site has to be from the base anchor before the
         * builder placing something there is offered a guard. Short enough
         * that an outpost beyond the base's own cover always qualifies;
         * long enough that an ordinary building going up beside the lab
         * does not draw a combat unit off the rally point for nothing.
         */
        SimScalar buildSiteGuardMinDistance{900_ss};
        /**
         * Enemy anti-ground damage per second that has to be able to reach a
         * build site before the builder placing something there is offered a
         * guard, on top of the distance test above.
         *
         * Zero switches the test off and restores the distance-only trigger
         * exactly, which is both the documented kill switch and the control
         * arm: the guard's own audit measured it COSTING games with distance
         * alone -- 71.7 units and 27.2 army with it off against 44.6 and 15.1
         * with it on, repeated in the other slot -- because 69 of 81 guards
         * ended with the builder simply finished and only 6 with it lost. The
         * mechanism was kept on the grounds that the idea is right and the
         * trigger is wrong; this is the trigger the entry asked for.
         *
         * Defaults to off, like the size knob it depends on, so nothing moves
         * until it has been played against its own absence. To measure, set
         * buildSiteGuardSize to 2 or 3 and run this at 0 and at a DPS floor.
         */
        float buildSiteGuardThreat{0.0f};
        /**
         * How far around a build site to look for that damage. Wide enough to
         * catch something already on its way rather than only what is standing
         * on the spot -- a guard summoned once the raider has arrived is too
         * late to be worth the two units it costs.
         */
        float buildSiteGuardThreatRadius{600.0f};
        /**
         * How long a guard will stand over a site with nothing to show for
         * it before it gives up and rejoins the reserve -- the builder
         * changed its mind, got stuck, or the order was dropped and never
         * reissued. Long enough to cover an ordinary build, short enough
         * that an abandoned request does not tie up part of the army for
         * the rest of the game.
         */
        int buildSiteGuardTimeoutSeconds{180};

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
        /**
         * How long a site is left alone after a builder's order to it was
         * dropped -- unreachable, or occupied when it got there. The site
         * is still the best one by every other test, so without a memory
         * it is handed straight back: a commander on Crystal Maze was given
         * one walled-in extractor site fifty-four times and built nothing
         * for twenty minutes on a full store. Long enough for whatever was
         * standing there to have moved; the ground itself does not change.
         */
        int failedSiteMemorySeconds{120};
        /**
         * Whether a builder with the moho's button replaces a standing
         * extractor once there is no free patch left to put a moho on. One at
         * a time, and see BuildManager::ExtractorUpgrade for why. A moho on a
         * free patch is always preferred: it costs the base no income while
         * it goes up.
         */
        bool extractorUpgrades{true};
        /**
         * How much of the moho's price must already be in the store before
         * the old extractor is taken down. The patch earns nothing from the
         * reclaim until the moho finishes, so the reclaim waits until the
         * moho can follow it promptly rather than when it is merely wanted.
         */
        float extractorUpgradeMinMetalFraction{0.5f};
        /** How long an upgrade may run from the reclaim order to the moho's build order before the patch is released. */
        int extractorUpgradeTimeoutSeconds{180};

        // --- Cadence (ticks) ---
        int buildPlannerTickInterval{30};
        int threatMapTickInterval{30};
        int scoutTickInterval{60};
        int tacticalTickInterval{15};

        /**
         * How long a contact stays a target after it was last seen, in ticks.
         *
         * The original's computer player and its weapons read the same list:
         * `0x40AA40` rebuilds the enemy list once per player through the
         * can-see predicate `0x465AC0`, and both the acquisition scan and the
         * missions walk that (TOTALA-EXE.md S:10). RWE's blackboard instead
         * remembers a contact until the AI is standing where it last saw it,
         * which is right for deciding where to go and wrong for deciding what
         * to shoot: it let an army keep firing at a unit its side had long
         * lost. Five seconds, so a target that steps behind a hill for a
         * moment is not dropped mid-burst.
         */
        int targetMemoryTicks{150};

        // --- Army ---
        int scoutCount{1};
        /** Attack once this many combat units are at the rally point. */
        int attackArmySize{8};
        /** Fall back to the rally point when the attacking army drops below this. */
        int retreatArmySize{3};
        /**
         * What the level-one kbot lab makes, as a ratio: this many raiders to
         * that many rocket kbots. Two integers and not a fraction because the
         * choice is made inside the simulation, where a float comparison is
         * one more thing that has to come out the same on every machine.
         */
        int labRaiderShare{2};
        int labRocketKbotShare{1};
        int labArtilleryKbotShare{0};
        /** The same for the vehicle plant: light tank, missile truck, medium tank. */
        int vehicleTankShare{1};
        int vehicleMissileTruckShare{0};
        int vehicleMediumTankShare{0};
        /**
         * Units built during an attack gather at the rally point for the
         * next wave instead of walking to the front one at a time. The
         * attack is over when the wave that set out has fallen below
         * retreatArmySize, not when the whole army has -- the reinforcements
         * waiting at home are the next wave, not a reason to keep this one
         * going.
         */
        bool attackInWaves{true};
        /**
         * With more armed intruders near the base than we have combat units,
         * hold at the rally point and fight what comes within reach, rather
         * than charging the nearest one. Two kbots sent at a raiding party
         * of nine are two kbots lost; kept, they are the start of the wave
         * that answers it.
         */
        bool holdWhenOutnumbered{true};
        /**
         * With no combat units at all, the commander answers up to this many
         * armed intruders near the base itself. 0 switches it off.
         *
         * Defend picks the intruder nearest home and hands it to the wave,
         * and the wave is drawn from combatUnits. Where every hull dies as a
         * nanoframe before it can float -- Brain Coral, where one enemy scout
         * ship parks off the shipyard and shoots each one as it is born, and
         * a frame has no hit points to lose -- that list is empty, so the
         * phase fires exactly as designed and sends nobody. Measured over ten
         * games: the victim entered Defend at tick 3523 and never left it,
         * while the ship shooting it survived to the final tick, unopposed
         * for thirteen minutes. Everything built to break the siege died at
         * nought hit points on the slipway.
         *
         * The commander is the one unit that lives through that, and it is
         * armed. It is kept out of combatUnits deliberately -- it is a
         * builder, and an AI that walks its commander at every raider loses
         * it -- so this is the narrowest case that breaks the lock: only when
         * there is nothing else whatever to send. Where the AI has any army
         * it is dead code.
         *
         * Counted rather than a flag because the natural guard cannot be
         * reused: holdWhenOutnumbered compares intruders against combatUnits,
         * which is empty here by construction, so that test is always true
         * and would make this never fire. A lone harasser is worth the
         * commander's attention; a raiding party of nine is a game already
         * lost, and walking the commander into it only loses it faster.
         *
         * No leash is needed. The intruder comes from enemiesNearBase, which
         * PerceptionManager already bounds by defendRadius.
         */
        int commanderDefendsAloneMaxIntruders{1};
        /**
         * While a wave is out, an intruder at home is answered by the units
         * gathering for the next wave, and the wave carries on. Off, one
         * scout at the extractors puts the AI in Defend and recalls the lot,
         * which is what used to happen: a wave that had nearly reached the
         * enemy turned round and walked home.
         */
        bool reserveAnswersIntruders{true};
        /**
         * Notice that a production site is being shot up, and stop feeding
         * it.
         *
         * A nanoframe spawns with zero hit points, so any damage at all
         * kills it. One enemy scout ship parked off a shipyard therefore
         * destroys every hull the yard makes, for the whole game, at a
         * hundredth of what it costs us -- and before this the AI could not
         * see it happen: recentLosses is diffed from standingBuildings and
         * so holds buildings only, and enemiesNearBase was measured from
         * the base anchor rather than from the thing being shot. The
         * diagnosis is ROADMAP Phase 2, the all-water entry, and commit
         * 42160d59.
         *
         * On it, three things follow. The AI remembers frames it lost where
         * they were born (bb.recentUnitLosses); an armed enemy sitting on
         * such a place counts as an enemy at the base, so everything hanging
         * off enemiesNearBase -- the Defend phase, the intruder answer, the
         * urgent tower -- fires for it; and the besieged factory's queue is
         * not topped up while the gun is still there, which stops the
         * economy being poured a frame at a time into a scout ship's guns.
         *
         * Off restores exactly the old behaviour, which is what the arena
         * measures it against.
         */
        bool noticeProductionHarassment{true};
        /**
         * How near a factory a frame has to have died, and how near an armed
         * enemy has to be sitting now, for that factory to count as
         * besieged.
         *
         * One radius for both halves because both are asking about the same
         * few hundred units of water: a hull is born on the yard's own
         * footprint, and the gun that kills it is inside its own weapon
         * range of that. 400 covers an 8x8 shipyard's footprint (128) plus
         * the reach of the light guns that do this -- a CORPT's is well
         * inside it -- without taking in a fight happening elsewhere in the
         * base.
         */
        SimScalar productionHarassRadius{400_ss};
        /**
         * Send the commander at whatever is besieging a production site,
         * when nothing else of ours can answer it.
         *
         * The third candidate from the diagnosis: the commander is armed, it
         * survives what kills the frames, and while the yard is feeding a
         * gun it is usually idle. Off by default because it has not been
         * measured, and because the map this is for is the one where it is
         * most likely to go wrong -- a commander sent at a hull in deep water
         * is a commander walking towards something it cannot reach. It is
         * leashed to engageRadius and only fires when the army and the fleet
         * have nothing that can go instead.
         */
        bool commanderAnswersHarassment{false};
        /**
         * Units built while a wave is out join it once this many have
         * gathered, rather than waiting for the wave to be spent. This keeps
         * what attackInWaves was for -- a lone kbot does not walk to the
         * front by itself -- without its cost: the wave is only over when it
         * has fallen below retreatArmySize, so until then every unit built
         * stood at the rally point while the wave died at the enemy's towers.
         */
        int reinforcementGroupSize{3};
        /**
         * Send a detachment at the enemy's undefended expansion instead of at
         * the main target. An AI that only ever walks at one place leaves the
         * enemy free to mine a whole flank of the map uncontested, and gives
         * it no reason to keep anything at home.
         */
        bool raidingParties{true};
        /** How many units go on a raid. */
        int raidPartySize{3};
        /** A raid target must be at least this far from the enemy's base. */
        SimScalar raidAvoidBaseRadius{900_ss};
        /**
         * Fighters and bombers the air plant keeps on hand. The plant has
         * only ever built one 40-metal scout and then stood idle for the rest
         * of the game, which is 850 metal of factory doing nothing; a bomber
         * is the cheapest thing either side owns that can reach an extractor
         * behind a wall of towers.
         */
        int targetFighterCount{2};
        int targetBomberCount{4};
        /** How far a fighter will chase something before it is called home. */
        SimScalar fighterLeash{2400_ss};
        /**
         * What a bomber will fly into. A bombing run is a trade: an aircraft
         * for a building. Sent at the dearest thing the enemy owns, it flies
         * at whatever stands deepest inside their anti-air and is traded for
         * a fraction of one. It goes instead at what is lightly covered or
         * not covered at all -- the outlying extractor, the army with a
         * Crasher or two -- which is also the thing whose loss they feel,
         * because it is the thing they did not think needed cover.
         */
        int bomberMaxAntiAirCover{2};
        /**
         * Except when they are at the door. An enemy this close to the base
         * anchor is bombed whatever is covering it: an aircraft is worth less
         * than the base is.
         */
        SimScalar bomberHomeDefenseRadius{900_ss};
        /** How near a candidate other enemy units count as the same army. */
        SimScalar bomberClusterRadius{300_ss};
        /** How many have to be standing together before an army is worth a sortie. */
        int bomberMinClusterSize{3};
        /**
         * Air constructors. One is worth having on any map: it flies, so no
         * ground has to connect for it to reach a patch, and the expansion
         * that a walking constructor cannot get to is exactly the one nobody
         * is contesting.
         */
        int targetAirConstructorCount{1};
        /**
         * How far from our own units a builder will go to reclaim wreckage.
         * The base's own rubble is already worked; this is the battlefield,
         * where the wrecks of the last two waves stand in a wall that blocks
         * movement and absorbs every shot fired at the enemy behind it. A
         * player clears that wall and takes the metal; measured, the AI stood
         * and fired into it instead. Only where enough of our own army is
         * standing, which is what makes it survivable.
         */
        SimScalar battlefieldReclaimRadius{700_ss};
        /** Our combat units that must be near the wreck before a builder is sent to it. */
        int battlefieldReclaimEscortCount{4};
        /** How many wrecks a builder is given at once when it is sent to clear a field. */
        int battlefieldReclaimBatch{6};
        /**
         * How far ahead of the wave's own centre a unit may get before it is
         * sent back to it. Every member of a wave is given the same
         * destination and paths to it alone, so a wave of mixed speeds
         * arrives as a column: the fastest three walk into the enemy army by
         * themselves and die, then the next three. Arriving as one body is
         * what makes two armies meet head on rather than feed each other a
         * unit at a time.
         */
        SimScalar waveCohesionRadius{0_ss};
        // Ships off. Measured over two sixteen-game runs it decided one game
        // of sixteen either way it was written -- walking the leader back to
        // the wave, and standing it still -- against three with the rule off
        // and three to seven for every other build tried. A wave member that
        // cannot advance at all, blocked by the wreck wall or by a path that
        // failed, holds the centre back for good and the rest wait for a unit
        // that never arrives. Kept as a knob and off by default, the way
        // techLevelTwo is.
        /**
         * An enemy army this near the wave's centre is what the wave fights,
         * instead of walking past it to the base it was pointed at. Both
         * sides choose the other's base and set off, so without this they can
         * trade bases without ever having met.
         */
        SimScalar waveMeetEnemyRadius{900_ss};
        /** How many standing together count as an army worth turning for. */
        int waveMeetEnemyCount{3};
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

    /**
     * Sets one knob by its field name, from text. This is what
     * `--ai-tune <player>:<knob>=<value>` applies, and it exists for the
     * arena: a change to the AI's behaviour cannot be judged in a mirror
     * match, where both sides get it, so the runner plays one setting
     * against the other in the same game. False for a name it does not
     * know; the caller decides whether that is fatal.
     */
    bool applyAiTuning(AiTuningProfile& profile, const std::string& knob, const std::string& value);
}
