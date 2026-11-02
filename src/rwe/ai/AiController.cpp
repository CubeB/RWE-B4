#include "AiController.h"

#include <cctype>
#include <cstring>

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
}
