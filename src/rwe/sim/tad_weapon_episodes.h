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
// WHICH SHOTS MAY BE HERE. Only weapons that fly at a constant speed, which is
// to say the ones this arithmetic describes. An accelerating missile leaves the
// rail at `startvelocity` and works up, a ballistic round travels an arc longer
// than the straight line, a vlaunch rocket goes up before it goes anywhere, a
// torpedo travels through water, and a burst weapon fires several rounds from
// one trigger so the isolation filter cannot mean what it means elsewhere. Those
// five are five more oracles, not discrepancies. See docs/TA-DEMOS.md.
//
// THE ARITHMETIC BEING PINNED. A projectile covers weaponVelocity / 30 world
// units a tick -- the same conversion LoadingScene_util.cpp does -- and takes
// its FIRST step on the tick it is fired, so it has covered the distance after
// ceil(d / v) steps and the gap between the shot and the damage is one less.
// RWE steps projectiles after the behaviour pass that spawns them, so it takes
// that first step on the firing tick too, and expectedFlightDelta is zero
// everywhere below. The field is kept because the fixture's whole purpose is to
// survive a deliberate divergence; a non-zero value here would have to name the
// docs/TOTALA-EXE.md section that licensed it, exactly as the build fixture's
// does.

namespace rwe
{
    struct TadWeaponEpisode
    {
        const char* demo;
        const char* shooterName;
        unsigned int weaponSlot;
        const char* weaponName;

        /** The tick the shot was fired on, and the tick its damage arrived. */
        uint32_t shotTick;
        uint32_t damageTick;

        /** How many pairings the cell held, and how many shared this flight time. */
        unsigned int pairings;
        unsigned int pairingsAtMode;

        /** TDF weaponvelocity, in world units a SECOND. Divide by 30 for a tick. */
        unsigned int weaponVelocity;

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
        {"14725.ted", "ARMFAV", 0, "LASER_FAV",
            21454, 21459, 622, 407,
            960, 36,
            280669234, 6367555, 414828813,
            276477901, 5811905, 404703456,
            5, 0, nullptr},
        {"14723.ted", "ARMHLT", 0, "LASER_HEAVY",
            22748, 22765, 69, 42,
            960, 300,
            294071661, 8843128, 408847640,
            260566086, 5964793, 392691356,
            17, 0, nullptr},
        {"14725.ted", "ARMJAV", 0, "GAUSS",
            28419, 28435, 133, 56,
            500, 160,
            295316972, 6932880, 211982467,
            310777984, 6550580, 221483446,
            16, 0, nullptr},
        {"14725.ted", "ARMLATNK", 0, "LIGHTNING_LATNK",
            16213, 16218, 280, 186,
            750, 168,
            327763281, 6185988, 578749601,
            333433236, 5942829, 571322016,
            5, 0, nullptr},
        {"14725.ted", "ARMLLT", 0, "LASER_LIGHT",
            28476, 28489, 58, 41,
            960, 80,
            334742436, 8177089, 247358366,
            319264154, 5808964, 224956621,
            13, 0, nullptr},
        {"14725.ted", "ARMMANNI", 0, "BLOD_MANNI",
            87071, 87097, 142, 67,
            960, 1500,
            332999843, 7613995, 470968655,
            388062492, 5570560, 461460108,
            26, 0, nullptr},
        {"14725.ted", "ARMMAV", 0, "GAUSS_MAV",
            30666, 30678, 323, 130,
            450, 420,
            218834026, 7431252, 279799813,
            221706350, 5928877, 292060765,
            12, 0, nullptr},
        {"14725.ted", "ARMSNIPE", 0, "GAUSS_SNIPE",
            51400, 51420, 4356, 2594,
            960, 675,
            277399088, 8084670, 331985343,
            315673886, 5811905, 351428067,
            20, 0, nullptr},
        {"14725.ted", "ARMSPID", 0, "PARALYZER",
            38212, 38217, 424, 288,
            960, 60,
            337612510, 6601480, 301537948,
            346024506, 6597801, 309605370,
            5, 0, nullptr},
        {"14725.ted", "ARMZEUS", 0, "LIGHTNING",
            58778, 58785, 1686, 1130,
            750, 225,
            326434206, 6095817, 312929551,
            313868033, 6093429, 314734450,
            7, 0, nullptr},
        {"14723.ted", "CORAK", 0, "LASER_GATOR",
            16116, 16126, 154, 76,
            400, 30,
            41529143, 5927779, 302539789,
            39567376, 5481284, 311214161,
            10, 0, nullptr},
        {"14729.ted", "CORAMPH", 0, "LASER_MAK",
            25617, 25620, 162, 85,
            960, 224,
            658237841, 7464782, 485060117,
            664544560, 7645228, 483885616,
            3, 0, nullptr},
        {"14726.ted", "CORCAN", 0, "LASER_CAN",
            33887, 33895, 303, 181,
            960, 125,
            47896568, 7322590, 179763639,
            49352856, 6400335, 196990565,
            8, 0, nullptr},
        {"14726.ted", "CORFAST", 0, "LASER_FAST",
            17565, 17568, 68, 42,
            960, 50,
            219004982, 6641365, 471278689,
            224528629, 6266879, 466240217,
            3, 0, nullptr},
        {"14723.ted", "CORFAV", 0, "LASER_FAV",
            6656, 6659, 260, 179,
            960, 36,
            452053434, 6251979, 121800441,
            445390753, 6074840, 125816615,
            3, 0, nullptr},
        {"14725.ted", "CORGATOR", 0, "LASER_GATOR",
            14502, 14513, 142, 63,
            400, 30,
            147752316, 6117219, 454598368,
            156166973, 6395104, 459986323,
            11, 0, nullptr},
        {"14726.ted", "CORHLT", 0, "LASER_HEAVY",
            42224, 42237, 58, 36,
            960, 300,
            386616443, 20336214, 349976130,
            405979171, 15003815, 370000367,
            13, 0, nullptr},
        {"14726.ted", "CORHRK", 0, "ROCKET_HRK",
            54052, 54064, 709, 281,
            800, 160,
            254563564, 7071399, 462888020,
            276615407, 6299647, 467673494,
            12, 0, nullptr},
        {"14726.ted", "CORLEVLR", 0, "RIOT_LEVLR",
            22422, 22430, 56, 23,
            500, 180,
            40558264, 6226068, 293949206,
            38776730, 6435288, 285098597,
            8, 0, nullptr},
        {"14725.ted", "CORLLT", 0, "LASER_LIGHT",
            26151, 26163, 66, 50,
            960, 80,
            338285940, 8223272, 317247341,
            314572375, 5949177, 326242331,
            12, 0, nullptr},
        {"14726.ted", "CORMAK", 0, "LASER_MAK",
            12414, 12419, 337, 234,
            960, 224,
            36629383, 6265469, 470095930,
            25659833, 6263530, 464840469,
            5, 0, nullptr},
        {"14725.ted", "CORREAP", 0, "RIOT_REAPER",
            45610, 45627, 30, 10,
            540, 160,
            482095297, 6320538, 445309573,
            461994524, 5945753, 451333002,
            17, 0, nullptr},
        {"14726.ted", "CORSUMO", 0, "LASER_SUMO",
            53324, 53326, 647, 398,
            960, 768,
            30275651, 15381835, 379329143,
            34352856, 14907248, 378335740,
            2, 0, nullptr},
    };
    // clang-format on
}
