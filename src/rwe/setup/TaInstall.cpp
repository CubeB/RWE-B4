#include "TaInstall.h"

#include <algorithm>
#include <rwe/util/rwe_string.h>

namespace fs = std::filesystem;

namespace rwe
{
    namespace
    {
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
