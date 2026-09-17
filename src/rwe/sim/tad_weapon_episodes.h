#pragma once

#include <cstdint>

// GENERATED FILE -- do not edit by hand. Regenerate with tad_episodes
// --emit-weapon-cpp; the command, and the corpus it needs, are in
// docs/TA-DEMOS.md.
//
// Episodes mined from real Total Annihilation games, for the conformance tests
// in weaponflight.test.cpp. Its siblings are tad_economy_episodes.h and
// tad_build_episodes.h; the three share no struct and are mined by different
// passes, so they are kept apart and regenerate independently.
//
// WHY THIS IS A HEADER OF STRUCTS AND NOT A DATA FILE. rwe_test is hermetic --
// it reads no files, mounts no VFS and opens no archive -- and it stays that
// way. Demos and mod files never enter the repository either. So the numbers
// travel as source: each weapon's own TDF values are transcribed inline beside
// the observation they explain, and a test can be read without either.
//
// WHAT AN EPISODE IS. One shot out of a real game, with the damage it caused.
// NOTHING IN A DEMO LINKS THE TWO -- no shot id, no sequence number, and no tick
// on a damage record beyond the packet serial carrying it, against 631,578 shots
// and 824,844 damage events -- so the pairing is a filter, not a lookup: a shot
// is kept only where it is the only shot from that shooter at that victim within
// 300 ticks either side and exactly one damage record from that shooter to that
// victim lands in the 300 after it. That keeps 35,535 shots. The representative
// below is the earliest surviving pairing of its cell that landed on the cell's
// modal flight time, and `pairings` and `pairingsAtMode` say how much company it
// had.
//
// The pairing is confirmed by a number no filter looks at: every cell's modal
// damage is its weapon's own [DAMAGE] default, which is `weaponDamage` here.
//
// WHICH SHOTS MAY BE HERE. The two classes the models describe: weapons that fly
// at a constant speed, and weapons with a motor. A ballistic round travels an
// arc longer than the straight line, a vlaunch rocket goes up before it goes
// anywhere, a torpedo travels through water, and a burst weapon fires several
// rounds from one trigger so the isolation filter cannot mean what it means
// elsewhere. Those four are four more oracles, not discrepancies. See
// docs/TA-DEMOS.md.
//
// THE ARITHMETIC BEING PINNED. A round does not stop at the point it was aimed
// at. It detonates the first tick its move puts it in a map square an enemy unit
// occupies (0x49B090, straight after the move in 0x49B720), and a unit occupies
// its FootprintX by FootprintZ squares -- so the flight time is the number of
// steps until the round stands on the victim's footprint, which is about half a
// footprint short of the aim point. How far a step goes depends on the class: a
// constant-speed projectile covers weaponVelocity / 30 world units -- the same
// conversion LoadingScene_util.cpp does -- and a self-propelled one leaves at
// startVelocity and gains weaponAcceleration / 900 a tick up to the same cap
// while its motor runs, and coasts after. RWE moves a projectile and then tests
// it against the occupied grid, in that order, so expectedFlightDelta is zero
// everywhere below. The field is kept because the fixture's whole purpose is to
// survive a deliberate divergence; a non-zero value here would have to name the
// docs/TOTALA-EXE.md section that licensed it, exactly as the build fixture's
// does.
//
// WHY THE MISSILE ROWS NAME THEIR VICTIM. A 0x0d records where the shot was
// AIMED, so a victim that moves while the round is in the air is not where the
// distance says it is when it arrives. Over a missile's twenty to forty ticks
// that is most of the error, so a self-propelled cell is scored only over the
// pairings whose victim could not have outrun one step of the projectile --
// `victimName` is the representative's, and it is a building or a slow ground
// unit in every row here. A constant-speed cell needs no such bound and gets
// none. tools/tad-weapontime.py --drift prints the measurement behind that.

namespace rwe
{
    struct TadWeaponEpisode
    {
        const char* demo;
        const char* shooterName;
        unsigned int weaponSlot;
        const char* weaponName;

        /** What was shot at -- see the note on the victim bound above. */
        const char* victimName;

        /**
         * The victim's FBI FootprintX and FootprintZ, in map squares: what the
         * round stops on. The victim stands at the aim point.
         */
        unsigned int victimFootprintX;
        unsigned int victimFootprintZ;

        /** The tick the shot was fired on, and the tick its damage arrived. */
        uint32_t shotTick;
        uint32_t damageTick;

        /** How many pairings the cell held, and how many shared this flight time. */
        unsigned int pairings;
        unsigned int pairingsAtMode;

        /**
         * Whether the round has a motor, which decides which model it is being
         * held to and which physics type the test builds for it.
         */
        bool selfPropelled;

        /** TDF weaponvelocity, in world units a SECOND. Divide by 30 for a tick. */
        unsigned int weaponVelocity;

