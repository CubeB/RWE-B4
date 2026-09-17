#pragma once

// GENERATED FILE -- do not edit by hand. Regenerate with tad_episodes
// --emit-stall-cpp; the command, and the corpus it needs, are in
// docs/TA-DEMOS.md.
//
// Episodes mined from real Total Annihilation games, for the stall cases in
// economy.test.cpp. Its siblings hold the storage, build-timing and weapon
// episodes; they share no struct and regenerate independently.
//
// WHY THIS IS A HEADER OF STRUCTS AND NOT A DATA FILE. rwe_test is hermetic --
// it reads no files, mounts no VFS and opens no archive -- and it stays that
// way. Demos and mod files never enter the repository either. So the numbers
// travel as source: each unit's own FBI values are transcribed inline beside the
// observation they explain, and a test can be read without either.
//
// WHAT AN EPISODE IS. A factory finishes one product in a second whose settle a
// 0x28 sample shows stalled -- a store read exactly empty -- and starts its next
// product before the following settle. It was granted resources in the stalled
// second, so the settle left it in debt, so the new job is refused from its
// first tick until a settle pays the debt off. TA settles every player on the
// same ticks, multiples of 30 of the demo clock (docs/TOTALA-EXE.md section
// 102), so the job starts late by exactly 30 - startTick % 30, plus a whole 30
// for every further settle the player stayed stalled.
//
// WHAT IS PREDICTED AND WHAT IS READ. residueTicks comes from the start tick and
// the settle cadence alone; nothing about it is read from the build it
// predicts, and over settles the sample says did NOT stall the same selection
// predicts no build at all (tools/tad-stalltime.py prints the control).
// furtherStalledSettles IS read from the observation, because the corpus sees
// one settle in four and the ones between samples are not observed; a test
// replays that many and asserts the rest.
//
// WHICH BUILDS MAY BE HERE. Only an immobile builder, on a (builder, product)
// cell whose modal duration the float32 build model already explains, with
// neither the build nor the builder's jobs either side of it early against the
// model: an early build means something assisted it, and an assist moves a
// duration by an amount nothing in the stream records. One episode per residue.

namespace rwe
{
    struct TadStallEpisode
    {
        /** Provenance: the demo, the owner block, and the factory's unit id in it. */
        const char* demo;
        unsigned int ownerBlock;
        unsigned int builderId;

        /** The factory, with its own WorkerTime from the FBI. */
        const char* builderName;
        unsigned int workerTime;

        /**
         * The job the factory finished in the stalled second, with its FBI
         * figures. All a test needs of it is that the factory was granted a
         * tick's worth of it before the settle; previousFinishTick is that tick.
         */
        const char* previousProductName;
        unsigned int previousBuildTime;
        unsigned int previousBuildCostMetal;
        unsigned int previousBuildCostEnergy;
        unsigned int previousStartTick;
        unsigned int previousFinishTick;

        /**
         * The settle that stalled, and the 0x28 that saw it: the sample's tick
         * and which store read empty. The sample lags the settle by the few
         * ticks a sender's clock runs behind.
         */
        unsigned int stalledSettleTick;
        bool metalEmpty;
        unsigned int sampleTick;

        /** The job that was refused into, with its FBI figures. */
        const char* productName;
        unsigned int buildTime;
        unsigned int buildCostMetal;
        unsigned int buildCostEnergy;
        unsigned int startTick;
        unsigned int finishTick;

        /** The float32 build model's duration for this job, unimpeded. */
        unsigned int modelDurationTicks;

        /** Observed: (finishTick - startTick) - modelDurationTicks. */
        unsigned int lateTicks;

        /** Predicted: 30 - startTick % 30. lateTicks is this plus 30 * furtherStalledSettles. */
        unsigned int residueTicks;

        /** Read from the observation: settles after the sampled one that also stalled. */
        unsigned int furtherStalledSettles;

        /**
         * What RWE is expected to differ by, and why. Computed, as the build
         * episodes' is: RWE's integer accumulator against TA's float32 fraction,
         * which part company only where BuildTime divides exactly by the rate.
         * A settle is not in it -- RWE settles every player on the same ticks,
         * and so, it turns out, does TA.
         */
        int expectedDurationDelta;
        const char* expectedDifference;
    };

