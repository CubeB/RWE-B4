#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <rwe/util/Result.h>
#include <rwe/util/Sha256.h>
#include <string>
#include <string_view>
#include <vector>

namespace rwe
{
    // MOD-SDK-002, docs/MODDING-SDK-FINDINGS.md. RWE-original authoring
    // foundation; no original-game counterpart.
    constexpr int currentModManifestVersion = 1;
    constexpr int currentModApiVersion = 1;
    constexpr int currentResolvedSetFormatVersion = 1;

    enum class ModErrorCode
    {
        InvalidManifest,
        UnsupportedManifestVersion,
        MissingField,
        InvalidField,
        UnsupportedVersion,
        UnsupportedConstraint,
        UnsupportedApi,
        UnsupportedFeature,
        UnsupportedKind,
        DuplicateId,
        UnknownPackage,
        MissingDependency,
        UnsatisfiedDependency,
        UnknownOrderingReference,
        DependencyCycle,
        ExclusiveConflict,
        UnknownOption,
        InvalidOptionValue,
        MissingOptionValue,
    };

    struct ModManifestError
    {
        ModErrorCode code = ModErrorCode::InvalidManifest;
        std::string packageId;
        std::string detail;

        std::string describe() const;
    };

    struct ModVersion
    {
        std::uint32_t major = 0;
        std::uint32_t minor = 0;
        std::uint32_t patch = 0;
    };

    bool operator==(const ModVersion& a, const ModVersion& b);
    bool operator<(const ModVersion& a, const ModVersion& b);

    // Format 1 accepts major.minor.patch only. Pre-release and build metadata
    // are refused rather than half-understood.
    Result<ModVersion, ModManifestError> parseModVersion(std::string_view text);
    std::string modVersionText(const ModVersion& version);

    // Supports *, an exact 1.2.3, ^1.2.3, ~1.2.3, and whitespace-separated
    // comparators such as ">=1.2.0 <2.0.0". A range is a conjunction.
    Result<bool, ModManifestError> satisfiesModConstraint(const ModVersion& version, std::string_view constraint);

    enum class ModKind
    {
        Mutator,
        Content,
        Ai,
        Maps,
        Presentation,
        Base,
        Assets,
    };

    std::string modKindText(ModKind kind);
    std::optional<ModKind> modKindFromText(std::string_view text);

    enum class ModOptionType
    {
        Number,
        Boolean,
        String,
    };

    struct ModOption
    {
        std::string key;
        ModOptionType type = ModOptionType::Number;
        nlohmann::json defaultValue;
        bool hasDefault = false;
        std::optional<double> minimum;
        std::optional<double> maximum;
        std::vector<std::string> choices;
    };

    struct ModDependency
    {
        std::string id;
        std::string version = "*";
    };

    struct ModManifest
    {
        int manifestVersion = currentModManifestVersion;
        // Absent manifestVersion means a legacy descriptor: display metadata
        // with no resolution semantics and a caller-supplied stable id.
        bool legacy = false;
        std::string id;
        std::string name;
        std::string versionText;
        ModVersion version;
        ModKind kind = ModKind::Content;
        int api = currentModApiVersion;
        std::optional<std::string> entry;
        // "requires" in the JSON. `requires` is a C++20 keyword, so the
        // member cannot carry the wire name.
        std::vector<ModDependency> dependencies;
        std::vector<std::string> after;
        std::vector<std::string> before;
        std::vector<std::string> features;
        std::vector<std::string> exclusive;
        std::vector<std::string> replaces;
        std::vector<ModOption> options;
        // Resource-directory overrides, e.g. {"weapons": "weaponP"} for a
        // package that renames its roots (ProTA uses downloadP, gamedatP,
        // guiP, unitpicsP and weaponP). Paths are relative to the package
        // root and are part of the resolved identity.
        std::map<std::string, std::string> resourceDirectories;
    };

    // The PathMapping fields a manifest may override. Kept as a free
    // predicate so the parser and tests agree on the vocabulary.
    bool isKnownResourceDirectory(const std::string& name);

    struct ModPackage
    {
        ModManifest manifest;
        std::string manifestSha256;
        // Diagnostic only. Excluded from the resolved-set identity so that a
        // renamed folder or a different download path cannot refuse a save or
        // a multiplayer launch (MOD-SDK-003).
        std::string source;
    };

    Result<ModManifest, ModManifestError> parseModManifest(
        const nlohmann::json& value,
        const std::string& source,
        const std::string& implicitLegacyId = {});

    // The manifest hash is taken over the raw author bytes, so whitespace and
    // key order in the author file are part of what peers must agree on.
    Result<ModPackage, ModManifestError> modPackageFromJson(
        const nlohmann::json& value,
        const std::string& rawManifestText,
        const std::string& source,
        const std::string& implicitLegacyId = {});

    struct ModResolutionEnvironment
    {
        int apiVersion = currentModApiVersion;
        std::vector<std::string> supportedFeatures;
        // MOD-10 supplies the presentation runtime, so a presentation package
        // resolves like any other kind. It takes no part in the simulation.
        std::vector<ModKind> supportedKinds = {
            ModKind::Mutator,
            ModKind::Content,
            ModKind::Ai,
            ModKind::Maps,
            ModKind::Presentation,
            ModKind::Base,
            ModKind::Assets,
        };
    };

    struct ResolvedMod
    {
        ModManifest manifest;
        std::string manifestSha256;
        std::map<std::string, nlohmann::json> options;
        // Only the keys this package ends up owning, after `replaces` is
        // applied in load order.
        std::vector<std::string> ownedExclusiveKeys;
    };

    struct ResolvedModSet
    {
        int formatVersion = currentResolvedSetFormatVersion;
        int apiVersion = currentModApiVersion;
        std::vector<ResolvedMod> mods;
        std::string identity;
    };

    using ModOptionSelections = std::map<std::string, std::map<std::string, nlohmann::json>>;

    Result<ResolvedModSet, ModManifestError> resolveModSet(
        std::vector<ModPackage> packages,
        const ModOptionSelections& selectedOptions,
        const ModResolutionEnvironment& environment);

    // Canonical resolved-set JSON, without the derived identity field.
    nlohmann::json resolvedModSetJson(const ResolvedModSet& set);

    // Compact JSON with sorted keys, no insignificant whitespace.
    std::string canonicalJson(const nlohmann::json& value);
}