        /**
         * TDF startvelocity and weaponacceleration, also per second (and per
         * second squared). Both zero for a constant-speed round. A startVelocity
         * of zero with an acceleration means the missile leaves from a
         * standstill; with no acceleration it means full speed.
         */
        unsigned int startVelocity;
        unsigned int weaponAcceleration;

        /**
         * What times the motor: `weaponRange / weaponVelocity` ticks, or
         * `weaponTimerTicks` where the weapon says noAutoRange. Two episodes
         * below outlive their motor and coast the rest of the way in --
         * CORMIST's, seventeen steps against a fifteen-tick burn, and
         * ARMAABOT's, sixteen against the same -- so these are load-bearing for
         * those two and describe every other.
         */
        unsigned int weaponRange;
        unsigned int weaponTimerTicks;
        bool noAutoRange;

        /**
         * Whether running out of motor detonates the round where it is instead
         * of letting it coast on. The two episodes that do outlive their motor
         * have it clear, so nothing here detonates early; it travels with them so
         * that a regeneration producing a burnblow round that did could not
         * quietly be flown as though it coasted.
         */
        bool burnBlow;

        /** The weapon's [DAMAGE] default, which the cell's modal damage matches. */
        unsigned int weaponDamage;

        /**
         * Where the shot came from and where it was aimed, as TA puts them on
         * the wire: 16.16 fixed point in world units, y up. Kept as the raw
         * integers because 16.16 carries up to 32 significant bits and a float
         * has 24, so converting here would lose the low end of a large
         * coordinate and could move a ceil across a boundary.
         */
        int32_t originX, originY, originZ;
        int32_t targetX, targetY, targetZ;

        /** damageTick - shotTick, which is what the test has to reproduce. */
        unsigned int flightTicks;

        /** Ticks RWE is expected to differ by, and what licenses it. */
        int expectedFlightDelta;
        const char* expectedDifference;
    };

