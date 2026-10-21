#include "TaInstall.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>

namespace fs = std::filesystem;

namespace rwe
{
    namespace
    {
        // Steam's text KeyValues files use quoted strings, braces, // comments
        // and escaped backslashes. Only scalar pairs are needed here; keeping
        // their depth distinguishes legacy library entries from app-size keys.
        struct SteamField
        {
            std::string key;
            std::string value;
            int depth;
        };

        std::vector<SteamField> readSteamFields(const fs::path& file)
        {
            std::ifstream input(file);
            std::vector<SteamField> fields;
            std::optional<std::string> key;
            int depth = 0;
            while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
            {
                const auto next = input.peek();
                if (next == '"')
                {
                    std::string value;
                    if (!(input >> std::quoted(value)))
                    {
                        break;
                    }
                    if (key)
                    {
                        fields.push_back({*key, std::move(value), depth});
                        key.reset();
                    }
                    else
                    {
                        key = std::move(value);
                    }
                }
                else if (next == '{' || next == '}')
                {
                    input.get();
                    depth += next == '{' ? 1 : -1;
                    key.reset();
                    if (depth < 0)
                    {
                        break;
                    }
                }
                else if (next == '/')
                {
                    input.get();
                    if (input.get() != '/')
                    {
                        break;
                    }
                    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                }
                else
                {
                    break;
                }
            }
            return fields;
        }

        std::string lowerCase(const std::string& s)
        {
            auto out = s;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        std::string extensionOf(const std::string& fileName)
        {
            auto dot = fileName.find_last_of('.');
            if (dot == std::string::npos)
            {
                return "";
            }
            return lowerCase(fileName.substr(dot));
        }
    }

    std::vector<fs::path> steamTaInstallPaths(const std::vector<fs::path>& steamRoots)
    {
        std::vector<fs::path> libraries;
        const auto appendUnique = [](std::vector<fs::path>& paths, const fs::path& path) {
            if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end())
            {
                paths.push_back(path);
            }
        };
        for (const auto& root : steamRoots)
        {
            appendUnique(libraries, root);
            for (const auto& file : {root / "steamapps/libraryfolders.vdf", root / "config/libraryfolders.vdf"})
            {
                for (const auto& field : readSteamFields(file))
                {
                    const bool legacy = field.depth == 1 && !field.key.empty()
                        && std::all_of(field.key.begin(), field.key.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
                    if ((field.depth == 2 && field.key == "path") || legacy)
                    {
                        appendUnique(libraries, fs::path(field.value));
                    }
                }
            }
        }
        std::vector<fs::path> candidates;
        for (const auto& library : libraries)
        {
            const auto common = library / "steamapps/common";
            // Steam app 298030: use its manifest when an install folder differs
            // from the normal name, and still search defaults without a manifest.
            for (const auto& field : readSteamFields(library / "steamapps/appmanifest_298030.acf"))
            {
                const fs::path directory(field.value);
                if (field.depth == 1 && field.key == "installdir" && !directory.empty()
                    && !directory.is_absolute() && !directory.has_root_name()
                    && std::find(directory.begin(), directory.end(), fs::path("..")) == directory.end())
                {
                    appendUnique(candidates, common / directory);
                }
            }
            appendUnique(candidates, common / "Total Annihilation");
            appendUnique(candidates, common / "Total Annihilation - Commander Pack");
        }
        return candidates;
    }

    const std::vector<std::string>& taArchiveExtensions()
    {
        // The same five `addToVfs` mounts, and the test holds them to it.
        static const std::vector<std::string> extensions{".hpi", ".ufo", ".ccx", ".gpf", ".gp3"};
        return extensions;
    }

    const std::vector<std::string>& requiredArchives()
    {
        static const std::vector<std::string> required{"totala1.hpi", "totala2.hpi"};
        return required;
    }

    const std::vector<std::string>& recommendedArchives()
    {
        // rev31 is the v3.1 data the behavioural work is written against;
        // the two ccx files are Core Contingency.
        static const std::vector<std::string> recommended{"rev31.gp3", "ccdata.ccx", "ccmaps.ccx"};
        return recommended;
    }