    // clang-format off
    inline constexpr TadStallEpisode tadStallEpisodes[] = {
        {"14730.ted", 3, 3226,
            "ARMVP", 120,
            "ARMFAV", 2198, 37, 564, 126439, 127048,
            127050, true, 127051,
            "ARMFAV", 2198, 37, 564, 127078, 127719,
            549, 92, 2, 3,
            0, nullptr},
        {"14730.ted", 3, 3247,
            "ARMVP", 120,
            "ARMFAV", 2198, 37, 564, 123303, 124032,
            124050, true, 124049,
            "ARMFAV", 2198, 37, 564, 124077, 124659,
            549, 33, 3, 1,
            0, nullptr},
        {"14730.ted", 7, 7046,
            "ARMLAB", 120,
            "ARMPW", 1089, 53, 627, 75428, 75790,
            75810, true, 75812,
            "ARMPW", 1089, 53, 627, 75835, 76232,
            272, 125, 5, 4,
            0, nullptr},
        {"14727.ted", 0, 133,
            "ARMVP", 120,
            "ARMFAV", 2198, 37, 564, 66506, 67389,
            67410, false, 67413,
            "ARMFAV", 2198, 37, 564, 67434, 68049,
            549, 66, 6, 2,
            0, nullptr},
        {"14726.ted", 0, 29,
            "ARMVP", 120,
            "ARMFAV", 2198, 37, 564, 60479, 61148,
            61170, true, 61171,
            "ARMFAV", 2198, 37, 564, 61193, 61929,
            549, 187, 7, 6,
            0, nullptr},
        {"14725.ted", 3, 3006,
            "ARMLAB", 120,
            "ARMCK", 7426, 180, 3045, 103226, 105802,
            105810, true, 105811,
            "ARMCK", 7426, 180, 3045, 105832, 107696,
            1856, 8, 8, 0,
            0, nullptr},
        {"14730.ted", 7, 7046,
            "ARMLAB", 120,
            "ARMPW", 1089, 53, 627, 68389, 68841,
            68850, true, 68848,
            "ARMPW", 1089, 53, 627, 68871, 69332,
            272, 189, 9, 6,
            0, nullptr},
        {"14733.ted", 2, 2008,
            "CORLAB", 120,
            "CORTHUD", 2171, 147, 1451, 38133, 38825,
            38850, false, 38851,
            "CORTHUD", 2171, 147, 1451, 38870, 39482,
            542, 70, 10, 2,
            0, nullptr},
        {"14730.ted", 7, 7046,
            "ARMLAB", 120,
            "ARMPW", 1089, 53, 627, 86867, 87319,
            87330, true, 87332,
            "ARMPW", 1089, 53, 627, 87349, 87722,
            272, 101, 11, 3,
            0, nullptr},
        {"14725.ted", 2, 2063,
            "ARMVP", 120,
            "ARMFAV", 2198, 37, 564, 132504, 133503,
            133530, false, 133535,
            "ARMFAV", 2198, 37, 564, 133548, 134679,
            549, 582, 12, 19,
            0, nullptr},
        {"14726.ted", 3, 3030,
            "CORLAB", 120,
            "CORCRASH", 1820, 116, 947, 76727, 77357,
            77370, true, 77367,
            "CORCRASH", 1820, 116, 947, 77387, 77975,
            455, 133, 13, 4,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14726.ted", 1, 1124,
            "CORLAB", 120,
            "CORCRASH", 1820, 116, 947, 72880, 73755,
            73770, true, 73770,
            "CORCRASH", 1820, 116, 947, 73785, 74405,
            455, 165, 15, 5,
            -1, "TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float"},
        {"14730.ted", 1, 1159,
            "CORAAP", 300,
            "CORVAMP", 14435, 289, 8717, 62227, 63809,
            63810, true, 63812,
            "CORVAMP", 14435, 289, 8717, 63824, 65733,
            1443, 466, 16, 15,
            0, nullptr},
        {"14726.ted", 1, 1007,
            "CORAP", 120,
            "CORFINK", 3234, 39, 1369, 96900, 98008,
            98010, true, 98013,
            "CORFINK", 3234, 39, 1369, 98023, 99118,
            808, 287, 17, 9,
            0, nullptr},
        {"14727.ted", 8, 8007,
            "CORLAB", 120,
            "CORAK", 1142, 56, 626, 23067, 23472,
            23490, true, 23492,
            "CORAK", 1142, 56, 626, 23502, 23835,
            285, 48, 18, 1,
            0, nullptr},
        {"14734.ted", 6, 6088,
            "ARMLAB", 120,
            "ARMPW", 1089, 53, 627, 96488, 96790,
            96810, true, 96814,
            "ARMPW", 1089, 53, 627, 96820, 97142,
            272, 50, 20, 1,
            0, nullptr},
        {"14730.ted", 3, 3005,
            "ARMVP", 120,
            "ARMFAV", 2198, 37, 564, 93960, 94629,
            94650, true, 94653,
            "ARMFAV", 2198, 37, 564, 94659, 95259,
            549, 51, 21, 1,
            0, nullptr},
        {"14726.ted", 1, 1177,
            "CORAP", 120,
            "CORFINK", 3234, 39, 1369, 93175, 94283,
            94290, true, 94293,
            "CORFINK", 3234, 39, 1369, 94298, 95278,
            808, 172, 22, 5,
            0, nullptr},
        {"14734.ted", 6, 6088,
            "ARMLAB", 120,
            "ARMPW", 1089, 53, 627, 63184, 63546,
            63570, false, 63574,
            "ARMPW", 1089, 53, 627, 63576, 63872,
            272, 24, 24, 0,
            0, nullptr},
        {"14734.ted", 1, 1181,
            "CORAP", 120,
            "CORFINK", 3234, 39, 1369, 80872, 82040,
            82050, false, 82050,
            "CORFINK", 3234, 39, 1369, 82055, 82948,
            808, 85, 25, 2,
            0, nullptr},
        {"14734.ted", 1, 1160,
            "CORAP", 120,
            "CORFINK", 3234, 39, 1369, 70731, 71599,
            71610, false, 71606,
            "CORFINK", 3234, 39, 1369, 71614, 72448,
            808, 26, 26, 0,
            0, nullptr},
        {"14725.ted", 2, 2063,
            "ARMVP", 120,
            "ARMFAV", 2198, 37, 564, 56544, 57783,
            57810, true, 57807,
            "ARMFAV", 2198, 37, 564, 57813, 58539,
            549, 177, 27, 5,
            0, nullptr},
        {"14730.ted", 7, 7046,
            "ARMLAB", 120,
            "ARMPW", 1089, 53, 627, 98702, 99062,
            99090, false, 99091,
            "ARMPW", 1089, 53, 627, 99092, 99422,
            272, 58, 28, 1,
            0, nullptr},
        {"14726.ted", 1, 1201,
            "CORAP", 120,
            "CORFINK", 3234, 39, 1369, 100188, 101236,
            101250, true, 101250,
            "CORFINK", 3234, 39, 1369, 101251, 102358,
            808, 299, 29, 9,
            0, nullptr},
    };
    // clang-format on
}
