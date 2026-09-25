#include "GameScene.h"

#include <cstdlib>
#include <rwe/game/chat_util.h>
#include <rwe/util/SimpleLogger.h>

// In-game chat, and the message bar the original opens on Enter (issue #30,
// and the Phase 3 roadmap entry).
//
// The decision worth stating is where chat does *not* go: the command stream.
// A lockstep tick cannot run until every peer's commands for it have arrived,
// so the moment a player most wants to say something -- the game has stopped
// because somebody's packets have -- is exactly the moment a command cannot be
// delivered. Chat therefore rides beside the commands in the same packet,
// acked and resent like the sync hashes, and arrives whether or not the
// simulation is advancing.
//
// The other half of that decision: nothing typed here may reach the
// simulation. A line of chat is never hashed, never saved, never recorded in a
// replay and never seen by any peer's GameSimulation. It is presentation, and
// it is drawn from the scene's own state, exactly as the console messages are.

namespace rwe
{
    bool GameScene::isChatBarOpen() const
    {
        return chatInput.has_value();
    }

    void GameScene::openChatBar()
    {
        // A replay has no peers to talk to and no player to talk as. The
        // original refuses the bar to a watcher for the same reason.
        if (replayPlayback)
        {
            return;
        }

        chatInput = std::string();
    }

    void GameScene::handleChatBarKey(const SDL_KeyboardEvent& keysym)
    {
        switch (keysym.key)
        {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                sendChatMessage();
                chatInput = std::nullopt;
                break;
            case SDLK_ESCAPE:
                chatInput = std::nullopt;
                break;
            case SDLK_BACKSPACE:
                chatTextBackspace(*chatInput);
                break;
            default:
                // Swallowed: while the bar is open the keyboard is the bar's,
                // and the letters arrive as text input rather than as keys.
                break;
        }
    }

    void GameScene::sendChatMessage()
    {
        auto text = sanitizeChatText(*chatInput);
        if (text.empty())
        {
            return;
        }

        if (!gameNetworkService->submitChatMessage(text))
        {
            printConsole("Message not sent: a player is not answering", Color(252, 252, 0));
            return;
        }

        // Shown from here rather than waited for, because it never comes back:
        // each peer is sent it once and nobody echoes it.
        printChatLine(localPlayerId, text);
    }

    void GameScene::updateChatTest()
    {
        // Read once and kept: an environment variable cannot change under a
        // running process, and this is asked every frame.
        static const auto spec = []() -> std::optional<std::pair<unsigned int, std::string>> {
            const auto* raw = std::getenv("RWE_CHAT_TEST");
            if (raw == nullptr)
            {
                return std::nullopt;
            }

            std::string value(raw);
            auto colon = value.find(':');
            if (colon == std::string::npos)
            {
                LOG_ERROR << "RWE_CHAT_TEST wants <tick>:<text>, got: " << value;
                return std::nullopt;
            }

            return std::make_pair(
                static_cast<unsigned int>(std::stoul(value.substr(0, colon))),
                value.substr(colon + 1));
        }();

        if (!spec || chatTestSent || sceneTime.value < spec->first)
        {
            return;
        }

        chatTestSent = true;
        chatInput = spec->second;
        sendChatMessage();
        chatInput = std::nullopt;
    }

    void GameScene::receiveChatMessages()
    {
        for (const auto& message : gameNetworkService->takeChatMessages())
        {
            printChatLine(message.sender, message.text);
        }
    }

    void GameScene::printChatLine(PlayerId sender, const std::string& text)
    {
        // In the speaker's own colour, into the same console the game's own
        // announcements use -- so it ages out after its five seconds and F12
        // clears it, both of which the original's message list does too.
        printConsole(
            formatChatLine(playerDisplayName(sender), text),
            playerColorToRgb(simulation.getPlayer(sender).color));

        LOG_INFO << "Chat from player " << sender.value << ": " << text;
    }

    void GameScene::renderChatBar()
    {
        if (!chatInput)
        {
            return;
        }

        // Along the bottom of the world view, above the panel -- where the
        // original puts TALK.GUI -- and in the font and the left margin the
        // console above it uses, so the two read as one column of text.
        auto y = static_cast<float>(sceneContext.viewport->height()) / static_cast<float>(effectiveUiScale())
            - static_cast<float>(GuiSizeBottom) - 14.0f;

        // A blinking caret, on the wall clock rather than on scene time: the
        // bar is most wanted while the game is stalled, and scene time is
        // exactly what has stopped.
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(getTimestamp().time_since_epoch()).count();
        auto caret = (millis / 500) % 2 == 0 ? "_" : "";

        chromeUiRenderService.drawText(
            static_cast<float>(GuiSizeLeft) + 8.0f,
            y,
            "Message: " + *chatInput + caret,
            *speechFont,
            Color(252, 252, 252));
    }
}
