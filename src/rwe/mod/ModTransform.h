#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace rwe
{
    // MOD-04/MOD-05, docs/MODDING-SDK-FINDINGS.md. The pre-match transform
    // surface: a sandboxed Lua script reads and writes the small set of unit
    // values a balance mutator needs. RWE-original.
    constexpr int currentModTransformApiVersion = 1;

    // Format 1 exposes exactly the integer cost fields. The keys are the FBI
    // key names, so the author sees the same names as the data files.
    bool isSupportedTransformKey(const std::string& key);

    struct ModUnitRecord
    {
        std::string name;
        // Sorted by key. Values are the current integers, held as doubles so
        // a script can scale them; the engine rounds once when applying.
        std::map<std::string, double> values;
    };

    struct ModTransformChange
    {
        std::string unit;
        std::string key;
        double before = 0;
        double after = 0;
    };

    struct ModTransformLimits
    {
        // Counted in executed Lua VM instructions, not wall time, so every
        // peer reaches the same fault at the same point.
        std::uint64_t instructionBudget = 20000000;
        std::size_t memoryBudget = 8 * 1024 * 1024;
        std::size_t maxChanges = 100000;
        std::size_t maxLogEntries = 1000;
        std::size_t maxLogLength = 1024;
    };

    struct ModTransformScript
    {
        std::string packageId;
        // Used in diagnostics and the Lua chunk name, e.g.
        // "com.example.cost/scripts/main.lua".
        std::string scriptName;
        std::string source;
        // Resolved manifest options, exposed to the script as rwe.options.
        nlohmann::json options = nlohmann::json::object();
    };

    struct ModTransformResult
    {
        bool ok = false;
        std::vector<ModTransformChange> changes;
        std::vector<std::string> log;
        std::string error;
        std::uint64_t instructionsUsed = 0;
    };

    // One package as the launch path resolved it. Script source is read at
    // load, not carried here: GameParameters is copied widely and a save or
    // replay records only identity and fingerprints.
    struct ModLaunchPackage
    {
        std::string id;
        std::string version;
        std::string manifestSha256;
        std::string scriptName;
        std::string scriptPath;
        nlohmann::json options = nlohmann::json::object();
        // "mutator", "ai", "content", ... from the resolved manifest. Empty
        // in a record written before kind existed; a script-bearing package
        // is then treated as a pre-match mutator.
        std::string kind;
        // Required API features. A package asking for a "runtime." feature
        // is a persistent runtime script, not a pre-match transform.
        std::vector<std::string> features;
        // Resource-directory overrides for this package, e.g. ProTA's
        // weaponP/guiP roots. Sorted by map order.
        std::map<std::string, std::string> resourceDirectories;
        // SHA-256 of the entry script as it was resolved (MOD-SDK-014).
        // Recorded in saves and replays; every reader checks the bytes it
        // runs against it, so a script edited after resolution is refused.
        // Last, so the positional initialisers in tests stay valid.
        std::string entrySha256;
    };

    struct ModLaunchContent
    {
        // The resolved-set identity from resolveModSet. Empty means the
        // unmodded base game.
        std::string identity;
        // In load order.
        std::vector<ModLaunchPackage> packages;
    };

    // Empty when the two sets are compatible. Otherwise a message naming the
    // first difference, in load order, for a save, a replay or a peer that
    // does not hold the content the other side recorded.
    std::string describeModSetMismatch(const ModLaunchContent* expected, const ModLaunchContent* recorded);

    // Save and replay metadata. Machine-local script paths are deliberately
    // not written: a save moved to another machine must not remember where
    // this one kept its files.
    nlohmann::json modLaunchContentToJson(const ModLaunchContent* content);
    std::shared_ptr<const ModLaunchContent> modLaunchContentFromJson(const nlohmann::json& value);

    // Reads a package's entry script from its resolved path and refuses it
    // unless its bytes still hash to entrySha256. Throws std::runtime_error
    // naming the package. Every consumer -- transform, runtime, AI and
    // presentation -- reads scripts through this, so none can run bytes the
    // identity does not cover.
    std::string readVerifiedModScript(const ModLaunchPackage& package);

    // Applies one script to `units` in place. A failed script leaves `units`
    // untouched: writes are journaled and committed only on success, so a
    // broken mutator cannot half-apply and then be ignored.
    ModTransformResult runUnitTransform(
        const ModTransformScript& script,
        std::vector<ModUnitRecord>& units,
        const ModTransformLimits& limits = {});
}
