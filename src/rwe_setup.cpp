/**
 * rwe_setup -- point it at a Total Annihilation installation, or at nothing,
 * and it fills in RWE's data directory.
 *
 * The manual version of this is "create %AppData%/RWE/Data and copy your TA
 * data files to it", which is fine until you discover that the films live in
 * a subdirectory the engine expects under a different name, and that the
 * soundtrack does too. This does the whole thing, finds the installation by
 * itself where it can, and says what it did.
 *
 * It shares `getSearchPath()` with the engine, so the two cannot disagree
 * about where the data belongs.
 */

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/mod/ModTransform.h>
#include <rwe/setup/TaInstall.h>
#include <rwe/setup/PackageLibrary.h>
#include <rwe/setup/ModManifest.h>
#include <rwe/util.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef RWE_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
#ifdef RWE_PLATFORM_WINDOWS
    constexpr const char* setupCommand = "rwe_setup";
    constexpr const char* exampleInstallPath = "C:/GOG Games/Total Annihilation";
#else
    constexpr const char* setupCommand = "rwe-setup";
    constexpr const char* exampleInstallPath = "/path/to/Total Annihilation";
#endif

    std::string humanBytes(std::uintmax_t bytes)
    {
        const char* units[] = {"B", "KB", "MB", "GB"};
        auto value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 3)
        {
            value /= 1024.0;
            ++unit;
        }

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), value < 10.0 && unit > 0 ? "%.1f %s" : "%.0f %s", value, units[unit]);
        return buffer;
    }

    /** Reads a directory one level deep, plus the two subdirectories we want. */
    rwe::TaInstallListing readInstallListing(const fs::path& root)
    {
        rwe::TaInstallListing listing;
        listing.root = root;

        std::error_code ec;

        auto readDirectory = [&](const fs::path& directory, const std::string& label) {
            for (fs::directory_iterator it(directory, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    break;
                }
                if (!it->is_regular_file(ec))
                {
                    continue;
                }
                auto size = it->file_size(ec);
                if (ec)
                {
                    ec.clear();
                    continue;
                }
                listing.files.emplace_back(label, it->path().filename().string(), size);
            }
            ec.clear();
        };

        if (!fs::is_directory(root, ec))
        {
            return listing;
        }

        readDirectory(root, "");

        // The films and the soundtrack are the two places the engine wants
        // things gathered from, and the only two we descend into.
        for (const auto& name : {"Data", "music"})
        {
            auto sub = root / name;
            if (fs::is_directory(sub, ec))
            {
                readDirectory(sub, name);
            }
            ec.clear();
        }

        return listing;
    }

    std::vector<std::tuple<std::string, std::string, std::uintmax_t>> readDestinationListing(const fs::path& dataDirectory)
    {
        std::vector<std::tuple<std::string, std::string, std::uintmax_t>> out;
        std::error_code ec;

        auto readDirectory = [&](const fs::path& directory, const std::string& label) {
            for (fs::directory_iterator it(directory, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    break;
                }
                if (!it->is_regular_file(ec))
                {
                    continue;
                }
                auto size = it->file_size(ec);
                if (ec)
                {
                    ec.clear();
                    continue;
                }
                out.emplace_back(label, it->path().filename().string(), size);
            }
            ec.clear();
        };

        if (!fs::is_directory(dataDirectory, ec))
        {
            return out;
        }

        readDirectory(dataDirectory, "");
        for (const auto& name : {"movies", "music"})
        {
            auto sub = dataDirectory / name;
            if (fs::is_directory(sub, ec))
            {
                readDirectory(sub, name);
            }
            ec.clear();
        }

        return out;
    }

#ifdef RWE_PLATFORM_WINDOWS
    std::optional<std::string> readRegistryString(HKEY root, const std::string& subKey, const std::string& valueName)
    {
        HKEY key;
        // Ask for the 32-bit view as well: the installers are 32-bit, so on a
        // 64-bit machine their keys live under WOW6432Node.
        for (auto access : {KEY_READ | KEY_WOW64_32KEY, KEY_READ | KEY_WOW64_64KEY})
        {
            if (RegOpenKeyExA(root, subKey.c_str(), 0, access, &key) != ERROR_SUCCESS)
            {
                continue;
            }

            char buffer[MAX_PATH];
            DWORD size = sizeof(buffer);
            DWORD type = 0;
            auto result = RegQueryValueExA(key, valueName.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
            RegCloseKey(key);

            if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && size > 0)
            {
                return std::string(buffer, size - 1);
            }
        }

        return std::nullopt;
    }
