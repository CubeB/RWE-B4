#pragma once

#include <filesystem>
#include <memory>
#include <rwe/PathMapping.h>
#include <rwe/mod/ModTransform.h>
#include <rwe/setup/ModManifest.h>
#include <string>
#include <vector>

namespace rwe
{
    // MOD-SDK-015, docs/MODDING-SDK-FINDINGS.md. Turning author manifests on
    // disk into the launch form, shared by rwe_setup's offline commands and
    // the engine's own --mod switch so the two cannot resolve a set
    // differently. RWE-original; every function throws std::runtime_error
    // with a message naming the package or argument at fault.

    // The API features this build provides. A manifest that requires
    // anything else is refused rather than half-loaded.
    const std::vector<std::string>& supportedModFeatures();

    // Reads one author manifest (bounded at 1 MiB, PACKAGE-003's metadata
    // limit) and, for a simulation package with an entry, that script's
    // bytes into entrySha256 (MOD-SDK-014). A legacy descriptor borrows an
    // id derived from its bytes (MOD-SDK-002).
    ModPackage readModPackageFile(const std::filesystem::path& manifestPath);

    // Parses "<packageId>:<key>=<value>" selections. A value that parses as
    // JSON is taken as JSON (2.5, true); anything else is a string.
    ModOptionSelections parseModOptionSelections(const std::vector<std::string>& selections);

    // Resolves packages read from `manifestPaths` (one per package, same
    // order) into the launch form: the resolved identity, then each package
    // in load order with its local script path.
    ModLaunchContent resolveModLaunchContent(
        const std::vector<ModPackage>& packages,
        const std::vector<std::filesystem::path>& manifestPaths,
        const ModOptionSelections& selections);

    // readModPackageFile on each path, then resolveModLaunchContent.
    ModLaunchContent resolveModLaunchContent(
        const std::vector<std::filesystem::path>& manifestPaths,
        const ModOptionSelections& selections);

    // The --mod and --mod-option arguments, as rwe and ai_arena take them.
    // With neither, `current` comes back unchanged. Otherwise the manifests
    // are resolved; for a fresh game the result replaces `current`, and for
    // a replay or a save (`fromRecording`) it must be the set `current`
    // recorded, via matchRecordedModSet.
    std::shared_ptr<const ModLaunchContent> applyModArguments(
        const std::vector<std::string>& manifests,
        const std::vector<std::string>& options,
        std::shared_ptr<const ModLaunchContent> current,
        bool fromRecording);

    // Applies each package's resourceDirectories (ProTA's weaponP, say) to
    // `mapping`, in load order. Throws naming the package for a directory
    // name PathMapping does not have. LoadingScene and ai_arena both use it.
    void applyModResourceDirectories(PathMapping& mapping, const ModLaunchContent* content);

    // For a replay or a save that recorded a mod set: the locally resolved
    // set when it is the recorded one, so the recording can run with this
    // machine's script paths. Throws with describeModSetMismatch's message
    // when it is not. Recording no mods and resolving none is fine and
    // returns null.
    std::shared_ptr<const ModLaunchContent> matchRecordedModSet(
        const ModLaunchContent* recorded,
        std::shared_ptr<const ModLaunchContent> resolved);
}
