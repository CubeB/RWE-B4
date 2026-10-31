#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <rwe/io/gaf/GafArchive.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/util/SpanStream.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

namespace
{
    using Clock = std::chrono::steady_clock;

    double milliseconds(Clock::duration duration)
    {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    std::string stripOtaSuffix(std::string name)
    {
        if (name.size() >= 4)
        {
            name.resize(name.size() - 4);
        }
        return name;
    }

    struct MapListTimings
    {
        double enumerationMs{0};
        double readsMs{0};
        double schemaParseMs{0};
        std::size_t candidateCount{0};
        std::size_t acceptedCount{0};
        double slowestReadMs{0};
        std::string slowestReadName;
    };

    std::vector<std::string> getMapNames(rwe::CompositeVirtualFileSystem& vfs, MapListTimings& timings)
    {
        const auto enumerationStart = Clock::now();
        auto names = vfs.getFileNames("maps", ".ota");
        timings.enumerationMs = milliseconds(Clock::now() - enumerationStart);
        timings.candidateCount = names.size();

        std::vector<std::string> accepted;
        accepted.reserve(names.size());
        for (const auto& filename : names)
        {
            const auto readStart = Clock::now();
            auto bytes = vfs.readFile(std::string("maps/").append(filename));
            const auto readMs = milliseconds(Clock::now() - readStart);
            timings.readsMs += readMs;
            if (readMs > timings.slowestReadMs)
            {
                timings.slowestReadMs = readMs;
                timings.slowestReadName = filename;
            }
            if (!bytes)
            {
                throw std::runtime_error("Failed to read OTA file: " + filename);
            }

            const auto parseStart = Clock::now();
            const bool hasNetworkSchema = rwe::parseTdfHasNetworkSchema(*bytes);
            timings.schemaParseMs += milliseconds(Clock::now() - parseStart);
            if (hasNetworkSchema)
            {
                accepted.push_back(stripOtaSuffix(filename));
            }
        }
        timings.acceptedCount = accepted.size();
        return accepted;
    }
}

int main(int argc, char** argv)
{
    std::string dataPath;
    std::string mapName = "Coast To Coast";
    unsigned int repeats = 10;
    std::vector<std::string> gafNames;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--data-path" && i + 1 < argc)
        {
            dataPath = argv[++i];
        }
        else if (arg == "--map" && i + 1 < argc)
        {
            mapName = argv[++i];
        }
        else if (arg == "--repeats" && i + 1 < argc)
        {
            repeats = static_cast<unsigned int>(std::stoul(argv[++i]));
        }
        else if (arg == "--gaf" && i + 1 < argc)
        {
            gafNames.emplace_back(argv[++i]);
        }
        else
        {
            std::cerr << "Usage: menu_lag_probe --data-path <path> [--map <name>] [--repeats <n>] [--gaf <name>]...\n";
            return 2;
        }
    }
    if (dataPath.empty() || repeats == 0)
    {
        std::cerr << "A data path and positive repeat count are required\n";
        return 2;
    }

    auto vfs = rwe::constructVfs(dataPath);

    // What a gadget's art lookup costs when the same screen GAF is asked for
    // one entry at a time: TextureService re-reads (and, for archive members,
    // re-decompresses) the whole file and rebuilds its entry table for each
    // lookup. The three figures are the parts of that repeated work.
    for (const auto& gafName : gafNames)
    {
        auto bytes = vfs.readFile(gafName);
        if (!bytes)
        {
            double missingMs = 0;
            for (unsigned int i = 0; i < repeats; ++i)
            {
                const auto start = Clock::now();
                auto again = vfs.readFile(gafName);
                missingMs += milliseconds(Clock::now() - start);
                if (again)
                {
                    missingMs = -1;
                    break;
                }
            }
            std::cout << "GAF name=" << gafName << " missing read_ms="
                      << (missingMs / static_cast<double>(repeats)) << '\n';
            continue;
        }

        double readMs = 0;
        for (unsigned int i = 0; i < repeats; ++i)
        {
            const auto start = Clock::now();
            auto again = vfs.readFile(gafName);
            readMs += milliseconds(Clock::now() - start);
            if (!again)
            {
                readMs = -1;
                break;
            }
        }

        double parseMs = 0;
        std::size_t entryCount = 0;
        std::vector<std::string> entryNames;
        for (unsigned int i = 0; i < repeats; ++i)
        {
            const auto start = Clock::now();
            rwe::SpanStream stream(bytes->data(), bytes->size());
            rwe::GafArchive archive(&stream);
            parseMs += milliseconds(Clock::now() - start);
            if (i == 0)
            {
                entryCount = archive.entries().size();
                for (const auto& entry : archive.entries())
                {
                    entryNames.push_back(entry.name);
                }
            }
        }

        double findMs = 0;
        if (!entryNames.empty())
        {
            const auto start = Clock::now();
            for (unsigned int i = 0; i < repeats; ++i)
            {
                rwe::SpanStream stream(bytes->data(), bytes->size());
                rwe::GafArchive archive(&stream);
                for (const auto& name : entryNames)
                {
                    (void)archive.findEntry(name);
                }
            }
            findMs = milliseconds(Clock::now() - start);
        }

        std::cout << "GAF name=" << gafName << " bytes=" << bytes->size() << " entries=" << entryCount
                  << " read_ms=" << (readMs / static_cast<double>(repeats))
                  << " parse_ms=" << (parseMs / static_cast<double>(repeats))
                  << " parse_and_find_all_ms=" << (findMs / static_cast<double>(repeats)) << '\n';
    }

    std::vector<std::string> mapNames;
    for (unsigned int i = 0; i < repeats; ++i)
    {
        const auto start = Clock::now();
        MapListTimings timings;
        mapNames = getMapNames(vfs, timings);
        std::cout << "MAPLIST run=" << (i + 1) << " elapsed_ms=" << milliseconds(Clock::now() - start)
                  << " enumerate_ms=" << timings.enumerationMs << " ota_read_ms=" << timings.readsMs
                  << " schema_parse_ms=" << timings.schemaParseMs << " candidates=" << timings.candidateCount
                  << " accepted=" << timings.acceptedCount << " slowest_ota=" << timings.slowestReadName
                  << ':' << timings.slowestReadMs << '\n';
    }

    const auto otaStart = Clock::now();
    auto bytes = vfs.readFile(std::string("maps/").append(mapName).append(".ota"));
    if (!bytes)
    {
        std::cerr << "Could not read map OTA: " << mapName << '\n';
        return 1;
    }
    const std::string otaText(bytes->begin(), bytes->end());
    const auto ota = rwe::parseOta(rwe::parseTdfFromString(otaText));
    std::cout << "SELECTED_MAP name=" << mapName << " elapsed_ms=" << milliseconds(Clock::now() - otaStart)
              << " bytes=" << bytes->size() << " schemas=" << ota.schemas.size() << '\n';
    return 0;
}
