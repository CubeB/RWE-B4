#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
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
        else
        {
            std::cerr << "Usage: menu_lag_probe --data-path <path> [--map <name>] [--repeats <n>]\n";
            return 2;
        }
    }
    if (dataPath.empty() || repeats == 0)
    {
        std::cerr << "A data path and positive repeat count are required\n";
        return 2;
    }

    auto vfs = rwe::constructVfs(dataPath);
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
