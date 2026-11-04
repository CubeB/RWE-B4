#include "PresentationScript.h"

#include <fstream>
#include <iterator>
#include <rwe/mod/ModTransform.h>
#include <stdexcept>

namespace rwe
{
    std::string readPresentationScript(const std::string& packageId, const std::string& path)
    {
        std::ifstream scriptFile(path, std::ios::binary);
        if (!scriptFile)
        {
            throw std::runtime_error("Mod " + packageId + ": cannot open " + path);
        }
        std::string source(
            (std::istreambuf_iterator<char>(scriptFile)), std::istreambuf_iterator<char>());
        if (source.size() > 1024 * 1024)
        {
            throw std::runtime_error("Mod " + packageId + ": script is larger than 1 MiB");
        }
        return source;
    }

    bool hasPresentationPackages(const ModLaunchContent* content)
    {
        if (content == nullptr)
        {
            return false;
        }
        for (const auto& package : content->packages)
        {
            if (package.kind == "presentation")
            {
                return true;
            }
        }
        return false;
    }

    std::vector<PresentationPackage> resolvedPresentationPackages(const ModLaunchContent* content)
    {
        std::vector<PresentationPackage> result;
        if (content == nullptr)
        {
            return result;
        }

        for (const auto& package : content->packages)
        {
            if (package.kind != "presentation")
            {
                continue;
            }

            if (package.scriptPath.empty())
            {
                // A save or replay records identity, not paths. Presentation
                // is not simulation state: playing without it is allowed.
                continue;
            }

            PresentationPackage resolved;
            resolved.packageId = package.id;
            resolved.scriptName = package.scriptName.empty()
                ? package.id + "/presentation.lua"
                : package.scriptName;
            resolved.scriptPath = package.scriptPath;
            // Deliberately not readVerifiedModScript: +ReloadPresentation
            // exists to pick up an edited HUD script, and presentation takes
            // no part in the simulation, so its script carries no entry hash
            // (MOD-SDK-013, MOD-SDK-014).
            resolved.source = readPresentationScript(package.id, package.scriptPath);
            resolved.options = package.options;
            result.push_back(std::move(resolved));
        }

        return result;
    }
}
