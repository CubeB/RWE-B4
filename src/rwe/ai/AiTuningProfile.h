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
         * So it stays off until the tier arrives early enough to stop being
         * a gamble. §15.7 says what is left to try.
         */
        bool techLevelTwo{false};
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
        /** Shipyards wanted, once the fleet target above is not zero. One is the whole navy's factory. */
        int targetShipyardCount{1};
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
         * existed, and it stays there until the arena has played it against
         * its own absence.
         */
        int attackNavalSize{0};

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
         * While a wave is out, an intruder at home is answered by the units
         * gathering for the next wave, and the wave carries on. Off, one
         * scout at the extractors puts the AI in Defend and recalls the lot,
         * which is what used to happen: a wave that had nearly reached the
         * enemy turned round and walked home.
         */
        bool reserveAnswersIntruders{true};
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
