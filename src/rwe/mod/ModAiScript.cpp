#include "ModAiScript.h"

#include <fstream>
#include <iterator>
#include <rwe/mod/ModTransform.h>
#include <stdexcept>

namespace rwe
{
    std::string readModAiScriptFile(const std::string& packageId, const std::string& path)
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

    std::uint32_t modAiSeedFromContent(const ModLaunchContent* content)
    {
        if (content == nullptr || content->identity.size() < 8)
        {
            return 0;
        }
        return static_cast<std::uint32_t>(std::stoul(content->identity.substr(0, 8), nullptr, 16));
    }

    std::shared_ptr<const ModAiScript> resolveModAiScript(
        const ModLaunchContent* content,
        const std::string& packageId,
        std::uint32_t seed)
    {
        if (content == nullptr)
        {
            throw std::runtime_error(
                "This seat plays the lua engine, but the game has no resolved mod set; "
                "an ai package must be resolved before launch");
        }

        const ModLaunchPackage* found = nullptr;
        for (const auto& package : content->packages)
        {
            if (!packageId.empty())
            {
                if (package.id == packageId)
                {
                    found = &package;
                    break;
                }
                continue;
            }
            if (package.kind == "ai")
            {
                found = &package;
                break;
            }
        }

        if (found == nullptr)
        {
            if (packageId.empty())
            {
                throw std::runtime_error(
                    "This seat plays the lua engine, but the resolved mod set has no ai package");
            }
            throw std::runtime_error(
                "Mod " + packageId + ": no such package in the resolved mod set");
        }
        if (found->kind != "ai")
        {
            throw std::runtime_error(
                "Mod " + found->id + ": not an ai package (kind is '" + found->kind + "')");
        }
        if (found->scriptPath.empty())
        {
            // A save or replay records identity, not paths. Playing a
            // different script would diverge from the recording.
            throw std::runtime_error(
                "Mod " + found->id + ": the recorded ai package has not been resolved on this machine");
        }

        auto script = std::make_shared<ModAiScript>();
        script->packageId = found->id;
        script->scriptName = found->scriptName;
        script->options = found->options;
        script->seed = seed;
        script->source = readModAiScriptFile(found->id, found->scriptPath);
        return script;
    }
}
