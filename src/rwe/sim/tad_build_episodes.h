#pragma once

// GENERATED FILE -- do not edit by hand. Regenerate with tad_episodes
// --emit-build-cpp; the command, and the corpus it needs, are in
// docs/TA-DEMOS.md.
//
// Episodes mined from real Total Annihilation games, for the conformance tests
// in buildtime.test.cpp. Its sibling, tad_economy_episodes.h, holds the storage
// ones; they share no struct and are mined by different passes, so they are kept
// apart and regenerate independently.
//
// WHY THIS IS A HEADER OF STRUCTS AND NOT A DATA FILE. rwe_test is hermetic --
// it reads no files, mounts no VFS and opens no archive -- and it stays that
// way. Demos and mod files never enter the repository either. So the numbers
// travel as source: each unit's own FBI values are transcribed inline beside the
// observation they explain, and a test can be read without either.
//
// WHAT AN EPISODE IS. One (builder type, product type) cell of the corpus: every
// build of that product by that builder, pooled across the games it appeared in,
// consumed as the MODE of its durations. The mode and not the mean, because a
// build runs at full rate unless something interferes -- an assist shortens it
// and a missed micro-stall lengthens it -- so the modal duration is the
// unassisted, unimpeded one and the spread either side is the interference.
//
// WHICH BUILDS MAY BE HERE. Builds by an IMMOBILE builder, which is to say a
// factory: there is nothing for it to walk to and nothing for it to deploy, so
// the duration is the nanolathe and nothing else. And builds by an AIRBORNE one,
// which has nothing to deploy either -- the original never makes a construction
// aircraft wait for its stance -- and whose extra tick is no longer a mystery:
// it lathes twice on the tick it creates the nanoframe (docs/TOTALA-EXE.md
// section 110), which RWE now does too. A ground mobile builder pays its own COB
// deploy sequence before INBUILDSTANCE, which is the mod's data rather than the
// engine's behaviour, and is never an episode. See docs/TA-DEMOS.md.
//
// THE ARITHMETIC BEING PINNED. A builder contributes p = WorkerTime / 30 build
// units a tick -- integer division, in the engine and in the demo tooling alike.
// The first increment lands on the tick the nanoframe appears, so an episode's
// duration is one less than the number of increments -- and two less for a
// construction aircraft, because that first tick pays two of them. The original
// finishes when a single-precision fraction counted up by p / BuildTime passes
// 1.0f; RWE adds p to an unsigned counter and finishes at BuildTime. Those two
// agree except where BuildTime divides exactly by p, and expectedDurationDelta
// is where they do not. The two classes carry the same convention, so a -1 here
// always means that difference and never the aircraft's second lathe.

namespace rwe
{
    /** One (builder, product) cell of the corpus, consumed as its modal duration. */
    struct TadBuildEpisode
    {
        /**
         * Provenance. builds is how many builds of this pair the corpus held
         * after the outlier cap, buildsAtMode how many of them landed on the
         * mode, and the tick pair is one representative build that did -- out of
         * the named demo, which is not necessarily the only game the cell pooled
         * over.
         */
        const char* demo;
        const char* builderName;
        const char* productName;
        unsigned int startTick;
        unsigned int finishTick;
        unsigned int builds;
        unsigned int buildsAtMode;

        /** The builder's own WorkerTime and the product's own BuildTime, from the FBI. */
        unsigned int workerTime;
        unsigned int buildTime;

        /**
         * Whether the builder is a construction aircraft, from its own FBI's
         * Canfly. It decides how many increments the first tick pays -- two
         * rather than one -- so a test cannot count them without it.
         */
        bool builderFlies;

        /** Observed: finishTick - startTick, at the mode. */
        unsigned int modeDurationTicks;

        /**
         * What RWE is expected to differ by, and why.
         *
         * A conformance test that asserts equality gets disabled the first time
         * it is right to fail, so an episode asserts the observation plus a
         * known delta instead. expectedDifference names the docs/TOTALA-EXE.md
         * section 88 entry that licences a non-zero one, and is null where there
         * is nothing to excuse.
         *
         * Unlike the storage episodes' deltas, these are not hand-written: the
         * two completion models are both small enough to replay, so the emitter
         * computes the difference between them. That is not circular. A test
         * asserting mode + delta still fails if the engine stops matching its
         * own model, and it fails on exactly these rows -- and no others -- if
         * the engine is changed to the original's float, which is what says the
         * deltas are the divergence section 88 describes rather than a fudge
         * that happens to fit.
         */
        int expectedDurationDelta;
        const char* expectedDifference;
    };

