#include "AiController.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace rwe
{
    const char* aiEngineName(AiEngine engine)
    {
        switch (engine)
        {
            case AiEngine::Classic:
                return "classic";
            case AiEngine::Rwe:
                return "rwe";
            case AiEngine::Lua:
                return "lua";
        }
        return "classic";
    }

    bool aiEngineFromName(const char* name, AiEngine& out)
    {
        if (name == nullptr)
        {
            return false;
        }
        std::string lowered;
        for (const char* p = name; *p != '\0'; ++p)
        {
            lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        }
        if (lowered == "classic")
        {
            out = AiEngine::Classic;
            return true;
        }
        if (lowered == "rwe")
        {
            out = AiEngine::Rwe;
            return true;
        }
        if (lowered == "lua")
        {
            out = AiEngine::Lua;
            return true;
        }
        return false;
    }

    AiController::~AiController() = default;

    nlohmann::json AiController::saveState() const
    {
        return nullptr;
    }

    void AiController::loadState(const nlohmann::json&)
    {
        throw std::runtime_error(std::string("the ") + aiEngineName(engine())
            + " engine keeps no saved state, so a save cannot restore one");
    }

    std::string AiController::stateHash() const
    {
        return {};
    }

    namespace
    {
        std::vector<PlayerId> sortedPlayers(const AiControllerMap& controllers)
        {
            std::vector<PlayerId> players;
            players.reserve(controllers.size());
            for (const auto& [player, controller] : controllers)
            {
                if (controller)
                {
                    players.push_back(player);
                }
            }
            // The map is unordered; a save and a hash must not be.
            std::sort(players.begin(), players.end(), [](PlayerId a, PlayerId b) { return a.value < b.value; });
            return players;
        }
    }

    nlohmann::json aiControllerStates(const AiControllerMap& controllers)
    {
        auto states = nlohmann::json::array();
        for (const auto player : sortedPlayers(controllers))
        {
            auto state = controllers.at(player)->saveState();
            if (!state.is_null())
            {
                states.push_back({{"player", player.value}, {"state", std::move(state)}});
            }
        }
        return states.empty() ? nlohmann::json(nullptr) : states;
    }

    void loadAiControllerStates(AiControllerMap& controllers, const nlohmann::json& states)
    {
        if (!states.is_array())
        {
            throw std::runtime_error("a saved game's AI states must be an array");
        }
        for (const auto& entry : states)
        {
            if (!entry.is_object() || !entry.contains("player") || !entry["player"].is_number_unsigned()
                || !entry.contains("state"))
            {
                throw std::runtime_error("a saved AI state must name its player");
            }
            const auto player = PlayerId(entry["player"].get<unsigned int>());
            const auto found = controllers.find(player);
            if (found == controllers.end() || !found->second)
            {
                throw std::runtime_error(
                    "the save has AI state for player " + std::to_string(player.value) + ", who has no computer player here");
            }
            found->second->loadState(entry["state"]);
        }
    }

    std::string aiControllerStatesHash(const AiControllerMap& controllers)
    {
        std::string combined;
        for (const auto player : sortedPlayers(controllers))
        {
            const auto hash = controllers.at(player)->stateHash();
            if (!hash.empty())
            {
                combined += std::to_string(player.value) + ":" + hash + ";";
            }
        }
        return combined;
    }
}
