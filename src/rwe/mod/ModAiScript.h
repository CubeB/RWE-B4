#pragma once

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace rwe
{
    struct ModLaunchContent;

    /**
     * MOD-09, docs/MODDING-SDK-FINDINGS.md MOD-SDK-010. One AI package as the
     * launch path resolved it, ready to hand to ModAiController.
     *
     * The script's source is carried here rather than read per tick: a
     * lockstep peer must run the exact bytes the resolved set identifies, not
     * whatever is on disk when the first tick arrives. RWE-original: the
     * original game has no scripted AI.
     *
     * The seed is derived before launch from the resolved-set identity, so two
     * peers that agree on the content also agree on the stream. It is mixed
     * with the seat's single simulation-RNG draw in AiSeat.cpp; neither source
     * alone decides the controller's sequence.
     */
    constexpr int currentModAiApiVersion = 1;

    struct ModAiScript
    {
        std::string packageId;
        std::string scriptName;
        std::string source;
        nlohmann::json options = nlohmann::json::object();
        std::uint32_t seed{0};
    };

    /**
     * Finds the AI package in `content` and reads its entry script.
     *
     * `packageId` empty means "the first package whose manifest kind is ai",
     * deterministically the first in resolved load order. A named package must
     * exist and must be an ai package.
     *
     * Throws std::runtime_error with a message naming the package when the
     * content is absent, no ai package was resolved, the recorded set has no
     * local script path, the file cannot be read, or it exceeds the 1 MiB cap
     * every package script shares. Callers must not fall back to Classic: a
     * silently different opponent is worse than a refused launch.
     */
    std::shared_ptr<const ModAiScript> resolveModAiScript(
        const ModLaunchContent* content,
        const std::string& packageId,
        std::uint32_t seed);

    /** Reads one package script with the shared 1 MiB cap. */
    std::string readModAiScriptFile(const std::string& packageId, const std::string& path);

    /**
     * The first 32 bits of the resolved-set identity, or 0 when there is no
     * set. Kept here so the loader and a future replay/join path derive the
     * same seed from the same bytes.
     */
    std::uint32_t modAiSeedFromContent(const ModLaunchContent* content);
}
