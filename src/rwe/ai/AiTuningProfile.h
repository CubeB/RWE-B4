#pragma once

#include <rwe/sim/SimScalar.h>
#include <string>

namespace rwe
{
    // Difficulty tiers, mapped 1:1 to default profile bundles.
    // The four tiers are described in docs/ai-architecture-proposal.md §1.
    // Phase 1 always uses Standard.
    enum class AiDifficulty
    {
        Easy,
        Standard,
        Hard,
        Brutal,
    };

    // Tunable knobs that control AI behaviour.
    //
    // Per the design (docs/ai-architecture-proposal.md §1, §6, §8, §12-Q2),
    // cheating is exposed externally as a single "Brutal" tier label, but
    // internally the cheat axes are independent flags so future modders can
    // mix-and-match (e.g. "Hard with omniscience, no resource bonus").
    //
    // All resource/scoring values are expressed as SimScalar so that AI
    // reasoning shares the deterministic primitives the rest of the sim uses.
    // A future migration of SimScalar to true fixed-point will sweep AI
    // along for free.
    struct AiTuningProfile
    {
        // Display label / identity. Currently informational only; a future
        // BuildManager will use this to look up an opening book by name.
        std::string name{"DEFAULT"};

        AiDifficulty difficulty{AiDifficulty::Standard};

        // ---- Phase 1 build-order tunables ----

        // How many metal extractors the opening tries to land before pivoting.
        int openingMetalExtractorCount{3};

        // How many solar collectors (or equivalent base energy plants) the
        // opening tries to land before pivoting.
        int openingSolarCount{1};

        // Maximum world-distance the AI will hunt for a metal patch around
        // its commander when picking a mex build site. Conservative to keep
        // the Phase 1 commander idle-near-base behaviour predictable.
        SimScalar maxMexSearchRadius{512_ss};

        // Build sites are tried on a square grid sampled at this spacing.
        // Smaller = more candidate sites considered per tick.
        SimScalar buildSiteGridSpacing{16_ss};

        // ---- Phase 1 cadence tunables ----

        // BuildManager only emits commands on ticks that are a multiple of
        // this value. Acts as both a rate-limit and an order-deduplication
        // shield (if the previous order is still being processed).
        int buildPlannerTickInterval{30};

        // ---- Difficulty-axis "cheat" flags (Brutal exposes these in UI) ----

        // When true, PerceivedWorldModel ignores fog of war.
        // No-op today (RWE has no LOS yet) but stabilises the API.
        bool cheatModeOmniscient{false};

        // Multiplier applied to AI metal/energy income each tick.
        // 1.0 = no cheat. >1.0 = AI receives bonus resources (Brutal default).
        SimScalar resourceCheatMultiplier{1_ss};
    };

    // Build the canonical Standard profile. Phase 1 wires this in by default;
    // future phases will load profiles from data/ai/profiles/<name>.tdf
    // (TDF chosen per docs/ai-architecture-proposal.md §12-Q3 — the project
    // already ships a TDF reader and TA modders are familiar with the format).
    AiTuningProfile makeDefaultStandardProfile();

    // Brutal profile: Standard knobs + cheat flags flipped on.
    // Hidden in Phase 1 (no UI selector), but the function exists so that
    // when the difficulty UI lands the controller does not need new code.
    AiTuningProfile makeDefaultBrutalProfile();
}
