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
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <rwe/setup/TaInstall.h>
#include <rwe/util.h>

#ifdef RWE_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
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

#ifdef RWE_PLATFORM_WINDOWS
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
            for (const auto& prefix : {".wine/drive_c", ".local/share/Steam/steamapps/compatdata"})
            {
                candidates.emplace_back(h / prefix / "GOG Games" / "Total Annihilation");
                candidates.emplace_back(h / prefix / "Program Files (x86)" / "Total Annihilation");
            }
            candidates.emplace_back(h / "Total Annihilation");
            candidates.emplace_back(h / "Games" / "Total Annihilation");
        }
#endif

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
            << "Usage: rwe_setup [options]\n"
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
}

int main(int argc, char* argv[])
{
    std::vector<std::string> args(argv + 1, argv + argc);

    if (argFlag(args, "--help") || argFlag(args, "-h"))
    {
        printUsage();
        return 0;
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
                << "  rwe_setup --from \"C:/GOG Games/Total Annihilation\"\n\n"
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