    // clang-format off
    inline constexpr TadWeaponEpisode tadWeaponEpisodes[] = {
        {"14727.ted", "ARMAABOT", 0, "MISSILE_GF_HEAVY", "CORPYRO", 2, 2,
            39609, 39625, 40, 23,
            true, 900, 600, 450,
            450, 90, false, false, 45,
            331911963, 7099748, 264221154,
            325224972, 6560458, 289512959,
            16, 0, nullptr},
        {"14729.ted", "ARMAMPH", 0, "GAUSS_MAV", "CORRL", 3, 3,
            40519, 40534, 221, 156,
            false, 450, 0, 0,
            288, 0, false, false, 420,
            398596580, 6509745, 158656866,
            383254528, 8388608, 163053568,
            15, 0, nullptr},
        {"14723.ted", "ARMFAV", 0, "LASER_FAV", "CORAK", 2, 2,
            4792, 4797, 619, 569,
            false, 960, 0, 0,
            180, 0, false, false, 36,
            284391770, 6033138, 440095306,
            293104898, 5444732, 436291351,
            5, 0, nullptr},
        {"14725.ted", "ARMFIG", 0, "MISSILE_VTOL", "ARMRL", 3, 3,
            61322, 61341, 262, 107,
            true, 600, 450, 150,
            600, 50, true, true, 36,
            399592882, 14417920, 361434935,
            412614656, 6904217, 345546752,
            19, 0, nullptr},
        {"14723.ted", "ARMHLT", 0, "LASER_HEAVY", "CORMEX", 3, 3,
            22748, 22765, 69, 42,
            false, 960, 0, 0,
            600, 0, false, false, 300,
            294071661, 8843128, 408847640,
            260566086, 5964793, 392691356,
            17, 0, nullptr},
        {"14725.ted", "ARMJAV", 0, "GAUSS", "ARMWAR", 2, 2,
            28419, 28435, 131, 78,
            false, 500, 0, 0,
            320, 0, false, true, 160,
            295316972, 6932880, 211982467,
            310777984, 6550580, 221483446,
            16, 0, nullptr},
        {"14723.ted", "ARMJETH", 0, "MISSILE_GF_MEDIUM", "CORGATOR", 2, 2,
            19572, 19579, 540, 311,
            true, 825, 550, 275,
            400, 90, false, false, 30,
            510278471, 6553644, 44597131,
            518120996, 5480194, 39024061,
            7, 0, nullptr},
        {"14725.ted", "ARMLATNK", 0, "LIGHTNING_LATNK", "CORFAV", 2, 2,
            16213, 16218, 280, 229,
            false, 750, 0, 0,
            210, 0, false, false, 168,
            327763281, 6185988, 578749601,
            333433236, 5942829, 571322016,
            5, 0, nullptr},
        {"14725.ted", "ARMLLT", 0, "LASER_LIGHT", "ARMFLASH", 2, 2,
            28476, 28489, 58, 39,
            false, 960, 0, 0,
            450, 0, false, false, 80,
            334742436, 8177089, 247358366,
            319264154, 5808964, 224956621,
            13, 0, nullptr},
        {"14725.ted", "ARMMANNI", 0, "BLOD_MANNI", "CORBEH", 4, 4,
            87071, 87097, 138, 66,
            false, 960, 0, 0,
            960, 0, false, false, 1500,
            332999843, 7613995, 470968655,
            388062492, 5570560, 461460108,
            26, 0, nullptr},
        {"14725.ted", "ARMMAV", 0, "GAUSS_MAV", "ARMFLASH", 2, 2,
            29214, 29229, 323, 191,
            false, 450, 0, 0,
            288, 0, false, false, 420,
            354328174, 7378324, 218394287,
            349296607, 5808964, 204883083,
            15, 0, nullptr},
        {"14723.ted", "ARMRL", 0, "MISSILE_GF_HEAVY", "CORAK", 2, 2,
            22141, 22148, 228, 127,
            true, 900, 600, 450,
            450, 90, false, false, 45,
            402257728, 8770291, 294444596,
            408969581, 5945241, 285772759,
            7, 0, nullptr},
        {"14723.ted", "ARMROCK", 0, "ROCKET", "CORGATOR", 2, 2,
            18699, 18702, 173, 128,
            true, 540, 405, 270,
            450, 60, false, false, 108,
            438937198, 6651624, 23960921,
            442324490, 5545730, 24097181,
            3, 0, nullptr},
        {"14725.ted", "ARMSAM", 0, "MISSILE_GF_HEAVY", "CORGATOR", 2, 2,
            18001, 18006, 644, 389,
            true, 900, 600, 450,
            450, 90, false, false, 45,
            245504013, 6799031, 464778249,
            247988338, 5811905, 457703308,
            5, 0, nullptr},
        {"14725.ted", "ARMSNIPE", 0, "GAUSS_SNIPE", "CORGATOR", 2, 2,
            51400, 51420, 4348, 2512,
            true, 960, 960, 960,
            800, 24, false, true, 675,
            277399088, 8084670, 331985343,
            315673886, 5811905, 351428067,
            20, 0, nullptr},
        {"14725.ted", "ARMSPID", 0, "PARALYZER", "ARMMAV", 2, 2,
            38212, 38217, 424, 381,
            false, 960, 0, 0,
            270, 0, false, false, 60,
            337612510, 6601480, 301537948,
            346024506, 6597801, 309605370,
            5, 0, nullptr},
        {"14725.ted", "ARMZEUS", 0, "LIGHTNING", "ARMMART", 3, 3,
            58778, 58785, 1679, 1317,
            false, 750, 0, 0,
            270, 0, false, false, 225,
            326434206, 6095817, 312929551,
            313868033, 6093429, 314734450,
            7, 0, nullptr},
        {"14723.ted", "CORAK", 0, "LASER_GATOR", "ARMFLASH", 2, 2,
            16116, 16126, 152, 98,
            false, 400, 0, 0,
            180, 0, false, false, 30,
            41529143, 5927779, 302539789,
            39567376, 5481284, 311214161,
            10, 0, nullptr},
        {"14729.ted", "CORAMPH", 0, "LASER_MAK", "ARMACK", 2, 2,
            25617, 25620, 161, 144,
            false, 960, 0, 0,
            270, 0, false, false, 224,
            658237841, 7464782, 485060117,
            664544560, 7645228, 483885616,
            3, 0, nullptr},
        {"14726.ted", "CORCAN", 0, "LASER_CAN", "CORMAK", 2, 2,
            33887, 33895, 302, 242,
            false, 960, 0, 0,
            300, 0, false, false, 125,
            47896568, 7322590, 179763639,
            49352856, 6400335, 196990565,
            8, 0, nullptr},
        {"14726.ted", "CORCRASH", 0, "MISSILE_GF_MEDIUM", "CORGATOR", 2, 2,
            18136, 18141, 250, 143,
            true, 825, 550, 275,
            400, 90, false, false, 30,
            15325153, 7176190, 106140911,
            12059157, 5872968, 99819333,
            5, 0, nullptr},
        {"14726.ted", "CORFAST", 0, "LASER_FAST", "ARMJETH", 2, 2,
            17565, 17568, 65, 59,
            false, 960, 0, 0,
            240, 0, false, false, 50,
            219004982, 6641365, 471278689,
            224528629, 6266879, 466240217,
            3, 0, nullptr},
        {"14723.ted", "CORFAV", 0, "LASER_FAV", "ARMPW", 2, 2,
            6656, 6659, 257, 240,
            false, 960, 0, 0,
            180, 0, false, false, 36,
            452053434, 6251979, 121800441,
            445390753, 6074840, 125816615,
            3, 0, nullptr},
        {"14723.ted", "CORGATOR", 0, "LASER_GATOR", "ARMMEX", 3, 3,
            23477, 23485, 142, 87,
            false, 400, 0, 0,
            180, 0, false, false, 30,
            326905818, 5789539, 95980016,
            335020032, 5901778, 95928320,
            8, 0, nullptr},
        {"14725.ted", "CORGEO", 0, "RIOT_ALL", "ARMLATNK", 2, 2,
            30949, 30965, 53, 25,
            false, 700, 0, 0,
            500, 0, false, true, 360,
            642037200, 8433451, 450697460,
            620636382, 5830673, 464833856,
            16, 0, nullptr},
        {"14725.ted", "CORHLT", 0, "LASER_HEAVY", "ARMLATNK", 2, 2,
            31127, 31130, 58, 36,
            false, 960, 0, 0,
            600, 0, false, false, 300,
            481451865, 8661188, 440837149,
            477592297, 5830673, 437620146,
            3, 0, nullptr},
        {"14726.ted", "CORLEVLR", 0, "RIOT_LEVLR", "ARMROCK", 2, 2,
            20270, 20278, 56, 36,
            false, 500, 0, 0,
            180, 27, false, true, 180,
            50152888, 6217990, 68902846,
            58743050, 6423732, 64072796,
            8, 0, nullptr},
        {"14725.ted", "CORLLT", 0, "LASER_LIGHT", "ARMFAV", 2, 2,
            26151, 26163, 66, 38,
            false, 960, 0, 0,
            450, 0, false, false, 80,
            338285940, 8223272, 317247341,
            314572375, 5949177, 326242331,
            12, 0, nullptr},
        {"14726.ted", "CORMAK", 0, "LASER_MAK", "CORAK", 2, 2,
            12600, 12603, 335, 303,
            false, 960, 0, 0,
            270, 0, false, false, 224,
            35506543, 6190153, 463932321,
            39442790, 6265207, 467871891,
            3, 0, nullptr},
        {"14725.ted", "CORMANT", 0, "MISSILE_TANK", "ARMROCK", 2, 2,
            33430, 33447, 148, 77,
            true, 825, 550, 275,
            540, 150, false, false, 90,
            327282369, 6301144, 391606945,
            305711956, 6399266, 402768266,
            17, 0, nullptr},
        {"14725.ted", "CORMIST", 0, "MISSILE_GF_HEAVY", "ARMAAA", 3, 3,
            66483, 66500, 469, 231,
            true, 900, 600, 450,
            450, 90, false, false, 45,
            299314596, 6591989, 409114570,
            274202624, 5570560, 398983168,
            17, 0, nullptr},
        {"14725.ted", "CORREAP", 0, "RIOT_REAPER", "ARMSTUMP", 3, 3,
            45610, 45627, 30, 13,
            false, 540, 0, 0,
            360, 0, false, true, 160,
            482095297, 6320538, 445309573,
            461994524, 5945753, 451333002,
            17, 0, nullptr},
        {"14726.ted", "CORRL", 0, "MISSILE_GF_HEAVY", "CORAK", 2, 2,
            12184, 12194, 169, 95,
            true, 900, 600, 450,
            450, 90, false, false, 45,
            24600920, 9289348, 450089278,
            12595892, 6252655, 459479224,
            10, 0, nullptr},
        {"14726.ted", "CORSTORM", 0, "ROCKET", "CORRAID", 3, 3,
            18167, 18176, 182, 138,
            true, 540, 405, 270,
            450, 60, false, false, 108,
            19865732, 7059986, 90691683,
            13631652, 5906818, 98566169,
            9, 0, nullptr},
        {"14726.ted", "CORSUMO", 0, "LASER_SUMO", "CORPYRO", 2, 2,
            53214, 53222, 647, 416,
            false, 960, 0, 0,
            750, 0, false, false, 768,
            29783679, 15441125, 378338974,
            41955869, 16503167, 367119090,
            8, 0, nullptr},
        {"14726.ted", "CORVAMP", 0, "MISSILE_VTOL_GF", "ARMARL", 3, 3,
            100800, 100820, 44, 29,
            true, 600, 450, 150,
            600, 150, false, false, 24,
            222171438, 24838144, 497591859,
            237502464, 14352384, 512229376,
            20, 0, nullptr},
        {"14725.ted", "CORVENG", 0, "MISSILE_VTOL", "ARMRAD", 2, 2,
            11294, 11304, 388, 235,
            true, 600, 450, 150,
            600, 50, true, true, 36,
            79895180, 14745600, 262695944,
            77584476, 7295927, 270565310,
            10, 0, nullptr},
    };
    // clang-format on
}
