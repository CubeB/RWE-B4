#include "ModLaunch.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        constexpr std::size_t maxManifestBytes = 1024 * 1024;
        constexpr std::size_t maxScriptBytes = 1024 * 1024;

        std::string readBoundedFile(const std::filesystem::path& path, std::size_t limit, const std::string& what)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Cannot open " + what + " " + path.string());
            }
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (text.size() > limit)
            {
                throw std::runtime_error(what + " is larger than 1 MiB: " + path.string());
            }
            return text;
        }
    }

    const std::vector<std::string>& supportedModFeatures()
    {
        static const std::vector<std::string> features = {"transform.definitions@1", "runtime.damage@1", "runtime.unit-state@1"};
        return features;
    }

    ModPackage readModPackageFile(const std::filesystem::path& manifestPath)
    {
        const auto text = readBoundedFile(manifestPath, maxManifestBytes, "manifest");
        nlohmann::json manifestValue;
        try
        {
            manifestValue = nlohmann::json::parse(text);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(manifestPath.string() + ": invalid JSON: " + e.what());
        }
        std::string implicitId;
        if (!manifestValue.is_object() || !manifestValue.contains("manifestVersion"))
        {
            implicitId = "legacy." + sha256Hex(text).substr(0, 16);
        }
        auto package = modPackageFromJson(manifestValue, text, manifestPath.string(), implicitId);
        if (!package)
        {
            throw std::runtime_error(package.getErr().describe());
        }
        // MOD-SDK-014: the entry script's bytes join the identity. The path
        // is the manifest's own normalized relative entry, so where the
        // package sits on disk still cannot change what is hashed.
        const auto& entry = package.get().manifest.entry;
        if (entry && package.get().manifest.kind != ModKind::Presentation)
        {
            const auto scriptPath = manifestPath.parent_path() / *entry;
            std::ifstream scriptFile(scriptPath, std::ios::binary);
            if (!scriptFile)
            {
                throw std::runtime_error(package.get().manifest.id + ": entry script " + *entry
                    + " is missing (looked for " + scriptPath.string() + ")");
            }
            const std::string script(
                (std::istreambuf_iterator<char>(scriptFile)), std::istreambuf_iterator<char>());
            if (script.size() > maxScriptBytes)
            {
                throw std::runtime_error(package.get().manifest.id + ": entry script is larger than 1 MiB");
            }
            package.get().entrySha256 = sha256Hex(script);
        }
        return std::move(package.get());
    }

    ModOptionSelections parseModOptionSelections(const std::vector<std::string>& selections)
    {
        ModOptionSelections result;
        for (const auto& text : selections)
        {
            const auto colon = text.find(':');
            const auto equals = text.find('=');
            if (colon == std::string::npos || equals == std::string::npos || colon == 0 || colon > equals
                || colon + 1 == equals || equals + 1 >= text.size())
            {
                throw std::runtime_error("a mod option must be <packageId>:<key>=<value>: " + text);
            }
            const auto id = text.substr(0, colon);
            const auto key = text.substr(colon + 1, equals - colon - 1);
            const auto raw = text.substr(equals + 1);
            nlohmann::json value;
            try
            {
                value = nlohmann::json::parse(raw);
            }
            catch (const std::exception&)
            {
                value = raw;
            }
            result[id][key] = value;
        }
        return result;
    }

    ModLaunchContent resolveModLaunchContent(
        const std::vector<ModPackage>& packages,
        const std::vector<std::filesystem::path>& manifestPaths,
        const ModOptionSelections& selections)
    {
        if (packages.size() != manifestPaths.size())
        {
            throw std::runtime_error("resolveModLaunchContent: one manifest path is needed per package");
        }
        ModResolutionEnvironment environment;
        environment.supportedFeatures = supportedModFeatures();
        const auto set = resolveModSet(packages, selections, environment);
        if (!set)
        {
            throw std::runtime_error(set.getErr().describe());
        }

        ModLaunchContent content;
        content.identity = set.get().identity;
        for (const auto& mod : set.get().mods)
        {
            ModLaunchPackage launch;
            launch.id = mod.manifest.id;
            launch.version = mod.manifest.versionText;
            launch.manifestSha256 = mod.manifestSha256;
            launch.entrySha256 = mod.entrySha256;
            launch.kind = modKindText(mod.manifest.kind);
            launch.features = mod.manifest.features;
            launch.resourceDirectories = mod.manifest.resourceDirectories;
            if (mod.manifest.entry)
            {
                launch.scriptName = mod.manifest.id + "/" + *mod.manifest.entry;
            }
            for (std::size_t i = 0; i < packages.size(); ++i)
            {
                if (packages[i].manifest.id == mod.manifest.id)
                {
                    if (mod.manifest.entry)
                    {
                        launch.scriptPath = (manifestPaths[i].parent_path() / *mod.manifest.entry).string();
                    }
                    break;
                }
            }
            launch.options = nlohmann::json::object();
            for (const auto& [key, value] : mod.options)
            {
                launch.options[key] = value;
            }
            content.packages.push_back(std::move(launch));
        }
        return content;
    }

    ModLaunchContent resolveModLaunchContent(
        const std::vector<std::filesystem::path>& manifestPaths,
        const ModOptionSelections& selections)
    {
        std::vector<ModPackage> packages;
        packages.reserve(manifestPaths.size());
        for (const auto& path : manifestPaths)
        {
            packages.push_back(readModPackageFile(path));
        }
        return resolveModLaunchContent(packages, manifestPaths, selections);
    }

    std::shared_ptr<const ModLaunchContent> matchRecordedModSet(
        const ModLaunchContent* recorded,
        std::shared_ptr<const ModLaunchContent> resolved)
    {
        const auto mismatch = describeModSetMismatch(resolved.get(), recorded);
        if (!mismatch.empty())
        {
            throw std::runtime_error("The --mod set does not match the recording: " + mismatch);
        }
        if (recorded == nullptr || recorded->identity.empty())
        {
            return nullptr;
        }
        return resolved;
    }

    std::shared_ptr<const ModLaunchContent> applyModArguments(
        const std::vector<std::string>& manifests,
        const std::vector<std::string>& options,
        std::shared_ptr<const ModLaunchContent> current,
        bool fromRecording)
    {
        if (manifests.empty() && options.empty())
        {
            return current;
        }
        if (manifests.empty())
        {
            throw std::runtime_error("--mod-option needs at least one --mod");
        }
        const std::vector<std::filesystem::path> manifestPaths(manifests.begin(), manifests.end());
        auto resolved = std::make_shared<const ModLaunchContent>(
            resolveModLaunchContent(manifestPaths, parseModOptionSelections(options)));
        if (fromRecording)
        {
            return matchRecordedModSet(current.get(), std::move(resolved));
        }
        return resolved;
    }

    void applyModResourceDirectories(PathMapping& mapping, const ModLaunchContent* content)
    {
        if (content == nullptr)
        {
            return;
        }
        for (const auto& package : content->packages)
        {
            for (const auto& [name, directory] : package.resourceDirectories)
            {
                if (!applyPathMappingDirectory(mapping, name, directory))
                {
                    throw std::runtime_error("Mod " + package.id + ": unknown resource directory \"" + name + "\"");
                }
            }
        }
    }
}
