#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace rwe
{
    struct ModLaunchContent;

    /**
     * MOD-10, docs/MODDING-SDK-FINDINGS.md MOD-SDK-013. One resolved
     * presentation package, with the bytes it ran and the path a developer
     * reload will read again.
     *
     * RWE-original: the original game has no presentation scripts. The
     * package itself is carried by the existing resolved set, so its identity
     * and order are already recorded there.
     */
    struct PresentationPackage
    {
        std::string packageId;
        std::string scriptName;
        std::string scriptPath;
        std::string source;
        nlohmann::json options = nlohmann::json::object();
    };

    /**
     * Every package in `content` whose manifest kind is `presentation`, in
     * resolved load order, with its entry script read from disk.
     *
     * A recorded set from a save or replay carries identity but no machine
     * paths; such a package is skipped rather than refusing the game,
     * because presentation is not simulation state and peers do not have to
     * run the same one. A package whose path is known but unreadable throws:
     * that is a broken install at launch, not an expected absence.
     *
     * `seed`-bearing scripts are not needed here: a presentation mod draws
     * from its own generator, never the simulation's, and nothing it does is
     * recorded for reproduction.
     */
    std::vector<PresentationPackage> resolvedPresentationPackages(const ModLaunchContent* content);

    /** Reads one script with the same 1 MiB cap every mod entry shares. */
    std::string readPresentationScript(const std::string& packageId, const std::string& path);

    /** True when `content` names at least one presentation package. */
    bool hasPresentationPackages(const ModLaunchContent* content);
}