    TaFileKind classifyTaFile(const std::string& subdirectory, const std::string& fileName)
    {
        auto extension = extensionOf(fileName);
        auto directory = lowerCase(subdirectory);

        if (directory == "music" && extension == ".mp3")
        {
            return TaFileKind::Music;
        }

        // The GOG release keeps its films in `Data`, which is also where a
        // stray archive can sit, so the extension decides rather than the
        // directory.
        if (extension == ".zrb" || extension == ".smk")
        {
            return TaFileKind::Movie;
        }

        // Archives are only taken from the installation root. A `.ufo` inside
        // a mod's own subdirectory is that mod's business, and flattening
        // several of them into one directory would have them collide.
        if (directory.empty())
        {
            const auto& extensions = taArchiveExtensions();
            if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end())
            {
                return TaFileKind::Archive;
            }
        }

        return TaFileKind::Ignored;
    }

    fs::path taFileDestination(TaFileKind kind, const std::string& fileName)
    {
        switch (kind)
        {
            case TaFileKind::Archive:
                return fs::path(fileName);
            case TaFileKind::Movie:
                return fs::path("movies") / fileName;
            case TaFileKind::Music:
                return fs::path("music") / fileName;
            case TaFileKind::Ignored:
                return fs::path();
        }
        return fs::path();
    }

    bool looksLikeTaInstall(const TaInstallListing& listing)
    {
        for (const auto& required : requiredArchives())
        {
            auto found = std::any_of(
                listing.files.begin(),
                listing.files.end(),
                [&](const auto& entry) {
                    const auto& [subdirectory, fileName, size] = entry;
                    return subdirectory.empty() && lowerCase(fileName) == required;
                });

            if (!found)
            {
                return false;
            }
        }

        return true;
    }

    bool anyPathHasGameData(const std::vector<fs::path>& paths)
    {
        std::error_code ec;

        for (const auto& path : paths)
        {
            if (!fs::is_directory(path, ec))
            {
                ec.clear();
                continue;
            }

            for (fs::directory_iterator it(path, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    break;
                }
                if (!it->is_regular_file(ec))
                {
                    continue;
                }
                if (classifyTaFile("", it->path().filename().string()) == TaFileKind::Archive)
                {
                    return true;
                }
            }
            ec.clear();
        }

        return false;
    }

    TaSetupPlan planTaSetup(
        const TaInstallListing& source,
        const fs::path& dataDirectory,
        const std::vector<std::tuple<std::string, std::string, std::uintmax_t>>& existing)
    {
        TaSetupPlan plan;

        auto existingSize = [&](const std::string& subdirectory, const std::string& fileName) -> std::optional<std::uintmax_t> {
            for (const auto& [existingDirectory, existingName, existingBytes] : existing)
            {
                if (lowerCase(existingDirectory) == lowerCase(subdirectory)
                    && lowerCase(existingName) == lowerCase(fileName))
                {
                    return existingBytes;
                }
            }
            return std::nullopt;
        };

        std::vector<std::string> presentArchives;

        for (const auto& [subdirectory, fileName, size] : source.files)
        {
            auto kind = classifyTaFile(subdirectory, fileName);
            if (kind == TaFileKind::Ignored)
            {
                continue;
            }

            if (kind == TaFileKind::Archive)
            {
                presentArchives.push_back(lowerCase(fileName));
            }

            auto relative = taFileDestination(kind, fileName);

            TaCopyItem item;
            item.source = source.root / (subdirectory.empty() ? fs::path(fileName) : fs::path(subdirectory) / fileName);
            item.destination = dataDirectory / relative;
            item.kind = kind;
            item.size = size;

            auto destinationDirectory = relative.has_parent_path() ? relative.parent_path().string() : std::string();
            auto already = existingSize(destinationDirectory, fileName);
            item.alreadyPresent = already.has_value() && *already == size;

            if (item.alreadyPresent)
            {
                ++plan.alreadyPresentCount;
            }
            else
            {
                plan.bytesToCopy += size;
            }

            plan.items.push_back(std::move(item));
        }

        auto missing = [&](const std::vector<std::string>& wanted, std::vector<std::string>& out) {
            for (const auto& name : wanted)
            {
                if (std::find(presentArchives.begin(), presentArchives.end(), name) == presentArchives.end())
                {
                    out.push_back(name);
                }
            }
        };

        missing(requiredArchives(), plan.missingRequired);
        missing(recommendedArchives(), plan.missingRecommended);

        return plan;
    }
}