#endif

    /** Places a TA installation is likely to be, best guesses first. */
    std::vector<fs::path> candidateInstallPaths()
    {
        std::vector<fs::path> candidates;
        std::vector<fs::path> steamRoots;

#ifdef RWE_PLATFORM_WINDOWS
        if (auto path = readRegistryString(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam", "SteamPath"))
        {
            steamRoots.emplace_back(*path);
        }
        if (auto path = readRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath"))
        {
            steamRoots.emplace_back(*path);
        }
        for (const auto* variable : {"ProgramFiles(x86)", "ProgramFiles"})
        {
            if (auto path = std::getenv(variable))
            {
                steamRoots.emplace_back(fs::path(path) / "Steam");
            }
        }
        steamRoots.emplace_back("C:/Program Files (x86)/Steam");
        steamRoots.emplace_back("C:/Program Files/Steam");
        // GOG records the install directory against the game's product id.
        // 1207658880 is the Total Annihilation: Commander Pack.
        for (const auto& id : {"1207658880", "1207658879"})
        {
            if (auto path = readRegistryString(HKEY_LOCAL_MACHINE, std::string("SOFTWARE\\GOG.com\\Games\\") + id, "path"))
            {
                candidates.emplace_back(*path);
            }
        }

        // Cavedog's own installer, and the CD version.
        if (auto path = readRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Cavedog Entertainment\\Total Annihilation\\1.0", "Directory"))
        {
            candidates.emplace_back(*path);
        }

        for (const auto& root : {"C:/Program Files (x86)", "C:/Program Files", "C:/GOG Games", "D:/GOG Games", "D:/Games"})
        {
            candidates.emplace_back(fs::path(root) / "Total Annihilation");
            candidates.emplace_back(fs::path(root) / "Total Annihilation" / "Total Annihilation");
        }
#else
        // A Wine or Proton prefix is where this usually is away from Windows.
        if (auto home = std::getenv("HOME"))
        {
            fs::path h(home);
            for (const auto& prefix : {".local/share/Steam", ".steam/steam", ".steam/root", ".steam/debian-installation",
                     ".var/app/com.valvesoftware.Steam/.local/share/Steam"})
            {
                steamRoots.emplace_back(h / prefix);
            }
            candidates.emplace_back(h / ".wine/drive_c/GOG Games/Total Annihilation");
            candidates.emplace_back(h / ".wine/drive_c/Program Files (x86)/Total Annihilation");
            candidates.emplace_back(h / "Total Annihilation");
            candidates.emplace_back(h / "Games" / "Total Annihilation");
        }
#endif
        if (auto dataHome = std::getenv("XDG_DATA_HOME"))
        {
            steamRoots.emplace_back(fs::path(dataHome) / "Steam");
        }
        const auto steamCandidates = rwe::steamTaInstallPaths(steamRoots);
        candidates.insert(candidates.end(), steamCandidates.begin(), steamCandidates.end());
        return candidates;
    }

    /**
     * A GOG install nests the game one directory down from the one the
     * registry records, so any candidate is tried both as itself and as its
     * own similarly-named child.
     */
    std::optional<fs::path> findInstall(const std::vector<fs::path>& candidates)
    {
        std::error_code ec;
        for (const auto& candidate : candidates)
        {
            if (!fs::is_directory(candidate, ec))
            {
                ec.clear();
                continue;
            }

            if (rwe::looksLikeTaInstall(readInstallListing(candidate)))
            {
                return candidate;
            }

            for (fs::directory_iterator it(candidate, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    break;
                }
                if (!it->is_directory(ec))
                {
                    continue;
                }
                if (rwe::looksLikeTaInstall(readInstallListing(it->path())))
                {
                    return it->path();
                }
            }
            ec.clear();
        }

        return std::nullopt;
    }

    void printUsage()
    {
        std::cout
            << "Usage: " << setupCommand << " [options]\n"
            << "\n"
            << "Copies a Total Annihilation installation's data into RWE's data\n"
            << "directory: the archives, the films and the soundtrack, each where\n"
            << "the engine looks for it.\n"
            << "\n"
            << "  --from <path>    The TA installation. Found automatically if omitted.\n"
            << "  --to <path>      RWE's data directory. Defaults to the engine's own.\n"
            << "  --link           Hard link instead of copying, where the filesystem\n"
            << "                   allows it. Saves about a gigabyte; falls back to a\n"
            << "                   copy across volumes.\n"
            << "  --dry-run        Say what would happen and change nothing.\n"
            << "  --force          Copy files again even if they are already there.\n"
            << "  --check-existing Exit successfully if the data directory already\n"
            << "                   contains the base-game archives; do not import.\n"
            << "  --inspect-package <path>  Read-only mod/map inspection (JSON).\n"
            << "  --list-packages          Read the package registry (JSON).\n"
            << "  --library-root <path>    Package library directory for listing.\n"
            << "  --resolve-mods <manifest>...\n"
            << "                   Resolve author manifests into one ordered set\n"
            << "                   and print its canonical identity (JSON).\n"
            << "  --option <id>:<key>=<value>\n"
            << "                   Choose a package option for a mod command.\n"
            << "  --preview-mods <manifest>...\n"
            << "                   Apply the scripts to the real unit data and\n"
            << "                   print every change; nothing is written.\n"
            << "  --validate-mod <manifest>\n"
            << "                   Check one author manifest and print it.\n"
            << "  --create-mod <dir>\n"
            << "                   Write a starter manifest and script.\n"
            << "  --data-path <path>  Data directory for --preview-mods.\n"
            << "  --help           Show this message.\n";
    }

    std::optional<std::string> argValue(const std::vector<std::string>& args, const std::string& name)
    {
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] == name)
            {
                return args[i + 1];
            }
        }
        return std::nullopt;
    }

    bool argFlag(const std::vector<std::string>& args, const std::string& name)
    {
        return std::find(args.begin(), args.end(), name) != args.end();
    }

    // Reads one author manifest. The bound matches PACKAGE-003's external
    // metadata limit; a manifest is metadata, not content.
    std::string readManifestText(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Cannot open manifest " + path.string());
        }
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (text.size() > 1024 * 1024)
        {
            throw std::runtime_error("Manifest is larger than 1 MiB: " + path.string());
        }
        return text;
    }

    // The features this build's authoring foundation provides. A manifest
    // that requires anything else is refused rather than half-loaded.
    const std::vector<std::string>& supportedModFeatures()
    {
        static const std::vector<std::string> features = {"transform.definitions@1", "runtime.damage@1"};
        return features;
    }

    // Manifest paths after `flag`, up to the next option. Indices rather than
    // strings so the native (Unicode) arguments are used on Windows.
    std::vector<std::size_t> manifestPositions(const std::vector<std::string>& args, const std::string& flag)
    {
        std::vector<std::size_t> positions;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] != flag)
            {
                continue;
            }
            for (std::size_t j = i + 1; j < args.size(); ++j)
            {
                if (args[j].compare(0, 2, "--") == 0)
                {
                    break;
                }
                positions.push_back(j);
            }
            break;
        }
        return positions;
    }

    rwe::ModOptionSelections readOptionSelections(const std::vector<std::string>& args)
    {
        rwe::ModOptionSelections selections;
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] != "--option")
            {
                continue;
            }
            const auto& text = args[i + 1];
            const auto colon = text.find(':');
            const auto equals = text.find('=');
            if (colon == std::string::npos || equals == std::string::npos || colon == 0 || colon > equals
                || equals + 1 >= text.size())
            {
                throw std::runtime_error("--option must be <packageId>:<key>=<value>: " + text);
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
            selections[id][key] = value;
        }
        return selections;
    }

    rwe::ModPackage readModPackage(const fs::path& path)
    {
        const auto text = readManifestText(path);
        nlohmann::json manifestValue;
        try
        {
            manifestValue = nlohmann::json::parse(text);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(path.string() + ": invalid JSON: " + e.what());
        }
        std::string implicitId;
        if (!manifestValue.is_object() || !manifestValue.contains("manifestVersion"))
        {
            implicitId = "legacy." + rwe::sha256Hex(text).substr(0, 16);
        }
        auto package = rwe::modPackageFromJson(manifestValue, text, path.string(), implicitId);
        if (!package)
        {
            throw std::runtime_error(package.getErr().describe());
        }
        return std::move(package.get());
    }

    std::vector<rwe::ModPackage> readModPackages(
        const std::vector<std::string>& args,
        const std::vector<fs::path>& nativeArguments,
        const std::string& flag)
    {
        const auto positions = manifestPositions(args, flag);
        if (positions.empty())
        {
            throw std::runtime_error(flag + " needs at least one manifest path");
        }
        std::vector<rwe::ModPackage> packages;
        for (const auto position : positions)
        {
            packages.push_back(readModPackage(nativeArguments.at(position)));
        }
        return packages;
    }

    rwe::ResolvedModSet resolvePackages(
        const std::vector<rwe::ModPackage>& packages,
        const rwe::ModOptionSelections& selections)
    {
        rwe::ModResolutionEnvironment environment;
        environment.supportedFeatures = supportedModFeatures();
        auto set = rwe::resolveModSet(packages, selections, environment);
        if (!set)
        {
            throw std::runtime_error(set.getErr().describe());
        }
        return std::move(set.get());
    }

    // Turns a resolved set into the launch form: ordered packages with the
    // script path each manifest's entry resolves to.
    rwe::ModLaunchContent launchContentFrom(
        const rwe::ResolvedModSet& set,
        const std::vector<rwe::ModPackage>& packages,
        const std::vector<fs::path>& manifestPaths)
    {
        rwe::ModLaunchContent content;
        content.identity = set.identity;
        for (const auto& mod : set.mods)
        {
            rwe::ModLaunchPackage launch;
            launch.id = mod.manifest.id;
            launch.version = mod.manifest.versionText;
            launch.manifestSha256 = mod.manifestSha256;
            launch.kind = rwe::modKindText(mod.manifest.kind);
            launch.features = mod.manifest.features;
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
}

int main(int argc, char* argv[])
{
    std::vector<std::string> args(argv + 1, argv + argc);

    if (argFlag(args, "--help") || argFlag(args, "-h"))
    {
        printUsage();
        return 0;
    }

    // PACKAGE-003: package commands never fall through into base-data import.
    const auto inspectPackage = argFlag(args, "--inspect-package");
    const auto listPackages = argFlag(args, "--list-packages");
    const auto resolveMods = argFlag(args, "--resolve-mods");
    const auto previewMods = argFlag(args, "--preview-mods");
    const auto validateMod = argFlag(args, "--validate-mod");
    const auto createMod = argFlag(args, "--create-mod");
    const auto pendingPackageCommand = argFlag(args, "--install-package") || argFlag(args, "--verify-package") || argFlag(args, "--uninstall-package");
    if (inspectPackage || listPackages || resolveMods || previewMods || validateMod || createMod
        || pendingPackageCommand || argFlag(args, "--library-root"))
    {
        try
        {
            if (pendingPackageCommand)
            {
                throw std::runtime_error("Package install, verification and removal are not implemented yet");
            }
            if (!inspectPackage && !listPackages && !resolveMods && !previewMods && !validateMod && !createMod)
            {
                throw std::runtime_error("--library-root requires --list-packages");
            }
            if (inspectPackage + listPackages + resolveMods + previewMods + validateMod + createMod > 1
                || argFlag(args, "--from") || argFlag(args, "--to"))
            {
                throw std::runtime_error("Choose one package command without base-data import options");
            }
            std::vector<fs::path> nativeArguments;
#ifdef _WIN32
            // PACKAGE-004: preserve Unicode paths rather than decoding narrow argv.
            int nativeCount = 0;
            auto releaseArguments = [](LPWSTR* pointer) { LocalFree(pointer); };
            std::unique_ptr<LPWSTR, decltype(releaseArguments)> native(
                CommandLineToArgvW(GetCommandLineW(), &nativeCount), releaseArguments);
            if (!native || nativeCount != argc)
            {
                throw std::runtime_error("Cannot parse native package arguments");
            }
            for (int i = 1; i < nativeCount; ++i)
            {
                nativeArguments.emplace_back(native.get()[i]);
            }
#else
            for (int i = 1; i < argc; ++i)
            {
                nativeArguments.emplace_back(argv[i]);
            }
#endif
            const auto value = [&](const std::string& key) -> fs::path {
                const auto found = std::find(args.begin(), args.end(), key);
                if (found == args.end() || std::next(found) == args.end())
                {
                    throw std::runtime_error("Missing value for " + key);
                }
                const auto index = static_cast<std::size_t>(std::distance(args.begin(), found)) + 1;
                const auto& argument = nativeArguments.at(index);
                if (argument.empty() || argument.native().compare(0, 2, fs::path("--").native()) == 0)
                {
                    throw std::runtime_error("Missing value for " + key);
                }
                return argument;
            };
            nlohmann::json result;
            if (inspectPackage)
            {
                result = rwe::inspectContentPackage(fs::path(value("--inspect-package")));
            }
            else if (resolveMods)
            {
                std::vector<std::size_t> manifestIndices;
                for (std::size_t i = 0; i < args.size(); ++i)
                {
                    if (args[i] != "--resolve-mods")
                    {
                        continue;
                    }
                    for (std::size_t j = i + 1; j < args.size(); ++j)
                    {
                        if (args[j].compare(0, 2, "--") == 0)
                        {
                            break;
                        }
                        manifestIndices.push_back(j);
                    }
                }
                if (manifestIndices.empty())
                {
                    throw std::runtime_error("--resolve-mods needs at least one manifest path");
                }

                std::vector<rwe::ModPackage> packages;
                for (const auto index : manifestIndices)
                {
                    const auto path = nativeArguments.at(index);
                    const auto text = readManifestText(path);
                    nlohmann::json manifestValue;
                    try
                    {
                        manifestValue = nlohmann::json::parse(text);
                    }
                    catch (const std::exception& e)
                    {
                        throw std::runtime_error(path.string() + ": invalid JSON: " + e.what());
                    }
                    // MOD-SDK-002: a legacy descriptor has no stable id, so
                    // derive one from its bytes until MOD-03 hashes the
                    // package's real content.
                    std::string implicitId;
                    if (!manifestValue.is_object() || !manifestValue.contains("manifestVersion"))
                    {
                        implicitId = "legacy." + rwe::sha256Hex(text).substr(0, 16);
                    }
                    auto package = rwe::modPackageFromJson(manifestValue, text, path.string(), implicitId);
                    if (!package)
                    {
                        throw std::runtime_error(package.getErr().describe());
                    }
                    packages.push_back(std::move(package.get()));
                }

                rwe::ModOptionSelections selections;
                for (std::size_t i = 0; i + 1 < args.size(); ++i)
                {
                    if (args[i] != "--option")
                    {
                        continue;
                    }
                    const auto& text = args[i + 1];
                    const auto colon = text.find(':');
                    const auto equals = text.find('=');
                    if (colon == std::string::npos || equals == std::string::npos || colon == 0
                        || colon > equals || equals + 1 >= text.size())
                    {
                        throw std::runtime_error("--option must be <packageId>:<key>=<value>: " + text);
                    }
                    const auto id = text.substr(0, colon);
                    const auto key = text.substr(colon + 1, equals - colon - 1);
                    const auto raw = text.substr(equals + 1);
                    nlohmann::json optionValue;
                    try
                    {
                        optionValue = nlohmann::json::parse(raw);
                    }
                    catch (const std::exception&)
                    {
                        optionValue = raw;
                    }
                    selections[id][key] = optionValue;
                }

                rwe::ModResolutionEnvironment environment;
                environment.supportedFeatures = supportedModFeatures();
                auto set = rwe::resolveModSet(std::move(packages), selections, environment);
                if (!set)
                {
                    throw std::runtime_error(set.getErr().describe());
                }
                auto output = rwe::resolvedModSetJson(set.get());
                output["identity"] = set.get().identity;
                std::cout << output.dump(2) << '\n';
                return 0;
            }
            else if (previewMods)
            {
                const auto packages = readModPackages(args, nativeArguments, "--preview-mods");
                const auto positions = manifestPositions(args, "--preview-mods");
                std::vector<fs::path> manifestPaths;
                manifestPaths.reserve(positions.size());
                for (const auto position : positions)
                {
                    manifestPaths.push_back(nativeArguments.at(position));
                }
                const auto set = resolvePackages(packages, readOptionSelections(args));
                const auto content = launchContentFrom(set, packages, manifestPaths);

                fs::path dataPath;
                if (auto supplied = argValue(args, "--data-path"))
                {
                    dataPath = *supplied;
                }
                else if (auto local = rwe::getSearchPath())
                {
                    dataPath = *local;
                }
                else
                {
                    throw std::runtime_error("Cannot locate the game data; pass --data-path");
                }

                auto vfs = rwe::constructVfs(dataPath);
                auto fbiNames = vfs.getFileNames("units", ".fbi");
                if (fbiNames.empty())
                {
                    throw std::runtime_error("No unit data found under " + dataPath.string());
                }
                std::vector<rwe::ModUnitRecord> records;
                records.reserve(fbiNames.size());
                for (const auto& fbiName : fbiNames)
                {
                    auto bytes = vfs.readFile("units/" + fbiName);
                    if (!bytes)
                    {
                        throw std::runtime_error("Unit file could not be read: " + fbiName);
                    }
                    const auto fbiTdf = rwe::parseTdfFromString(std::string(bytes->data(), bytes->size()));
                    const auto fbi = rwe::parseUnitFbi(fbiTdf);
                    records.push_back({fbi.unitName,
                        {{"BuildTime", static_cast<double>(fbi.buildTime)},
                            {"EnergyCost", static_cast<double>(fbi.buildCostEnergy)},
                            {"MetalCost", static_cast<double>(fbi.buildCostMetal)}}});
                }

                nlohmann::json report;
                report["identity"] = set.identity;
                report["units"] = records.size();
                auto packageReports = nlohmann::json::array();
                for (const auto& package : content.packages)
                {
                    std::ifstream scriptFile(package.scriptPath, std::ios::binary);
                    if (!scriptFile)
                    {
                        throw std::runtime_error("Mod " + package.id + ": cannot open " + package.scriptPath);
                    }
                    std::string source(
                        (std::istreambuf_iterator<char>(scriptFile)), std::istreambuf_iterator<char>());
                    rwe::ModTransformScript script{package.id, package.scriptName, source, package.options};
                    const auto transformed = rwe::runUnitTransform(script, records);
                    if (!transformed.ok)
                    {
                        throw std::runtime_error("Mod " + package.id + ": " + transformed.error);
                    }
                    nlohmann::json packageReport;
                    packageReport["id"] = package.id;
                    packageReport["instructions"] = transformed.instructionsUsed;
                    packageReport["log"] = transformed.log;
                    auto changes = nlohmann::json::array();
                    for (const auto& change : transformed.changes)
                    {
                        changes.push_back({
                            {"unit", change.unit},
                            {"key", change.key},
                            {"before", change.before},
                            {"after", change.after},
                        });
                    }
                    packageReport["changes"] = std::move(changes);
                    packageReports.push_back(std::move(packageReport));
                }
                report["packages"] = std::move(packageReports);
                std::cout << report.dump(2) << '\n';
                return 0;
            }
            else if (validateMod)
            {
                const auto package = readModPackage(fs::path(value("--validate-mod")));
                nlohmann::json output;
                output["id"] = package.manifest.id;
                output["name"] = package.manifest.name;
                output["version"] = package.manifest.versionText;
                output["kind"] = rwe::modKindText(package.manifest.kind);
                output["api"] = package.manifest.api;
                output["legacy"] = package.manifest.legacy;
                output["entry"] = package.manifest.entry ? nlohmann::json(*package.manifest.entry) : nlohmann::json(nullptr);
                output["manifestSha256"] = package.manifestSha256;
                output["requires"] = nlohmann::json::array();
                for (const auto& dependency : package.manifest.dependencies)
                {
                    output["requires"].push_back({{"id", dependency.id}, {"version", dependency.version}});
                }
                output["features"] = package.manifest.features;
                output["exclusive"] = package.manifest.exclusive;
                std::cout << output.dump(2) << '\n';
                return 0;
            }
            else if (createMod)
            {
                const auto directory = fs::path(value("--create-mod"));
                const auto manifestPath = directory / "rwe_mod.json";
                if (fs::exists(manifestPath))
                {
                    throw std::runtime_error("Refusing to overwrite " + manifestPath.string());
                }
                auto id = argValue(args, "--id").value_or(std::string());
                if (id.empty())
                {
                    std::string stem = directory.filename().string();
                    for (auto& character : stem)
                    {
                        const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                        const bool allowed = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')
                            || lower == '.' || lower == '_' || lower == '-';
                        character = allowed ? lower : '-';
                    }
                    id = "com.example." + (stem.empty() ? std::string("mod") : stem);
                }
                const auto name = argValue(args, "--name").value_or("New mod");
                const auto version = argValue(args, "--version").value_or("1.0.0");

                nlohmann::json manifest = {
                    {"manifestVersion", 1},
                    {"id", id},
                    {"name", name},
                    {"version", version},
                    {"kind", "mutator"},
                    {"api", 1},
                    {"entry", "scripts/main.lua"},
                    {"features", nlohmann::json::array({"transform.definitions@1"})},
                    {"options", nlohmann::json::array({
                         {{"key", "multiplier"}, {"type", "number"}, {"default", 1.5}, {"min", 0.1}, {"max", 10.0}},
                     })},
                };
                // Refuse to write a manifest the resolver would reject.
                const auto checked = rwe::parseModManifest(manifest, manifestPath.string());
                if (!checked)
                {
                    throw std::runtime_error(checked.getErr().describe());
                }

                fs::create_directories(directory / "scripts");
                std::ofstream manifestFile(manifestPath, std::ios::binary);
                manifestFile << manifest.dump(2) << '\n';
                if (!manifestFile)
                {
                    throw std::runtime_error("Cannot write " + manifestPath.string());
                }
                const auto scriptPath = directory / "scripts" / "main.lua";
                std::ofstream scriptFile(scriptPath, std::ios::binary);
                scriptFile
                    << "-- Cost mutator: edit this file, then run `rwe_setup --preview-mods`.\n"
                    << "-- rwe.unit_names() is sorted, so every peer sees the same order.\n"
                    << "local multiplier = rwe.options.multiplier\n"
                    << "for _, name in ipairs(rwe.unit_names()) do\n"
                    << "    local cost = rwe.unit_get(name, \"MetalCost\")\n"
                    << "    rwe.unit_set(name, \"MetalCost\", cost * multiplier)\n"
                    << "end\n"
                    << "rwe.log(\"scaled MetalCost by \" .. multiplier)\n";
                if (!scriptFile)
                {
                    throw std::runtime_error("Cannot write " + scriptPath.string());
                }
                std::cout << nlohmann::json({{"created", true},
                                       {"id", id},
                                       {"manifest", manifestPath.string()},
                                       {"script", scriptPath.string()}})
                                 .dump(2)
                          << '\n';
                return 0;
            }
            else
            {
                fs::path library;
                if (argFlag(args, "--library-root"))
                {
                    library = value("--library-root");
                }
                else if (const auto local = rwe::getLocalDataPath())
                {
                    library = rwe::packageLibraryPaths(*local).root;
                }
                else
                {
                    throw std::runtime_error("Cannot locate the package library; pass --library-root");
                }
                std::error_code statusError;
                const auto libraryStatus = fs::status(library, statusError);
                if (statusError && statusError != std::errc::no_such_file_or_directory)
                {
                    throw std::runtime_error(
                        "Cannot access package library root " + library.string() + ": " + statusError.message());
                }
                if (!statusError && fs::exists(libraryStatus) && !fs::is_directory(libraryStatus))
                {
                    throw std::runtime_error("Package library root is not a directory: " + library.string());
                }
                result = rwe::loadPackageRegistry(library / "registry.json");
            }
            std::cout << result.dump() << '\n';
            return 0;
        }
        catch (const std::exception& e)
        {
            std::cout << nlohmann::json({{"schemaVersion", 1}, {"error", {{"code", "package-command"}, {"message", e.what()}}}})
                             .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
            return 1;
        }
    }

    auto dryRun = argFlag(args, "--dry-run");
    auto useLinks = argFlag(args, "--link");
    auto force = argFlag(args, "--force");

    // Where it goes. The engine's own answer, so the two cannot disagree.
    fs::path dataDirectory;
    if (auto to = argValue(args, "--to"))
    {
        dataDirectory = *to;
    }
    else
    {
        auto searchPath = rwe::getSearchPath();
        if (!searchPath)
        {
            std::cerr << "Could not work out where RWE keeps its data. Pass --to <path>.\n";
            return 1;
        }
        dataDirectory = *searchPath;
    }

    // A completed import remains usable even after the original TA install is
    // moved or removed. Explicit imports and --force still run as requested.
    auto checkExisting = argFlag(args, "--check-existing");
    if ((checkExisting || (!argValue(args, "--from") && !force))
        && rwe::looksLikeTaInstall(readInstallListing(dataDirectory)))
    {
        std::cout << "Game data already present in " << dataDirectory.string()
                  << ". No import needed.\n";
        return 0;
    }
    if (checkExisting)
    {
        return 1;
    }

    // Where it comes from.
    std::optional<fs::path> installPath;
    if (auto from = argValue(args, "--from"))
    {
        installPath = fs::path(*from);
        if (!rwe::looksLikeTaInstall(readInstallListing(*installPath)))
        {
            std::cerr << "That does not look like a Total Annihilation installation:\n  "
                      << installPath->string() << "\n\n"
                      << "Expected to find " << rwe::requiredArchives()[0]
                      << " and " << rwe::requiredArchives()[1] << " in it.\n";
            return 1;
        }
    }
    else
    {
        std::cout << "Looking for Total Annihilation...\n";
        installPath = findInstall(candidateInstallPaths());
        if (!installPath)
        {
            std::cerr
                << "Could not find a Total Annihilation installation.\n\n"
                << "Point me at one:\n"
                << "  " << setupCommand << " --from \"" << exampleInstallPath << "\"\n\n"
                << "It is the directory containing " << rwe::requiredArchives()[0] << ".\n";
            return 1;
        }
    }

    std::cout << "Found:  " << installPath->string() << "\n"
              << "Target: " << dataDirectory.string() << "\n\n";

    auto listing = readInstallListing(*installPath);
    auto existing = force
        ? std::vector<std::tuple<std::string, std::string, std::uintmax_t>>()
        : readDestinationListing(dataDirectory);

    auto plan = rwe::planTaSetup(listing, dataDirectory, existing);

    if (!plan.missingRequired.empty())
    {
        std::cerr << "That installation is missing files the engine needs:\n";
        for (const auto& name : plan.missingRequired)
        {
            std::cerr << "  " << name << "\n";
        }
        return 1;
    }

    auto count = [&](rwe::TaFileKind kind) {
        return std::count_if(plan.items.begin(), plan.items.end(), [&](const auto& item) { return item.kind == kind; });
    };

    std::cout << count(rwe::TaFileKind::Archive) << " archives, "
              << count(rwe::TaFileKind::Movie) << " films, "
              << count(rwe::TaFileKind::Music) << " music tracks\n";

    if (plan.alreadyPresentCount > 0)
    {
        std::cout << plan.alreadyPresentCount << " already in place\n";
    }

    std::cout << humanBytes(plan.bytesToCopy) << " to " << (useLinks ? "link" : "copy") << "\n\n";

    if (!plan.missingRecommended.empty())
    {
        std::cout << "Note: not present in that installation:\n";
        for (const auto& name : plan.missingRecommended)
        {
            std::cout << "  " << name;
            if (name == "rev31.gp3")
            {
                std::cout << "  (the v3.1 data; the game runs without it, but this is what"
                          << " the engine's behaviour is matched against)";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    if (dryRun)
    {
        std::cout << "Dry run: nothing was changed.\n";
        return 0;
    }

    std::error_code ec;
    fs::create_directories(dataDirectory, ec);
    if (ec)
    {
        std::cerr << "Could not create " << dataDirectory.string() << ": " << ec.message() << "\n";
        return 1;
    }

    std::size_t copied = 0;
    std::size_t failed = 0;
    std::uintmax_t bytesDone = 0;

    for (const auto& item : plan.items)
    {
        if (item.alreadyPresent)
        {
            continue;
        }

        fs::create_directories(item.destination.parent_path(), ec);
        ec.clear();

        auto ok = false;
        if (useLinks)
        {
            fs::create_hard_link(item.source, item.destination, ec);
            ok = !ec;
            ec.clear();
        }

        if (!ok)
        {
            // Overwrite rather than skip: a file of the wrong size is a
            // half-finished copy from a previous run, not something to keep.
            ok = fs::copy_file(item.source, item.destination, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                std::cerr << "  failed: " << item.source.filename().string() << " -- " << ec.message() << "\n";
                ec.clear();
                ++failed;
                continue;
            }
        }

        ++copied;
        bytesDone += item.size;

        std::cout << "\r  " << copied << " files, " << humanBytes(bytesDone) << "   " << std::flush;
    }

    std::cout << "\r  " << copied << " files, " << humanBytes(bytesDone) << "   \n\n";

    if (failed > 0)
    {
        std::cerr << failed << " file(s) could not be copied.\n";
        return 1;
    }

    std::cout << "Done. Run rwe to play.\n";
    return 0;
}
