#pragma once

// GENERATED FILE -- do not edit by hand. Regenerate with tad_episodes --emit-cpp;
// the command, and the corpus it needs, are in docs/TA-DEMOS.md.
//
// Episodes mined from real Total Annihilation games, for the conformance tests
// in economy.test.cpp.
//
// WHY THIS IS A HEADER OF STRUCTS AND NOT A DATA FILE. rwe_test is hermetic --
// it reads no files, mounts no VFS and opens no archive -- and it stays that
// way. Demos and mod files never enter the repository either. So the numbers
// travel as source: each unit's own FBI values are transcribed inline beside
// the observation they explain, and a test can be read without either.
//
// WHAT AN EPISODE IS. One 0x28 resource sample from one player, with everything
// that player had finished, and everything it still had under construction, at
// the tick the sample landed on. A 0x28 is the sender's own state
// (docs/TA-DEMOS.md), so the composition is that sender's own owner block.
// Only slots 2 and 3 of the record -- the two storage capacities -- are what
// these episodes are chosen to explain; slots 0 and 1 come along for the clamp.
//
// The composition does NOT include the commander. TA gives a player its lobby
// storage setting for its commander rather than the commander's own FBI figures
// -- neither data set's commander declares any -- and startingMetal is that
// setting, read off the player's own opening sample, before it had finished
// anything at all.

#include <cstddef>

namespace rwe
{
    /**
     * One unit type in an episode's composition, with the FBI values that make
     * the observation predictable transcribed beside it.
     */
    struct TadEpisodeComposition
    {
        const char* unitName;
        unsigned int count;
        float metalStorage;
        float energyStorage;
    };

    /** One player's resource sample, and what it owned when the sample was taken. */
    struct TadStorageEpisode
    {
        /** Provenance: the demo this came out of, and where in it. */
        const char* demo;
        unsigned int ownerBlock;

        /**
         * The tick the sample landed on, and the one before it. The pair is the
         * window a failure has to be explained inside: anything that finished in
         * between is credited by the later sample and not by the earlier. A zero
         * previous tick means this was the player's first sample.
         */
        unsigned int previousSampleTick;
        unsigned int sampleTick;

        /** What the lobby gave the player for its commander. */
        float startingMetal;
        float startingEnergy;

        /** Finished at sampleTick, aggregated by type, commander excluded. */
        const TadEpisodeComposition* finished;
        std::size_t finishedCount;

        /**
         * Nanoframes standing at sampleTick that had not finished. These
         * contribute nothing, which is the falsifiable half of the episode:
         * crediting them would break 5,972 of the 6,162 corpus samples that have
         * one in flight.
         */
        const TadEpisodeComposition* building;
        std::size_t buildingCount;

        /** Observed, slots 2 and 3 of the record. */
        float metalStorage;
        float energyStorage;

        /** Observed, slots 0 and 1. Never above the capacity, in 61,709 samples. */
        float metalStored;
        float energyStored;

        /**
         * What RWE is expected to differ by, and why.
         *
         * A conformance test that asserts equality gets disabled the first time
         * it is right to fail, so an episode asserts the observation plus a
         * known delta instead. expectedDifference names the docs/TOTALA-EXE.md
         * section 88 entry that licences a non-zero one, and is null where there
         * is nothing to excuse. The emitter cannot know about a deliberate
         * difference, so it writes zero and null; an entry here is written by
         * hand, and survives regeneration because it is written into the
         * emitter's own table. There are none yet -- see section 88 for the
         * differences that exist and why none of them moves a storage capacity.
         */
        float expectedMetalStorageDelta;
        float expectedEnergyStorageDelta;
        const char* expectedDifference;
    };

