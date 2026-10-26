#include "PackageLibrary.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <random>
#include <regex>
#include <rwe/ai/classic/ClassicAiScript.h>
#include <rwe/io/hpi/HpiArchive.h>
#include <rwe/setup/TaInstall.h>
#include <rwe/util/rwe_string.h>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rwe
{
    PackageLibraryPaths packageLibraryPaths(const fs::path& userDataPath)
    {
        const auto root = userDataPath / "Library";
        return {
            root,
            root / "registry.json",
            root / "packages",
            userDataPath / "mods",
            userDataPath / "MapPacks",
            root / "staging"};
    }

    namespace
    {
        constexpr std::uintmax_t MaxMetadataBytes = 1024 * 1024;
        constexpr std::uintmax_t MaxRegistryBytes = 16 * 1024 * 1024;
        constexpr std::size_t MaxScanEntries = 100000;
        constexpr std::size_t MaxRoots = 256;
        constexpr int MaxScanDepth = 12;

        std::string pathText(const fs::path& path)
        {
            const auto value = path.generic_u8string();
            return std::string(value.begin(), value.end());
        }

        bool oneOf(const std::string& value, const std::initializer_list<const char*>& choices)
        {
            return std::any_of(choices.begin(), choices.end(), [&](const char* choice) { return value == choice; });
        }

        std::string requiredText(const json& value, const char* key, std::size_t maxSize = 4096)
        {
            if (!value.contains(key) || !value.at(key).is_string())
            {
                throw std::runtime_error(std::string("Package registry requires string: ") + key);
            }
            auto text = value.at(key).get<std::string>();
            if (text.empty() || text.size() > maxSize || text.find('\0') != std::string::npos
                || text.find_first_not_of(" \t\r\n") == std::string::npos)
            {
                throw std::runtime_error(std::string("Invalid package registry string: ") + key);
            }
            return text;
        }

        bool validId(const std::string& id)
        {
            return (id.size() == 34 || id.size() == 66) && id.substr(0, 2) == "p-"
                && id.find_first_not_of("0123456789abcdef", 2) == std::string::npos;
        }

        bool relativeAssetPath(const std::string& path)
        {
            if (path.empty() || path.front() == '/' || path.back() == '/'
                || path.find_first_of("\\:") != std::string::npos || path.find('\0') != std::string::npos)
            {
                return false;
            }
            std::istringstream parts(path);
            std::string part;
            while (std::getline(parts, part, '/'))
            {
                if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' '
                    || part.find_first_of("<>\"|?*") != std::string::npos)
                {
                    return false;
                }
                const auto base = toUpper(part.substr(0, part.find('.')));
                if (oneOf(base, {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"})
                    || (base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0)
                        && base[3] >= '1' && base[3] <= '9'))
                {
                    return false;
                }
            }
            return true;
        }

        std::string readText(const fs::path& path, std::uintmax_t limit)
        {
            if (fs::file_size(path) > limit)
            {
                throw std::runtime_error("Metadata exceeds size limit: " + pathText(path));
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw std::runtime_error("Cannot read " + pathText(path));
            }
            std::string text(static_cast<std::size_t>(limit + 1), '\0');
            stream.read(text.data(), static_cast<std::streamsize>(text.size()));
            const auto count = stream.gcount();
            if (stream.bad() || static_cast<std::uintmax_t>(count) > limit)
            {
                throw std::runtime_error("Cannot read bounded metadata: " + pathText(path));
            }
            text.resize(static_cast<std::size_t>(count));
            return text;
        }

        bool taArchive(const fs::path& path)
        {
            auto extension = toUpper(path.extension().string());
            for (const auto& supported : taArchiveExtensions())
            {
                if (extension == toUpper(supported))
                {
                    return true;
                }
            }
            return false;
        }

        bool container(const fs::path& path)
        {
            return oneOf(toUpper(path.extension().string()), {".ZIP", ".RAR", ".7Z"});
        }

        bool resourceDirectory(const std::string& name)
        {
            return oneOf(toUpper(name), {"AI", "AE", "UNITS", "UNITSE", "WEAPONS", "WEAPONE", "MAPS",
                "GAMEDATA", "GAMEDATE", "GUIS", "GUIE", "DOWNLOAD", "DOWNLOADSE", "UNITPICS", "UNITPICE",
                "FEATURES", "ANIMS", "OBJECTS3D", "TEXTURES", "SCRIPTS", "SOUNDS", "PALETTES", "FONTS", "BITMAPS", "CAMPS"});
        }

        bool documentationFile(const fs::path& path)
        {
            const auto name = toUpper(path.filename().string());
            return name.find("README") != std::string::npos || name.find("READ_ME") != std::string::npos
                || name.find("CHANGELOG") != std::string::npos || name.compare(0, 5, "GOLD_") == 0;
        }

        struct Inventory
        {
            std::map<std::string, std::string> files;
            json archives = json::array();
            json warnings = json::array();
        };

        void inventoryArchive(const HpiArchive::Directory& directory, const std::string& prefix, Inventory& inventory)
        {
            for (const auto& entry : directory.entries)
            {
                if (inventory.files.size() >= MaxScanEntries)
                {
                    throw std::runtime_error("Package archive inventory limit reached");
                }
                const auto name = prefix + entry.name;
                if (const auto* nested = std::get_if<HpiArchive::Directory>(&entry.data))
                {
                    inventoryArchive(*nested, name + "/", inventory);
                }
                else
                {
                    inventory.files.emplace(toUpper(name), name);
                }
            }
        }

        bool hasPrefix(const Inventory& inventory, const std::string& prefix)
        {
            const auto it = inventory.files.lower_bound(prefix);
            return it != inventory.files.end() && it->first.compare(0, prefix.size(), prefix) == 0;
        }

        bool isAiText(const std::string& text)
        {
            const auto directives = parseClassicScript(text);
            return std::any_of(directives.begin(), directives.end(), [](const auto& directive) {
                return directive.kind != ClassicScriptDirective::Kind::Plan && !directive.args.empty();
            });
        }

        json describeRoot(const fs::path& root, const std::vector<fs::path>& looseFiles, const std::vector<fs::path>& archives)
        {
            Inventory inventory;
            for (const auto& archivePath : archives)
            {
                try
                {
                    std::ifstream stream(archivePath, std::ios::binary);
                    if (!stream)
                    {
                        throw std::runtime_error("Cannot open archive");
                    }
                    HpiArchive archive(&stream);
                    inventoryArchive(archive.root(), "", inventory);
                    inventory.archives.push_back({{"path", pathText(archivePath)}, {"bytes", fs::file_size(archivePath)}});
                }
                catch (const std::exception& e)
                {
                    inventory.warnings.push_back("Unreadable archive " + pathText(archivePath) + ": " + e.what());
                }
            }

            json metadata = json::object();
            std::string version;
            bool plainAi = false;
            for (const auto& file : looseFiles)
            {
                const auto name = pathText(file.lexically_relative(root));
                inventory.files.emplace(toUpper(name), name);
                const auto filename = toUpper(file.filename().string());
                try
                {
                    if (filename == "RWE_MOD.JSON")
                    {
                        metadata = json::parse(readText(file, MaxMetadataBytes));
                        if (!metadata.is_object())
                        {
                            throw std::runtime_error("rwe_mod.json must be an object");
                        }
                        for (const auto* field : {"name", "shortname", "version", "author"})
                        {
                            if (metadata.contains(field) && !metadata[field].is_null()
                                && !(metadata[field].is_string() && metadata[field].get<std::string>().empty()))
                            {
                                requiredText(metadata, field, 200);
                            }
                        }
                    }
                    else if (file.parent_path() == root && filename.compare(0, 5, "GOLD_") == 0 && toUpper(file.extension().string()) == ".TXT")
                    {
                        const auto text = readText(file, MaxMetadataBytes);
                        std::smatch match;
                        if (std::regex_search(text, match, std::regex("TAESC RELEASE GOLD[ \\t]*([0-9]+(?:\\.[0-9]+){1,3})", std::regex::icase)))
                        {
                            version = match[1];
                        }
                    }
                    else if (file.parent_path() == root && toUpper(file.extension().string()) == ".TXT"
                        && !documentationFile(file))
                    {
                        plainAi = isAiText(readText(file, MaxMetadataBytes)) || plainAi;
                    }
                }
                catch (const std::exception& e)
                {
                    inventory.warnings.push_back("Metadata " + pathText(file) + ": " + e.what());
                }
            }

            const auto hasUnits = hasPrefix(inventory, "UNITS/") || hasPrefix(inventory, "UNITSE/");
            const auto hasWeapons = hasPrefix(inventory, "WEAPONS/") || hasPrefix(inventory, "WEAPONE/");
            const auto hasAi = plainAi || hasPrefix(inventory, "AI/") || hasPrefix(inventory, "AE/");
            const auto escalation = hasPrefix(inventory, "AE/") && hasPrefix(inventory, "UNITSE/")
                && hasPrefix(inventory, "WEAPONE/") && hasPrefix(inventory, "GAMEDATE/");
            bool base1 = false;
            bool base2 = false;
            for (const auto& path : archives)
            {
                base1 = base1 || toUpper(path.filename().string()) == "TOTALA1.HPI";
                base2 = base2 || toUpper(path.filename().string()) == "TOTALA2.HPI";
            }

            json maps = json::array();
            std::map<std::string, json> pairs;
            for (const auto& [upperName, originalName] : inventory.files)
            {
                const auto extension = toUpper(fs::path(upperName).extension().string());
                if (!oneOf(extension, {".OTA", ".TNT"}))
                {
                    continue;
                }
                const auto key = upperName.substr(0, upperName.size() - 4);
                if (!pairs.count(key))
                {
                    pairs[key] = {{"name", pathText(fs::path(originalName).stem())}, {"validation", "not-run"}};
                }
                pairs[key][extension == ".OTA" ? "ota" : "tnt"] = originalName;
            }
            for (auto& [key, pair] : pairs)
            {
                (void)key;
                pair["paired"] = pair.contains("ota") && pair.contains("tnt");
                maps.push_back(std::move(pair));
            }

            std::string kind = base1 && base2 ? "base" : hasUnits || hasWeapons ? "mod"
                : hasAi ? "ai" : !maps.empty() ? "maps" : inventory.files.empty() ? "unknown" : "assets";
            std::string name = pathText(root.filename());
            if (name.empty())
            {
                name = "Unnamed package";
            }
            std::string recognition = "folder-name";
            if (base1 && base2)
            {
                name = "Total Annihilation";
                recognition = "base-archive-layout";
            }
            if (escalation)
            {
                name = "Total Annihilation: Escalation";
                recognition = "escalation-resource-layout";
            }
            if (metadata.is_object())
            {
                for (const auto* field : {"shortname", "name"})
                {
                    if (metadata.contains(field) && metadata[field].is_string() && !metadata[field].get<std::string>().empty())
                    {
                        name = metadata[field].get<std::string>();
                        recognition = "rwe_mod.json";
                    }
                }
                if (metadata.contains("version") && metadata["version"].is_string())
                {
                    version = metadata["version"].get<std::string>();
                }
            }
            json directories = json::object();
            if (escalation)
            {
                for (const auto& [resource, directory] : std::map<std::string, std::string>{
                         {"ai", "ae"}, {"units", "unitse"}, {"weapons", "weapone"}, {"gamedata", "gamedate"},
                         {"guis", "guie"}, {"downloads", "downloadse"}, {"unitpics", "unitpice"}})
                {
                    if (hasPrefix(inventory, toUpper(directory) + "/"))
                    {
                        directories[resource] = directory;
                    }
                }
            }
            return {{"name", name}, {"kind", kind}, {"version", version.empty() ? json(nullptr) : json(version)},
                {"author", metadata.is_object() && metadata.contains("author") && metadata["author"].is_string()
                    && !metadata["author"].get<std::string>().empty() ? metadata["author"] : json(nullptr)},
                {"root", pathText(root)}, {"recognition", recognition}, {"compatibility", "unverified"},
                {"resourceDirectories", directories}, {"archives", inventory.archives}, {"maps", maps},
                {"warnings", inventory.warnings}, {"fileCount", inventory.files.size()}};
        }
    }

    json emptyPackageRegistry()
    {
        return {{"schemaVersion", 1}, {"packages", json::array()}};
    }

    void validatePackageRegistry(const json& registry)
    {
        if (!registry.is_object() || !registry.contains("schemaVersion") || !registry["schemaVersion"].is_number_integer()
            || registry["schemaVersion"] != 1 || !registry.contains("packages") || !registry["packages"].is_array())
        {
            throw std::runtime_error("Unsupported or malformed package registry schema");
        }
        std::set<std::string> ids;
        for (const auto& entry : registry["packages"])
        {
            const auto id = requiredText(entry, "id", 66);
            if (!validId(id) || !ids.insert(id).second)
            {
                throw std::runtime_error("Invalid or duplicate package ID: " + id);
            }
            requiredText(entry, "name", 200);
            const auto kind = requiredText(entry, "kind");
            if (!oneOf(kind, {"base", "mod", "ai", "maps", "assets"}))
            {
                throw std::runtime_error("Unknown package kind: " + kind);
            }
            if (!oneOf(requiredText(entry, "state"), {"available", "installed", "needs-attention"}))
            {
                throw std::runtime_error("Unknown package installation state");
            }
            requiredText(entry, "source");
            if (!entry.contains("managed") || !entry["managed"].is_boolean())
            {
                throw std::runtime_error("Package ownership must be explicit");
            }
            if (entry["managed"].get<bool>())
            {
                if (requiredText(entry, "contentPath") != "packages/" + id)
                {
                    throw std::runtime_error("Managed content path must match its package ID");
                }
            }
            else
            {
                const auto external = requiredText(entry, "externalPath");
                if (!fs::path(std::u8string(external.begin(), external.end())).is_absolute())
                {
                    throw std::runtime_error("Unmanaged package path must be absolute");
                }
            }
            if (entry.contains("version") && !entry["version"].is_null() && !entry["version"].is_string())
            {
                throw std::runtime_error("Package version must be a string or null");
            }
            if (!oneOf(requiredText(entry, "compatibility"), {"unverified", "load-tested", "unsupported"}))
            {
                throw std::runtime_error("Unknown package compatibility state");
            }
            if (!entry.contains("dependencies") || !entry["dependencies"].is_array()
                || !entry.contains("files") || !entry["files"].is_array()
                || !entry.contains("resourceDirectories") || !entry["resourceDirectories"].is_object())
            {
                throw std::runtime_error("Package dependencies, files and resource directories are required");
            }
            std::set<std::string> dependencies;
            for (const auto& dependency : entry["dependencies"])
            {
                if (!dependency.is_string() || !validId(dependency.get<std::string>()) || dependency == id
                    || !dependencies.insert(dependency.get<std::string>()).second)
                {
                    throw std::runtime_error("Invalid package dependency");
                }
            }
            for (const auto& directory : entry["resourceDirectories"].items())
            {
                if (!directory.value().is_string() || !relativeAssetPath(directory.value().get<std::string>()))
                {
                    throw std::runtime_error("Invalid package resource directory");
                }
            }
            std::set<std::string> paths;
            for (const auto& file : entry["files"])
            {
                const auto path = requiredText(file, "path");
                const auto hash = requiredText(file, "sha256", 64);
                if (!relativeAssetPath(path) || !paths.insert(toUpper(path)).second || hash.size() != 64
                    || hash.find_first_not_of("0123456789abcdef") != std::string::npos || !file.contains("size")
                    || !file["size"].is_number_integer() || file["size"] < 0)
                {
                    throw std::runtime_error("Invalid package file manifest");
                }
            }
        }
    }

    json loadPackageRegistry(const fs::path& filename)
    {
        if (!fs::exists(filename))
        {
            return emptyPackageRegistry();
        }
        auto registry = json::parse(readText(filename, MaxRegistryBytes));
        validatePackageRegistry(registry);
        return registry;
    }

    void savePackageRegistry(const fs::path& filename, const json& registry)
    {
        validatePackageRegistry(registry);
        const auto text = registry.dump(2) + "\n";
        if (text.size() > MaxRegistryBytes)
        {
            throw std::runtime_error("Package registry exceeds size limit");
        }
        auto target = fs::absolute(filename).lexically_normal();
        fs::create_directories(target.parent_path());
        auto temporary = target;
        temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(std::random_device{}());
        try
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.close();
            if (!stream)
            {
                throw std::runtime_error("Cannot write package registry temporary file");
            }
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                throw std::runtime_error("Cannot replace package registry (Windows error " + std::to_string(GetLastError()) + ")");
            }
#else
            fs::rename(temporary, target);
#endif
        }
        catch (...)
        {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw;
        }
    }

    json inspectContentPackage(const fs::path& source)
    {
        const auto input = fs::canonical(source);
        json report{{"schemaVersion", 1}, {"source", pathText(input)}, {"complete", true},
            {"candidates", json::array()}, {"containers", json::array()}, {"warnings", json::array()}};
        auto addContainer = [&](const fs::path& file) {
            report["containers"].push_back({{"path", pathText(file)}, {"format", toUpper(file.extension().string()).substr(1)}, {"state", "extraction-required"}});
            report["complete"] = false;
        };
        if (fs::is_regular_file(input))
        {
            if (container(input))
            {
                addContainer(input);
            }
            else if (taArchive(input))
            {
                auto candidate = describeRoot(input.parent_path(), {}, {input});
                if (candidate["recognition"] == "folder-name")
                {
                    candidate["name"] = pathText(input.stem());
                    candidate["recognition"] = "archive-name";
                }
                report["candidates"].push_back(std::move(candidate));
            }
            else if (oneOf(toUpper(input.extension().string()), {".OTA", ".TNT"}))
            {
                std::vector<fs::path> pair;
                for (const auto& file : fs::directory_iterator(input.parent_path()))
                {
                    if (file.is_regular_file() && toUpper(file.path().stem().string()) == toUpper(input.stem().string())
                        && oneOf(toUpper(file.path().extension().string()), {".OTA", ".TNT"}))
                    {
                        pair.push_back(file.path());
                    }
                }
                auto candidate = describeRoot(input.parent_path(), pair, {});
                candidate["name"] = pathText(input.stem());
                report["candidates"].push_back(std::move(candidate));
            }
            else
            {
                throw std::runtime_error("Unsupported package source");
            }
        }
        else if (fs::is_directory(input))
        {
            std::map<fs::path, std::vector<fs::path>> loose;
            std::map<fs::path, std::vector<fs::path>> archives;
            std::set<fs::path> roots;
            std::size_t visited = 0;
            std::error_code error;
            for (fs::recursive_directory_iterator it(input, fs::directory_options::none, error), end;
                 it != end && !error; it.increment(error))
            {
                if (++visited > MaxScanEntries || roots.size() > MaxRoots)
                {
                    report["warnings"].push_back("Package scan limit reached");
                    report["complete"] = false;
                    break;
                }
                if (it->is_symlink())
                {
                    it.disable_recursion_pending();
                    report["warnings"].push_back("Skipped link: " + pathText(it->path()));
                    report["complete"] = false;
                    continue;
                }
                if (it->is_directory())
                {
                    if (it.depth() + 1 >= MaxScanDepth)
                    {
                        it.disable_recursion_pending();
                        report["warnings"].push_back("Package depth limit reached: " + pathText(it->path()));
                        report["complete"] = false;
                    }
                    continue;
                }
                if (!it->is_regular_file())
                {
                    continue;
                }
                const auto file = it->path();
                if (container(file))
                {
                    addContainer(file);
                    continue;
                }
                if (taArchive(file))
                {
                    roots.insert(file.parent_path());
                    archives[file.parent_path()].push_back(file);
                    continue;
                }
                auto root = file.parent_path();
                for (auto parent = root; parent != input && parent != parent.parent_path(); parent = parent.parent_path())
                {
                    if (resourceDirectory(parent.filename().string()))
                    {
                        root = parent.parent_path();
                        break;
                    }
                }
                loose[root].push_back(file);
                if (root != file.parent_path() || toUpper(file.filename().string()) == "RWE_MOD.JSON"
                    || oneOf(toUpper(file.extension().string()), {".OTA", ".TNT"}))
                {
                    roots.insert(root);
                }
                else if (toUpper(file.extension().string()) == ".TXT"
                    && !documentationFile(file))
                {
                    try
                    {
                        if (isAiText(readText(file, MaxMetadataBytes)))
                        {
                            roots.insert(root);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        report["warnings"].push_back(e.what());
                        report["complete"] = false;
                    }
                }
            }
            if (error)
            {
                report["warnings"].push_back("Package scan failed: " + error.message());
                report["complete"] = false;
            }
            // Companion folders (music/icons/etc.) belong to the closest
            // discovered data root, not to invented standalone packages.
            for (const auto& [bucket, files] : loose)
            {
                if (roots.count(bucket) != 0)
                {
                    continue;
                }
                for (auto parent = bucket.parent_path(); parent != parent.parent_path(); parent = parent.parent_path())
                {
                    if (roots.count(parent) != 0)
                    {
                        loose[parent].insert(loose[parent].end(), files.begin(), files.end());
                        break;
                    }
                    if (parent == input)
                    {
                        break;
                    }
                }
            }
            std::size_t described = 0;
            for (const auto& root : roots)
            {
                if (described++ >= MaxRoots)
                {
                    report["complete"] = false;
                    break;
                }
                report["candidates"].push_back(describeRoot(root, loose[root], archives[root]));
            }
        }
        else
        {
            throw std::runtime_error("Package source is not a file or directory");
        }
        for (const auto& candidate : report["candidates"])
        {
            if (!candidate["warnings"].empty())
            {
                report["complete"] = false;
            }
        }
        if (report["candidates"].empty() && report["containers"].empty())
        {
            report["warnings"].push_back("No supported TA package data found");
        }
        return report;
    }
}
