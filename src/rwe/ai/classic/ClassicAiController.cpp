#include "ClassicAiController.h"

#include <utility>

namespace rwe
{
    // A stub, so that the engine selection can be built and tested before the
    // controller exists. Replaced wholesale by the controller itself.
    struct ClassicAiController::Impl
    {
        ClassicAiConfig config;
        std::uint64_t rngSeed;
    };

    ClassicAiController::ClassicAiController(PlayerId playerId, ClassicAiConfig config, std::uint64_t rngSeed)
        : playerId(playerId), impl(std::make_unique<Impl>(Impl{std::move(config), rngSeed}))
    {
        profile.name = "CLASSIC";
        profile.difficulty = impl->config.difficulty;
        profile.idle = impl->config.idle;
        switch (impl->config.difficulty)
        {
            case AiDifficulty::Easy:
                profile.resourceCheatMultiplier = SimScalar(0.5f);
                break;
            case AiDifficulty::Standard:
                profile.resourceCheatMultiplier = SimScalar(0.7f);
                break;
            default:
                profile.resourceCheatMultiplier = SimScalar(1.0f);
                break;
        }
    }

    ClassicAiController::~ClassicAiController() = default;

    void ClassicAiController::tick(const GameSimulation&, std::vector<PlayerCommand>&)
    {
    }
}
