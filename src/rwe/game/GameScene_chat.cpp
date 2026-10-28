#include "GameScene.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <rwe/game/chat_command.h>
#include <rwe/game/chat_util.h>
#include <rwe/util/Index.h>
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
        // A replay or a demo has no peers to talk to and no player to talk as.
        // The original refuses the bar to a watcher for the same reason.
        if (isPlayback())
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
            case SDLK_TAB:
                // The talk dialog's SENDTYPE button: everybody, allies,
                // non-allies, a custom list (BT-176).
                chatSendMode.cycle();
                break;
            case SDLK_BACKSLASH:
                // #906: the original's repeat key is virtual-key 0x5C, which
                // is this key on a US layout. It reruns the saved line under
                // all permission bits, but only after the developer phrase
                // has unlocked debug.
                if (debugCommandsEnabled && lastCommandLine)
                {
                    runChatCommand(*lastCommandLine);
                }
                break;
            default:
                // Ctrl and a digit adds or removes that player slot from the
                // custom list, the LIVEPLYR buttons' job in the original.
                if ((keysym.mod & SDL_KMOD_CTRL) != 0 && keysym.key >= SDLK_0 && keysym.key <= SDLK_9)
                {
                    chatSendMode.toggleSlot(static_cast<unsigned int>(keysym.key - SDLK_0));
                }
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

        // A line that starts with `+` is a command to the game and is never
        // chat, whether or not it turns out to be one this build knows: the
        // original's talk dialog hands it to the command interpreter locally,
        // so it must not reach the other players' screens either.
        if (parseChatCommand(text))
        {
            // The original's line runner saves the submitted line as it goes,
            // capped at 79 bytes, and that saved copy is what the repeat key
            // reruns (#906). Saved before dispatch, so a line that fails to
            // run is still the last one.
            lastCommandLine = text.substr(0, 79);
            runChatCommand(text);
            return;
        }

        // Who it is for is decided here, on the sending machine, and the line
        // is queued only for those peers: a private line is never put on the
        // wire to anyone else, so there is nothing for a modified client to
        // read (BT-176). Nothing of this reaches the simulation.
        auto route = routeChatLine(text, chatSendMode);
        auto wireText = capChatForWire(playerDisplayName(localPlayerId), route.label, route.text);
        if (wireText.empty())
        {
            return;
        }

        // A TA joiner is the one remote player there is, and it is reached
        // through its own link and not through the lockstep service, which has
        // no peers in an own-clock game.
        auto taPeer = taHostRemotePlayer();

        std::vector<unsigned int> peers;
        if (taPeer)
        {
            peers.push_back(taPeer->value);
        }
        else
        {
            for (const auto& id : gameNetworkService->remotePlayerIds())
            {
                peers.push_back(id.value);
            }
        }

        auto chosen = chatRecipients(route.address, peers, localPlayerId.value, [&](unsigned int a, unsigned int b) {
            return simulation.arePlayersAllied(PlayerId(a), PlayerId(b));
        });

        // A line aimed at particular slots, none of which is a player in the
        // game, goes nowhere -- the original sends nothing either.
        if (chosen.empty() && route.address.kind == ChatAddress::Kind::Players)
        {
            printConsole("Message not sent: no such player", Color(252, 252, 0));
            return;
        }

        std::vector<PlayerId> recipients;
        for (auto id : chosen)
        {
            recipients.push_back(PlayerId(id));
        }

        auto delivered = taPeer
            ? submitTaHostChat(wireText, route.label, route.address.kind != ChatAddress::Kind::Everyone, recipients)
            : gameNetworkService->submitChatMessage(wireText, route.label, recipients);
        if (!delivered)
        {
            printConsole("Message not sent: a player is not answering", Color(252, 252, 0));
            return;
        }

        // Shown from here rather than waited for, because it never comes back:
        // each peer is sent it once and nobody echoes it.
        printChatLine(localPlayerId, wireText, route.label);
    }

    void GameScene::runChatCommand(const std::string& line)
    {
        auto parsed = parseChatCommand(line);
        if (!parsed || parsed->kind == ChatCommand::Kind::Empty)
        {
            return;
        }

        const auto& command = *parsed;
        const Color feedbackColor(252, 252, 0);

        // #906: the mask the original computes per submitted line. Campaign
        // is 1, skirmish 3, multiplayer uses the host's Cheat Codes option
        // (RWE has no such lobby option, and no cheat or debug handlers yet,
        // so those bits change nothing today). The local developer phrase
        // raises it to 7.
        const auto mask = chatCommandMask(
            debugCommandsEnabled,
            gameParameters.mission,
            gameNetworkService->hasRemotePeers(),
            false);

        if (command.kind == ChatCommand::Kind::Now)
        {
            // The exact phrase unlocks; any other `Now` clears it. Nothing is
            // printed, and it takes effect on the next line (#906).
            debugCommandsEnabled = command.developerPhraseAccepted;
            return;
        }

        if (command.kind == ChatCommand::Kind::NoShake)
        {
            // #907: UI-option bit 4. A local preference, not saved, and the
            // original prints nothing for it.
            screenShakeEnabled = !screenShakeEnabled;
            return;
        }

        if (command.kind == ChatCommand::Kind::Gamma)
        {
            // The original sets brightness to the raw integer x 0.1 and does
            // not clamp (#907, audio/option finding section 2.2). RWE's field
            // is a percentage where 100 is 1.0, so the command's n maps to
            // n * 10; the unsigned field floors at zero.
            gammaSetting = static_cast<unsigned int>(std::max(0, command.number * 10));
            saveInGameOptions();
            return;
        }

        if (command.kind == ChatCommand::Kind::ScrollSpeed)
        {
            // The original's unit is pixels per simulation tick (menu 1..65,
            // default 32); RWE stores a percentage of 1000 world units/s, so
            // 32 maps to 100. Its saved field is a byte and has no minimum,
            // so the conversion is clamped only to that byte (#907).
            const auto percent = command.number * 100 / 32;
            scrollSpeedSetting = static_cast<unsigned int>(std::clamp(percent, 0, 255));
            saveInGameOptions();
            return;
        }

        // Every command built so far is in the ordinary table.
        if ((mask & ChatCommandMaskOrdinary) == 0)
        {
            return;
        }

        if (command.kind == ChatCommand::Kind::Unknown)
        {
            printConsole("Unknown command: " + command.word, feedbackColor);
            return;
        }

        // Every sharing command needs a network game and acts on the local
        // player, and a computer player or a watcher has no stock to share.
        // An own-clock game is a peer that owns its own clock and its own
        // units: there is no one here to share with.
        const auto& local = simulation.getPlayer(localPlayerId);
        if (isPlayback() || isOwnClock() || !gameNetworkService->hasRemotePeers() || local.type != GamePlayerType::Human)
        {
            printConsole("That command only works in a network game.", feedbackColor);
            return;
        }

        auto onOff = [](bool on) { return on ? "on" : "off"; };
        auto submit = [&](PlayerShareCommand::Kind kind, float amount = 0.0f) {
            PlayerShareCommand share;
            share.kind = kind;
            share.amount = amount;
            localPlayerCommandBuffer.push_back(share);
        };

        // The state printed is the simulation's as it stands, before the flip
        // this line asks for; the flip itself lands a few ticks later, with
        // everyone's, through the command stream.
        switch (command.kind)
        {
            case ChatCommand::Kind::ShareMetal:
                printConsole(std::string("ShareMetal: ") + onOff(!local.shareMetal), feedbackColor);
                submit(PlayerShareCommand::Kind::ToggleMetal);
                break;

            case ChatCommand::Kind::ShareEnergy:
                printConsole(std::string("ShareEnergy: ") + onOff(!local.shareEnergy), feedbackColor);
                submit(PlayerShareCommand::Kind::ToggleEnergy);
                break;

            case ChatCommand::Kind::ShareAll:
                // Two independent toggles, as in the original: with mixed
                // starting states it is not "all on".
                printConsole(std::string("ShareMetal: ") + onOff(!local.shareMetal), feedbackColor);
                printConsole(std::string("ShareEnergy: ") + onOff(!local.shareEnergy), feedbackColor);
                submit(PlayerShareCommand::Kind::ToggleMetal);
                submit(PlayerShareCommand::Kind::ToggleEnergy);
                break;

            case ChatCommand::Kind::ShareMapping:
                printConsole(shareNoticeMessage(command.kind), feedbackColor);
                break;

            case ChatCommand::Kind::ShareRadar:
                printConsole(shareNoticeMessage(command.kind), feedbackColor);
                break;

            case ChatCommand::Kind::SetShareMetal:
                // The number typed, not the number stored: the original
                // prints what it was given and stores the capped value.
                printConsole("SetShareMetal: " + std::to_string(command.number), feedbackColor);
                submit(PlayerShareCommand::Kind::SetMetalReserve, static_cast<float>(command.number));
                break;

            case ChatCommand::Kind::SetShareEnergy:
                printConsole("SetShareEnergy: " + std::to_string(command.number), feedbackColor);
                submit(PlayerShareCommand::Kind::SetEnergyReserve, static_cast<float>(command.number));
                break;

            case ChatCommand::Kind::Give:
            {
                // A player is named as the game names them: by the name they
                // joined with, or as "Player n" with n counted from one.
                std::optional<PlayerId> target;
                for (Index i = 0; i < getSize(simulation.players); ++i)
                {
                    PlayerId id(static_cast<unsigned int>(i));
                    auto name = playerDisplayName(id);
                    if (name.size() == command.playerToken.size()
                        && std::equal(name.begin(), name.end(), command.playerToken.begin(), [](char a, char b) {
                               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                           }))
                    {
                        target = id;
                        break;
                    }
                }

                if (!target)
                {
                    auto number = chatAtoi(command.playerToken);
                    if (number >= 1 && number <= getSize(simulation.players))
                    {
                        target = PlayerId(static_cast<unsigned int>(number - 1));
                    }
                }

                if (!target || *target == localPlayerId)
                {
                    printConsole("Give: no such player.", feedbackColor);
                    break;
                }

                PlayerShareCommand give;
                give.kind = PlayerShareCommand::Kind::Give;
                give.amount = static_cast<float>(command.number);
                give.target = target;
                give.giveMetal = command.giveMetal;
                localPlayerCommandBuffer.push_back(give);
                printConsole(
                    "Give: " + std::to_string(command.number) + (command.giveMetal ? " metal to " : " energy to ") + playerDisplayName(*target),
                    feedbackColor);
                break;
            }

            case ChatCommand::Kind::GiveUsage:
                printConsole("Usage: +Give player amount metal|energy", feedbackColor);
                break;

            case ChatCommand::Kind::Empty:
            case ChatCommand::Kind::Unknown:
            case ChatCommand::Kind::Now:
            case ChatCommand::Kind::NoShake:
            case ChatCommand::Kind::Gamma:
            case ChatCommand::Kind::ScrollSpeed:
                break;
        }
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
            printChatLine(message.sender, message.text, message.label);
        }
    }

    void GameScene::printChatLine(PlayerId sender, const std::string& text, ChatLabel label)
    {
        // In the speaker's own colour, into the same console the game's own
        // announcements use -- so it ages out after its five seconds and F12
        // clears it, both of which the original's message list does too.
        printConsole(
            formatChatLine(playerDisplayName(sender), label, text),
            playerColorToRgb(simulation.getPlayer(sender).color));

        LOG_INFO << "Chat from player " << sender.value << ": " << text;
    }

    void GameScene::printTaChatLine(PlayerId sender, const std::string& text)
    {
        // Off a socket or out of a recording, so it is cleaned as a peer's
        // line is: invalid UTF-8 replaced, control characters dropped, length
        // bounded.
        auto clean = sanitizeChatText(text);
        if (clean.empty())
        {
            return;
        }

        printConsole(clean, playerColorToRgb(simulation.getPlayer(sender).color));
        LOG_INFO << "Chat from player " << sender.value << ": " << clean;
    }

    std::string GameScene::chatBarPrompt() const
    {
        switch (chatSendMode.mode)
        {
            case 1:
                return "Message (Allies): ";
            case 2:
                return "Message (Enemies): ";
            case 3:
            {
                std::string slots;
                for (unsigned int i = 0; i < 10; ++i)
                {
                    if ((chatSendMode.customSlots >> i) & 1u)
                    {
                        slots += (slots.empty() ? "" : ",") + std::to_string(i);
                    }
                }
                return "Message (Players " + slots + "): ";
            }
            default:
                return "Message: ";
        }
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
            chatBarPrompt() + *chatInput + caret,
            *speechFont);
    }
}
