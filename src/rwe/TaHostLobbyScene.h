#pragma once

#include <memory>
#include <optional>
#include <rwe/LoadingScene.h>
#include <rwe/SceneContext.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/net/ta/TaBattleroom.h>
#include <rwe/scene/Scene.h>
#include <string>

namespace rwe
{
    class TaHostGame;

    /**
     * The waiting panel in front of a hosted game: what the map is, which
     * ports the DirectPlay sockets are on, and who has joined the battleroom,
     * with the side, colour, team and ready state its own status record gave.
     *
     * The host session and the battleroom run on a thread of their own
     * (`TaHostGame`); nothing here touches them but through that object. The
     * panel launches the game on Enter, or at once with --auto-launch, and
     * loads the map the ordinary way when the launch has been sent.
     */
    class TaHostLobbyScene : public Scene
    {
    public:
        TaHostLobbyScene(
            const SceneContext& sceneContext,
            TdfBlock* audioLookup,
            AudioService::LoopToken&& bgm,
            std::shared_ptr<TaHostGame> host,
            std::string mapName,
            unsigned int schemaIndex);

        void init() override;

        void update(int millisecondsElapsed) override;


        void onKeyDown(const SDL_KeyboardEvent& event) override;

    private:
        struct Launched
        {
            /** The joiner's own side and colour, from its 0x20. */
            std::string side;

            PlayerColorIndex color{0};

            unsigned int team{TaNoTeam};

            std::string name;
        };

        SceneContext sceneContext;
        TdfBlock* audioLookup;
        AudioService::LoopToken bgm;
        std::shared_ptr<TaHostGame> host;
        std::string mapName;
        unsigned int schemaIndex;

        std::optional<Launched> launched;
        bool loading{false};

        /**
         * The commander's own figures, read from the loaded data set when the
         * panel opens: a 0x09 names a type by its position in the unit listing,
         * which is a property of the data and not of the wire.
         */
        std::optional<TaBattleroom::LaunchParams> params;

        std::optional<TaBattleroom::LaunchParams> computeLaunchParams() const;

        void drawPanel();
        void tryLaunch();
        void launch();
        void startLoading();
    };
}