    // clang-format off
    inline constexpr TadBuildEpisode tadBuildEpisodes[] = {
        {"14729.ted", "ARMAAP", "ARMPNIX",
            53973, 56985, 27, 27,
            300, 30120, false, 3012,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14729.ted", "ARMALAB", "ARMZEUS",
            35520, 36375, 7, 3,
            300, 8560, false, 855,
            0, nullptr},
        {"14729.ted", "ARMASY", "ARMSUBK",
            45693, 48903, 10, 10,
            300, 32110, false, 3210,
            0, nullptr},
        {"14725.ted", "ARMLAB", "ARMFLEA",
            38096, 39354, 35, 30,
            120, 5032, false, 1258,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14725.ted", "ARMLAB", "ARMJETH",
            7914, 8379, 312, 290,
            120, 1863, false, 465,
            0, nullptr},
        {"14725.ted", "ARMLAB", "ARMPW",
            5519, 5791, 256, 189,
            120, 1089, false, 272,
            0, nullptr},
        {"14725.ted", "ARMLAB", "ARMROCK",
            11153, 11761, 35, 30,
            120, 2432, false, 608,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14726.ted", "ARMLAB", "ARMVADER",
            34090, 35670, 65, 62,
            120, 6320, false, 1580,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14727.ted", "ARMLAB", "ARMWAR",
            27159, 28301, 6, 6,
            120, 4568, false, 1142,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14725.ted", "ARMVP", "ARMFAV",
            77922, 78471, 363, 325,
            120, 2198, false, 549,
            0, nullptr},
        {"14723.ted", "ARMVP", "ARMFLASH",
            12525, 12944, 249, 225,
            120, 1676, false, 419,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14728.ted", "ARMVP", "ARMJAV",
            15126, 16302, 14, 14,
            120, 4704, false, 1176,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14728.ted", "ARMVP", "ARMLART",
            19119, 19828, 74, 64,
            120, 2840, false, 709,
            0, nullptr},
        {"14727.ted", "ARMVP", "ARMSAM",
            68513, 69170, 137, 114,
            120, 2631, false, 657,
            0, nullptr},
        {"14726.ted", "ARMVP", "ARMSTUMP",
            25837, 26738, 138, 119,
            120, 3606, false, 901,
            0, nullptr},
        {"14730.ted", "CORALAB", "CORPYRO",
            21815, 22715, 6, 5,
            300, 9000, false, 900,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14725.ted", "CORAP", "CORFINK",
            39307, 40115, 115, 102,
            120, 3234, false, 808,
            0, nullptr},
        {"14725.ted", "CORAP", "CORVENG",
            23807, 25646, 40, 33,
            120, 7356, false, 1839,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14725.ted", "CORCA", "CORDRAG",
            57526, 58090, 22, 17,
            60, 1130, true, 564,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14725.ted", "CORCA", "CORMEX",
            8049, 8985, 7, 6,
            60, 1874, true, 936,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14729.ted", "CORCA", "CORRAD",
            12481, 13048, 19, 18,
            60, 1137, true, 567,
            0, nullptr},
        {"14723.ted", "CORLAB", "CORAK",
            18740, 19025, 157, 144,
            120, 1142, false, 285,
            0, nullptr},
        {"14726.ted", "CORLAB", "CORCK",
            22207, 24136, 29, 25,
            120, 7720, false, 1929,
            0, nullptr},
        {"14726.ted", "CORLAB", "CORCRASH",
            13133, 13588, 37, 31,
            120, 1820, false, 455,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14726.ted", "CORLAB", "CORSTORM",
            14103, 14718, 137, 125,
            120, 2461, false, 615,
            0, nullptr},
        {"14723.ted", "CORVP", "CORFAV",
            2428, 2996, 146, 123,
            120, 2273, false, 568,
            0, nullptr},
        {"14729.ted", "CORVP", "CORMIST",
            12668, 13326, 21, 17,
            120, 2636, false, 658,
            0, nullptr},
        {"14726.ted", "CORVP", "CORRAID",
            19024, 19914, 57, 48,
            120, 3564, false, 890,
            0, nullptr},
    };
    // clang-format on
}
