#include "PathfindingBackend.h"

namespace rwe
{
    const char* pathfindingBackendName(PathfindingBackend backend)
    {
        switch (backend)
        {
            case PathfindingBackend::RweAStar:
                return "rwe";
            case PathfindingBackend::OpenAnnihilation:
                return "oa";
        }
        return "rwe";
    }

    PathfindingBackend pathfindingBackendFromString(const std::string& value, PathfindingBackend fallback)
    {
        if (value == "rwe" || value == "astar")
        {
            return PathfindingBackend::RweAStar;
        }
        if (value == "oa" || value == "open-annihilation" || value == "openannihilation")
        {
            return PathfindingBackend::OpenAnnihilation;
        }
        return fallback;
    }
}