    // 14727.ted, owner block 0, tick 95: capacity 1000.0f metal, 1000.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode0Building[] = {
        {"ARMMEX", 1, 0.0f, 0.0f},
    };

    // 14727.ted, owner block 0, tick 2374: capacity 1100.0f metal, 1100.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode1Finished[] = {
        {"ARMLAB", 1, 100.0f, 100.0f},
        {"ARMMEX", 3, 0.0f, 0.0f},
        {"ARMWIN", 4, 0.0f, 0.0f},
    };

    inline constexpr TadEpisodeComposition tadStorageEpisode1Building[] = {
        {"ARMWIN", 1, 0.0f, 0.0f},
    };

    // 14727.ted, owner block 0, tick 3094: capacity 1125.0f metal, 1125.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode2Finished[] = {
        {"ARMCK", 1, 25.0f, 25.0f},
        {"ARMLAB", 1, 100.0f, 100.0f},
        {"ARMMEX", 3, 0.0f, 0.0f},
        {"ARMWIN", 6, 0.0f, 0.0f},
    };

    // 14727.ted, owner block 1, tick 2854: capacity 1125.0f metal, 1125.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode3Finished[] = {
        {"ARMCK", 1, 25.0f, 25.0f},
        {"ARMLAB", 1, 100.0f, 100.0f},
        {"ARMMEX", 3, 0.0f, 0.0f},
        {"ARMWIN", 4, 0.0f, 0.0f},
    };

    inline constexpr TadEpisodeComposition tadStorageEpisode3Building[] = {
        {"ARMCK", 1, 25.0f, 25.0f},
        {"ARMWIN", 1, 0.0f, 0.0f},
    };

    // 14727.ted, owner block 6, tick 2015: capacity 1000.0f metal, 6000.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode4Finished[] = {
        {"CORESTOR", 1, 0.0f, 5000.0f},
        {"CORMEX", 2, 0.0f, 0.0f},
        {"CORRL", 1, 0.0f, 0.0f},
        {"CORWIN", 2, 0.0f, 0.0f},
    };

    // 14727.ted, owner block 6, tick 4415: capacity 1100.0f metal, 6100.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode5Finished[] = {
        {"CORESTOR", 1, 0.0f, 5000.0f},
        {"CORLAB", 1, 100.0f, 100.0f},
        {"CORMEX", 4, 0.0f, 0.0f},
        {"CORRL", 2, 0.0f, 0.0f},
        {"CORWIN", 3, 0.0f, 0.0f},
    };

    // 14727.ted, owner block 7, tick 2373: capacity 1100.0f metal, 1100.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode6Finished[] = {
        {"CORLAB", 1, 100.0f, 100.0f},
        {"CORMEX", 3, 0.0f, 0.0f},
        {"CORWIN", 3, 0.0f, 0.0f},
    };

    inline constexpr TadEpisodeComposition tadStorageEpisode6Building[] = {
        {"CORWIN", 1, 0.0f, 0.0f},
    };

    // 14727.ted, owner block 7, tick 3453: capacity 1125.0f metal, 1125.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode7Finished[] = {
        {"CORAK", 1, 0.0f, 0.0f},
        {"CORCK", 1, 25.0f, 25.0f},
        {"CORLAB", 1, 100.0f, 100.0f},
        {"CORMEX", 3, 0.0f, 0.0f},
        {"CORWIN", 5, 0.0f, 0.0f},
    };

    inline constexpr TadEpisodeComposition tadStorageEpisode7Building[] = {
        {"CORCK", 1, 25.0f, 25.0f},
    };

    // 14731.ted, owner block 0, tick 3455: capacity 1000.0f metal, 6000.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode8Finished[] = {
        {"ARMESTOR", 1, 0.0f, 5000.0f},
        {"ARMMEX", 3, 0.0f, 0.0f},
        {"ARMWIN", 7, 0.0f, 0.0f},
    };

    inline constexpr TadEpisodeComposition tadStorageEpisode8Building[] = {
        {"ARMESTOR", 1, 0.0f, 5000.0f},
    };

    // 14731.ted, owner block 0, tick 4055: capacity 1000.0f metal, 11000.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode9Finished[] = {
        {"ARMESTOR", 2, 0.0f, 5000.0f},
        {"ARMMEX", 3, 0.0f, 0.0f},
        {"ARMWIN", 7, 0.0f, 0.0f},
    };

    // 14731.ted, owner block 2, tick 3095: capacity 1125.0f metal, 1125.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode10Finished[] = {
        {"CORAK", 1, 0.0f, 0.0f},
        {"CORCK", 1, 25.0f, 25.0f},
        {"CORLAB", 1, 100.0f, 100.0f},
        {"CORMEX", 3, 0.0f, 0.0f},
        {"CORSOLAR", 3, 0.0f, 0.0f},
    };

    inline constexpr TadEpisodeComposition tadStorageEpisode10Building[] = {
        {"CORSOLAR", 1, 0.0f, 0.0f},
    };

    // 14731.ted, owner block 4, tick 2011: capacity 2000.0f metal, 1000.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode11Finished[] = {
        {"CORMEX", 3, 0.0f, 0.0f},
        {"CORMSTOR", 1, 1000.0f, 0.0f},
        {"CORSOLAR", 2, 0.0f, 0.0f},
    };

    inline constexpr TadEpisodeComposition tadStorageEpisode11Building[] = {
        {"CORSOLAR", 1, 0.0f, 0.0f},
    };

    // 14731.ted, owner block 4, tick 3090: capacity 2100.0f metal, 1100.0f energy.
    inline constexpr TadEpisodeComposition tadStorageEpisode12Finished[] = {
        {"CORMEX", 3, 0.0f, 0.0f},
        {"CORMSTOR", 1, 1000.0f, 0.0f},
        {"CORSOLAR", 3, 0.0f, 0.0f},
        {"CORVP", 1, 100.0f, 100.0f},
    };

    inline constexpr TadEpisodeComposition tadStorageEpisode12Building[] = {
        {"CORSOLAR", 1, 0.0f, 0.0f},
    };

    // clang-format off
    inline constexpr TadStorageEpisode tadStorageEpisodes[] = {
        {"14727.ted", 0, 0, 95,
            1000.0f, 1000.0f,
            nullptr, 0,
            tadStorageEpisode0Building, 1,
            1000.0f, 1000.0f, 984.0f, 891.60022f,
            0.0f, 0.0f, nullptr},
        {"14727.ted", 0, 2254, 2374,
            1000.0f, 1000.0f,
            tadStorageEpisode1Finished, 3,
            tadStorageEpisode1Building, 1,
            1100.0f, 1100.0f, 507.384827f, 1095.30566f,
            0.0f, 0.0f, nullptr},
        {"14727.ted", 0, 2974, 3094,
            1000.0f, 1000.0f,
            tadStorageEpisode2Finished, 4,
            nullptr, 0,
            1125.0f, 1125.0f, 448.78241f, 1012.5238f,
            0.0f, 0.0f, nullptr},
        {"14727.ted", 1, 2734, 2854,
            1000.0f, 1000.0f,
            tadStorageEpisode3Finished, 4,
            tadStorageEpisode3Building, 2,
            1125.0f, 1125.0f, 430.845093f, 299.696991f,
            0.0f, 0.0f, nullptr},
        {"14727.ted", 6, 1895, 2015,
            1000.0f, 1000.0f,
            tadStorageEpisode4Finished, 4,
            nullptr, 0,
            1000.0f, 6000.0f, 789.31134f, 649.017273f,
            0.0f, 0.0f, nullptr},
        {"14727.ted", 6, 4295, 4415,
            1000.0f, 1000.0f,
            tadStorageEpisode5Finished, 5,
            nullptr, 0,
            1100.0f, 6100.0f, 595.231567f, 4121.23486f,
            0.0f, 0.0f, nullptr},
        {"14727.ted", 7, 2253, 2373,
            1000.0f, 1000.0f,
            tadStorageEpisode6Finished, 3,
            tadStorageEpisode6Building, 1,
            1100.0f, 1100.0f, 534.906372f, 1100.0f,
            0.0f, 0.0f, nullptr},
        {"14727.ted", 7, 3333, 3453,
            1000.0f, 1000.0f,
            tadStorageEpisode7Finished, 5,
            tadStorageEpisode7Building, 1,
            1125.0f, 1125.0f, 478.470062f, 825.494019f,
            0.0f, 0.0f, nullptr},
        {"14731.ted", 0, 3335, 3455,
            1000.0f, 1000.0f,
            tadStorageEpisode8Finished, 3,
            tadStorageEpisode8Building, 1,
            1000.0f, 6000.0f, 968.959473f, 1150.09729f,
            0.0f, 0.0f, nullptr},
        {"14731.ted", 0, 3935, 4055,
            1000.0f, 1000.0f,
            tadStorageEpisode9Finished, 3,
            nullptr, 0,
            1000.0f, 11000.0f, 937.918945f, 1827.72876f,
            0.0f, 0.0f, nullptr},
        {"14731.ted", 2, 2975, 3095,
            1000.0f, 1000.0f,
            tadStorageEpisode10Finished, 5,
            tadStorageEpisode10Building, 1,
            1125.0f, 1125.0f, 307.429596f, 176.100403f,
            0.0f, 0.0f, nullptr},
        {"14731.ted", 4, 1891, 2011,
            1000.0f, 1000.0f,
            tadStorageEpisode11Finished, 3,
            tadStorageEpisode11Building, 1,
            2000.0f, 1000.0f, 647.085999f, 1000.0f,
            0.0f, 0.0f, nullptr},
        {"14731.ted", 4, 2970, 3090,
            1000.0f, 1000.0f,
            tadStorageEpisode12Finished, 4,
            tadStorageEpisode12Building, 1,
            2100.0f, 1100.0f, 121.899414f, 1100.0f,
            0.0f, 0.0f, nullptr},
    };
    // clang-format on
}
