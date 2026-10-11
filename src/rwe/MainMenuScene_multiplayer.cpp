#include "MainMenuScene_multiplayer.h"

#include "MainMenuScene.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <rwe/MultiplayerSetup.h>
#include <rwe/TextureService.h>
#include <rwe/rwe_time.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/ui/UiLabel.h>
#include <rwe/ui/UiListBox.h>
#include <rwe/ui/UiScrollBar.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>

/**
 * The multiplayer front end: TotalA.exe's own flow, built from its own GUI
 * files and art. See MainMenuScene_multiplayer.h for the overview; this file
 * is the logic that needs no window (the free functions) followed by the
 * scene's half of it (the MainMenuScene members).
 */
namespace rwe
{
    using PlayerType = MainMenuModel::PlayerSettings::Type;
    using PlayerSide = MainMenuModel::PlayerSettings::Side;

    // ------------------------------------------------------------------
    // The model and the announcement
    // ------------------------------------------------------------------

    namespace
    {
        struct OptionSpec
        {
            const char* name;
            unsigned int stages;
            BehaviorSubject<unsigned int> MainMenuModel::SkirmishOptions::*subject;
        };

        // One table, read in both directions. The stage counts are the
        // number of faces SKIRMISH.GUI gives each button.
        const OptionSpec optionSpecs[] = {
            {"commander", 2, &MainMenuModel::SkirmishOptions::commanderDeath},
            {"startloc", 2, &MainMenuModel::SkirmishOptions::startLocation},
            {"mapping", 2, &MainMenuModel::SkirmishOptions::mapping},
            {"los", 3, &MainMenuModel::SkirmishOptions::lineOfSight},
            {"difficulty", 3, &MainMenuModel::SkirmishOptions::difficulty},
        };

        std::optional<unsigned int> parseStage(const std::string& text, unsigned int stages)
        {
            if (text.empty() || text.size() > 3)
            {
                return std::nullopt;
            }
            unsigned int value = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                {
                    return std::nullopt;
                }
                value = (value * 10) + static_cast<unsigned int>(c - '0');
            }
            if (value >= stages)
            {
                return std::nullopt;
            }
            return value;
        }

        const DiscoveredSlot* findSlot(const DiscoveredGame& game, int index)
        {
            auto it = std::find_if(game.slots.begin(), game.slots.end(), [index](const auto& s) { return s.index == index; });
            return it == game.slots.end() ? nullptr : &*it;
        }

        const std::string* findOption(const LobbyOptions& options, const std::string& name)
        {
            for (const auto& [n, v] : options)
            {
                if (n == name)
                {
                    return &v;
                }
            }
            return nullptr;
        }

        bool isHexToken(const std::string& s, std::size_t begin, std::size_t length)
        {
            if (begin + length > s.size())
            {
                return false;
            }
            for (std::size_t i = begin; i < begin + length; ++i)
            {
                auto c = s[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                {
                    return false;
                }
            }
            return true;
        }
    }

    LobbyOptions optionsFromModel(const MainMenuModel& model)
    {
        LobbyOptions options;
        for (const auto& spec : optionSpecs)
        {
            options.emplace_back(spec.name, std::to_string((model.skirmishOptions.*spec.subject).getValue()));
        }
        return options;
    }

    void applyOptionsToModel(MainMenuModel& model, const LobbyOptions& options)
    {
        for (const auto& spec : optionSpecs)
        {
            const auto* value = findOption(options, spec.name);
            if (value == nullptr)
            {
                continue;
            }
            if (auto stage = parseStage(*value, spec.stages))
            {
                (model.skirmishOptions.*spec.subject).next(*stage);
            }
        }
    }

    DiscoveredGame lobbyFromModel(
        const MainMenuModel& model,
        const LobbyIdentity& identity,
        const std::string& mapName,
        const std::vector<std::string>& seatTokens)
    {
        DiscoveredGame game;
        game.session = identity.session;
        game.map = mapName;
        game.port = identity.port;
        game.label = identity.label;
        game.hostName = identity.hostName.substr(0, maxPlayerNameLength);
        game.passwordHash = identity.passwordHash;
        game.launching = identity.launching;
        game.options = optionsFromModel(model);

        // The newest lines only, which is all an announcement has room for.
        const auto& said = identity.chat;
        const auto firstSaid = said.size() > LanDiscovery::maxChatLines ? said.size() - LanDiscovery::maxChatLines : 0;
        game.chat.assign(said.begin() + static_cast<std::ptrdiff_t>(firstSaid), said.end());

        for (std::size_t i = 0; i < model.players.size(); ++i)
        {
            const auto& p = model.players[i];

            DiscoveredSlot seat;
            seat.index = static_cast<int>(i);
            seat.side = p.side.getValue() == PlayerSide::Core ? "CORE" : "ARM";
            seat.colour = p.colorIndex.getValue().value;
            seat.team = p.teamIndex.getValue().value_or(-1);
            seat.metal = static_cast<int>(p.metal.getValue().value);
            seat.energy = static_cast<int>(p.energy.getValue().value);

            switch (p.type.getValue())
            {
                case PlayerType::Open:
                    seat.controller = "open";
                    break;
                case PlayerType::Human:
                    // The host's own seat. The host is never anyone the room
                    // is waiting for.
                    seat.controller = "human";
                    seat.name = p.name.getValue().substr(0, maxPlayerNameLength);
                    seat.ready = true;
                    break;
                case PlayerType::Computer:
                    seat.controller = "computer";
                    seat.personality = p.personality.getValue().substr(0, LanDiscovery::maxPersonalityLength);
                    seat.ready = true;
                    break;
                case PlayerType::Network:
                    seat.controller = "network";
                    seat.address = p.networkAddress.getValue();
                    seat.name = p.name.getValue().substr(0, maxPlayerNameLength);
                    if (i < seatTokens.size())
                    {
                        seat.personality = seatTokens[i];
                    }
                    seat.ready = i < identity.ready.size() && identity.ready[i];
                    break;
            }

            game.slots.push_back(std::move(seat));
        }

        return game;
    }

    std::optional<int> firstOpenSeat(const DiscoveredGame& game, const std::vector<int>& notThese)
    {
        auto sorted = game.slots;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
        for (const auto& slot : sorted)
        {
            if (slot.controller != "open")
            {
                continue;
            }
            if (std::find(notThese.begin(), notThese.end(), slot.index) != notThese.end())
            {
                continue;
            }
            return slot.index;
        }
        return std::nullopt;
    }

    void applyLobbyToModel(
        MainMenuModel& model,
        const DiscoveredGame& game,
        int mySeat,
        const std::string& hostGameAddress,
        const std::string& myName)
    {
        applyOptionsToModel(model, game.options);

        bool teamsChanged = false;

        for (std::size_t i = 0; i < model.players.size(); ++i)
        {
            auto& p = model.players[i];
            const auto* slot = findSlot(game, static_cast<int>(i));

            if (slot == nullptr)
            {
                p.name.next(std::string());
                p.networkAddress.next(std::string());
                p.type.next(PlayerType::Open);
                continue;
            }

            // Everything a row's widgets are built from is set before the
            // type, because changing the type is what rebuilds the row.
            // Values from the wire are range-checked here as well as by the
            // codec: a colour or a team that indexes a sprite sheet is
            // somebody else's number reaching an array.
            PlayerType type;
            std::string name;
            std::string address;

            if (static_cast<int>(i) == mySeat)
            {
                type = PlayerType::Human;
                name = myName;
            }
            else if (slot->controller == "human")
            {
                // The host's own seat, which it calls human and the guest
                // sees as the machine to connect to.
                type = PlayerType::Network;
                name = slot->name.empty() ? game.hostName : slot->name;
                address = hostGameAddress;
            }
            else if (slot->controller == "network")
            {
                type = PlayerType::Network;
                name = slot->name;
                address = slot->address;
            }
            else if (slot->controller == "computer")
            {
                type = PlayerType::Computer;
            }
            else
            {
                type = PlayerType::Open;
            }

            p.name.next(name);
            p.networkAddress.next(address);
            p.side.next(slot->side == "CORE" ? PlayerSide::Core : PlayerSide::Arm);
            p.colorIndex.next(PlayerColorIndex(slot->colour < 10 ? slot->colour : static_cast<unsigned int>(i)));

            std::optional<int> team;
            if (slot->team >= 0 && slot->team <= 4)
            {
                team = slot->team;
            }
            if (p.teamIndex.getValue() != team)
            {
                teamsChanged = true;
            }
            p.teamIndex.next(team);

            p.metal.next(Metal(static_cast<float>(slot->metal)));
            p.energy.next(Energy(static_cast<float>(slot->energy)));
            if (type == PlayerType::Computer && !slot->personality.empty())
            {
                p.personality.next(slot->personality);
            }

            p.type.next(type);
        }

        if (teamsChanged)
        {
            // The team icons of the rows that share a team change with it.
            for (int t = 0; t <= 4; ++t)
            {
                model.teamChanges.next(t);
            }
        }
    }

    // ------------------------------------------------------------------
    // Seats and claims
    // ------------------------------------------------------------------

    std::string claimSession(const std::string& hostSession, const std::string& token)
    {
        return hostSession + "-g" + token;
    }

    bool isClaimSession(const std::string& session)
    {
        // "-g" and a token, at the end.
        const auto tail = claimTokenLength + 2;
        return session.size() > tail
            && session[session.size() - tail] == '-'
            && session[session.size() - tail + 1] == 'g'
            && isHexToken(session, session.size() - claimTokenLength, claimTokenLength);
    }

    std::vector<SeatClaim> claimsOn(const std::vector<DiscoveredGame>& heard, const std::string& hostSession)
    {
        std::vector<SeatClaim> claims;
        const auto prefix = hostSession + "-g";
        for (const auto& g : heard)
        {
            if (g.session.size() != prefix.size() + claimTokenLength
                || g.session.compare(0, prefix.size(), prefix) != 0
                || !isHexToken(g.session, prefix.size(), claimTokenLength)
                || g.slots.empty())
            {
                continue;
            }

            SeatClaim claim;
            claim.token = g.session.substr(prefix.size());
            claim.seat = g.slots.front().index;
            claim.name = g.slots.front().name.empty() ? g.hostName : g.slots.front().name;
            claim.name = claim.name.substr(0, maxPlayerNameLength);
            claim.address = gameAddress(g.sourceAddress, g.port);
            claim.side = g.slots.front().side;
            claim.colour = g.slots.front().colour;
            claim.team = g.slots.front().team;
            claim.ready = g.slots.front().ready;
            claim.chat = g.chat;
            claims.push_back(std::move(claim));
        }
        return claims;
    }

    DiscoveredGame claimAnnouncement(
        const DiscoveredGame& host,
        int seat,
        const std::string& token,
        const std::string& name,
        unsigned int gamePort,
        const std::optional<GuestChoice>& choice,
        const std::vector<DiscoveredChatLine>& chat)
    {
        DiscoveredGame claim;
        claim.session = claimSession(host.session, token);
        claim.map = host.map;
        claim.port = gamePort;
        claim.hostName = name.substr(0, maxPlayerNameLength);

        DiscoveredSlot slot;
        slot.index = seat;
        slot.controller = "human";
        slot.name = name.substr(0, maxPlayerNameLength);
        if (const auto* hostSlot = findSlot(host, seat))
        {
            slot.side = hostSlot->side;
            slot.colour = hostSlot->colour;
            slot.team = hostSlot->team;
        }
        if (choice)
        {
            slot.side = choice->side == PlayerSide::Core ? "CORE" : "ARM";
            slot.colour = choice->colour.value;
            slot.team = choice->team.value_or(-1);
            slot.ready = choice->ready;
        }
        claim.slots.push_back(std::move(slot));

        const auto firstLine = chat.size() > LanDiscovery::maxChatLines ? chat.size() - LanDiscovery::maxChatLines : 0;
        claim.chat.assign(chat.begin() + static_cast<std::ptrdiff_t>(firstLine), chat.end());
        return claim;
    }

    ClaimState claimState(const DiscoveredGame& host, int seat, const std::string& token)
    {
        const auto* slot = findSlot(host, seat);
        if (slot == nullptr)
        {
            return ClaimState::Lost;
        }
        if (slot->controller == "network" && slot->personality == token)
        {
            return ClaimState::Accepted;
        }
        if (slot->controller == "open")
        {
            return ClaimState::Waiting;
        }
        return ClaimState::Lost;
    }

    // ------------------------------------------------------------------
    // The battleroom: who may touch what
    // ------------------------------------------------------------------

    SeatControls seatControls(RoomRole role, PlayerType type, bool ownSeat)
    {
        SeatControls controls;

        switch (role)
        {
            case RoomRole::None:
                break;

            case RoomRole::Host:
                switch (type)
                {
                    case PlayerType::Open:
                    case PlayerType::Network:
                        // Open it to a computer or back, or put a guest out.
                        controls.name = true;
                        break;
                    case PlayerType::Computer:
                        controls.name = true;
                        controls.side = true;
                        controls.colour = true;
                        controls.team = true;
                        break;
                    case PlayerType::Human:
                        // The host's own row: its choices, and nothing to
                        // click on the name.
                        if (ownSeat)
                        {
                            controls.side = true;
                            controls.colour = true;
                            controls.team = true;
                        }
                        break;
                }
                break;

            case RoomRole::Guest:
                if (ownSeat)
                {
                    controls.side = true;
                    controls.colour = true;
                    controls.team = true;
                    controls.ready = true;
                }
                break;
        }

        return controls;
    }

    std::optional<std::string> startBlocker(const MainMenuModel& model, const std::vector<bool>& ready, bool mapChosen)
    {
        if (!mapChosen)
        {
            return std::string("Select a map");
        }

        bool anyone = false;
        for (std::size_t i = 0; i < model.players.size(); ++i)
        {
            const auto& p = model.players[i];
            if (p.type.getValue() != PlayerType::Network)
            {
                continue;
            }
            anyone = true;
            if (i < ready.size() && ready[i])
            {
                continue;
            }
            const auto& name = p.name.getValue();
            return "Waiting for " + (name.empty() ? "player " + std::to_string(i + 1) : name) + " to be ready";
        }

        // A game of one person and computers is the skirmish screen's, and
        // starting it from here would only be refused (see
        // multiplayerSetupProblem) after the button had looked pressable.
        if (!anyone)
        {
            return std::string("Waiting for another player to join");
        }

        return std::nullopt;
    }

    bool colourHeldByAnotherSeat(const MainMenuModel& model, std::size_t seat, const PlayerColorIndex& colour)
    {
        for (std::size_t i = 0; i < model.players.size(); ++i)
        {
            if (i == seat)
            {
                continue;
            }
            const auto& p = model.players[i];
            if (p.type.getValue() != PlayerType::Open && p.colorIndex.getValue() == colour)
            {
                return true;
            }
        }
        return false;
    }

    void ensureFreeColour(MainMenuModel& model, std::size_t seat)
    {
        auto& p = model.players[seat];
        if (!colourHeldByAnotherSeat(model, seat, p.colorIndex.getValue()))
        {
            return;
        }
        for (unsigned int c = 0; c < 10; ++c)
        {
            if (!colourHeldByAnotherSeat(model, seat, PlayerColorIndex(c)))
            {
                p.colorIndex.next(PlayerColorIndex(c));
                return;
            }
        }
    }

    bool applyClaimChoice(MainMenuModel& model, std::size_t seat, const SeatClaim& claim)
    {
        if (seat >= model.players.size())
        {
            return false;
        }
        auto& p = model.players[seat];
        bool changed = false;

        const auto side = claim.side == "CORE" ? PlayerSide::Core : PlayerSide::Arm;
        if (p.side.getValue() != side)
        {
            p.side.next(side);
            changed = true;
        }

        // A number off the wire: anything outside the five teams is no team.
        std::optional<int> team;
        if (claim.team >= 0 && claim.team <= 4)
        {
            team = claim.team;
        }
        if (p.teamIndex.getValue() != team)
        {
            p.teamIndex.next(team);
            changed = true;
        }

        // A colour is taken if it is a colour and nobody in the game has it.
        // Refusing it here is the whole of the rule: the next announcement
        // says what the seat has, and the guest's screen follows that.
        if (claim.colour < 10)
        {
            const PlayerColorIndex wanted(claim.colour);
            if (p.colorIndex.getValue() != wanted && !colourHeldByAnotherSeat(model, seat, wanted))
            {
                p.colorIndex.next(wanted);
                changed = true;
            }
        }

        return changed;
    }

    std::optional<SeatMessage> parseSeatMessage(const std::string& name, std::size_t seatCount)
    {
        const std::string prefix = "PLAYER";
        if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
        {
            return std::nullopt;
        }

        std::size_t i = prefix.size();
        std::size_t seat = 0;
        std::size_t digits = 0;
        while (i < name.size() && name[i] >= '0' && name[i] <= '9' && digits < 3)
        {
            seat = (seat * 10) + static_cast<std::size_t>(name[i] - '0');
            ++i;
            ++digits;
        }
        if (digits == 0 || seat >= seatCount)
        {
            return std::nullopt;
        }

        if (i == name.size())
        {
            return SeatMessage{seat, std::string()};
        }
        if (name[i] != '_' || i + 1 >= name.size())
        {
            return std::nullopt;
        }
        return SeatMessage{seat, name.substr(i + 1)};
    }

    // ------------------------------------------------------------------
    // The battleroom: the options
    // ------------------------------------------------------------------

    namespace
    {
        /**
         * What each option's stage numbers mean, in the model's order (the
         * order optionsFromModel writes and the setup screen's buttons have).
         */
        const std::vector<std::string>* modelStageLabels(const std::string& optionName)
        {
            static const std::vector<std::pair<std::string, std::vector<std::string>>> table{
                {"commander", {"GAME ENDS", "CONTINUES"}},
                {"startloc", {"FIXED", "RANDOM"}},
                {"mapping", {"UNMAPPED", "MAPPED"}},
                {"los", {"PERMANENT", "TRUE", "CIRCULAR"}},
            };
            for (const auto& [name, labels] : table)
            {
                if (name == optionName)
                {
                    return &labels;
                }
            }
            return nullptr;
        }
    }

    std::optional<unsigned int> optionStageForLabel(const std::string& optionName, const std::string& label)
    {
        const auto* labels = modelStageLabels(optionName);
        if (labels == nullptr)
        {
            return std::nullopt;
        }
        const auto wanted = toUpper(trimmed(label));
        for (std::size_t i = 0; i < labels->size(); ++i)
        {
            if ((*labels)[i] == wanted)
            {
                return static_cast<unsigned int>(i);
            }
        }
        return std::nullopt;
    }

    std::optional<unsigned int> optionStageOfButtonStage(const std::string& optionName, const std::vector<std::string>& labels, unsigned int buttonStage)
    {
        if (buttonStage >= labels.size())
        {
            return std::nullopt;
        }
        return optionStageForLabel(optionName, labels[buttonStage]);
    }

    std::optional<unsigned int> buttonStageOfOptionStage(const std::string& optionName, const std::vector<std::string>& labels, unsigned int optionStage)
    {
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            if (optionStageForLabel(optionName, labels[i]) == optionStage)
            {
                return static_cast<unsigned int>(i);
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> usableOptionLabels(const std::string& optionName, const std::vector<std::string>& labels)
    {
        std::vector<std::string> usable;
        for (const auto& label : labels)
        {
            if (optionStageForLabel(optionName, label))
            {
                usable.push_back(label);
            }
        }
        return usable;
    }

    std::optional<unsigned int> nextOptionStage(const std::string& optionName, const std::vector<std::string>& labels, unsigned int currentOptionStage)
    {
        if (labels.empty())
        {
            return std::nullopt;
        }
        const auto here = buttonStageOfOptionStage(optionName, labels, currentOptionStage).value_or(0u);
        for (std::size_t step = 1; step <= labels.size(); ++step)
        {
            const auto candidate = static_cast<unsigned int>((here + step) % labels.size());
            if (auto stage = optionStageOfButtonStage(optionName, labels, candidate))
            {
                return stage;
            }
        }
        return std::nullopt;
    }

    const std::vector<BattleroomOption>& battleroomOptions()
    {
        static const std::vector<BattleroomOption> options{
            {"COMMANDER", "commander"},
            {"FIXEDLOC", "startloc"},
            {"MAPPING", "mapping"},
            {"LOSTYPE", "los"},
        };
        return options;
    }

    unsigned int resourceStopValue(unsigned int stop)
    {
        if (stop == 0)
        {
            return 200;
        }
        return 500 * std::min(stop, resourceStopCount - 1);
    }

    unsigned int resourceStopNearest(float value)
    {
        unsigned int best = 0;
        float bestDistance = std::abs(value - static_cast<float>(resourceStopValue(0)));
        for (unsigned int stop = 1; stop < resourceStopCount; ++stop)
        {
            const auto distance = std::abs(value - static_cast<float>(resourceStopValue(stop)));
            if (distance < bestDistance)
            {
                best = stop;
                bestDistance = distance;
            }
        }
        return best;
    }

    float resourceStopPercent(unsigned int stop)
    {
        return static_cast<float>(std::min(stop, resourceStopCount - 1)) / static_cast<float>(resourceStopCount - 1);
    }

    unsigned int resourceStopAtPercent(float percent)
    {
        const auto clamped = std::clamp(percent, 0.0f, 1.0f);
        return static_cast<unsigned int>(std::lround(clamped * static_cast<float>(resourceStopCount - 1)));
    }

    // ------------------------------------------------------------------
    // The battleroom: chat
    // ------------------------------------------------------------------

    namespace
    {
        /** Bytes of printable text only: nothing below a space, and not DEL. */
        std::string withoutControlCharacters(const std::string& text)
        {
            std::string out;
            out.reserve(text.size());
            for (char c : text)
            {
                const auto byte = static_cast<unsigned char>(c);
                if (byte >= 0x20 && byte != 0x7F)
                {
                    out.push_back(c);
                }
            }
            return out;
        }

        /** Cuts at `limit` bytes without leaving half of a multi-byte character. */
        std::string cutAtCharacter(const std::string& text, std::size_t limit)
        {
            if (text.size() <= limit)
            {
                return text;
            }
            std::size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
            {
                --end;
            }
            return text.substr(0, end);
        }
    }

    std::string sanitisedChatText(const std::string& text)
    {
        return trimmed(cutAtCharacter(trimmed(withoutControlCharacters(text)), LanDiscovery::maxChatTextLength));
    }

    bool ChatTranscript::say(const std::string& name, const std::string& text)
    {
        auto said = sanitisedChatText(text);
        if (said.empty() || lastSequence >= 2147483647u)
        {
            return false;
        }

        DiscoveredChatLine line;
        line.sequence = ++lastSequence;
        line.name = cutAtCharacter(withoutControlCharacters(name), maxPlayerNameLength);
        line.text = std::move(said);
        lines.push_back(std::move(line));

        if (lines.size() > maxKept)
        {
            lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(lines.size() - maxKept));
        }
        return true;
    }

    std::vector<DiscoveredChatLine> ChatTranscript::tail() const
    {
        const auto first = lines.size() > LanDiscovery::maxChatLines ? lines.size() - LanDiscovery::maxChatLines : 0;
        return std::vector<DiscoveredChatLine>(lines.begin() + static_cast<std::ptrdiff_t>(first), lines.end());
    }

    std::size_t ChatTranscript::merge(const std::vector<DiscoveredChatLine>& announced)
    {
        std::size_t added = 0;
        for (const auto& line : announced)
        {
            // Only forwards: a line at or before the newest one held is one
            // already shown, and a sequence that goes back is somebody's
            // garbage.
            if (line.sequence <= lastSequence)
            {
                continue;
            }
            lastSequence = line.sequence;
            lines.push_back(line);
            ++added;
        }
        if (lines.size() > maxKept)
        {
            lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(lines.size() - maxKept));
        }
        return added;
    }

    std::size_t takeClaimChat(ChatTranscript& room, unsigned int& lastTaken, const std::string& speaker, const std::vector<DiscoveredChatLine>& claimed)
    {
        auto ordered = claimed;
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.sequence < b.sequence; });

        std::size_t said = 0;
        for (const auto& line : ordered)
        {
            if (line.sequence == 0 || line.sequence <= lastTaken)
            {
                continue;
            }
            // Taken even when it says nothing once cleaned: the same line
            // will be in the next announcement and must not be tried again.
            lastTaken = line.sequence;
            // The speaker is the seat's, whatever name the claim put on the
            // line, so that nobody speaks for somebody else.
            if (room.say(speaker, line.text))
            {
                ++said;
            }
        }
        return said;
    }

    std::string chatLineText(const DiscoveredChatLine& line)
    {
        return line.name.empty() ? line.text : line.name + ": " + line.text;
    }

    std::vector<std::string> wrapChatLine(const std::string& text, float maxWidth, const std::function<float(const std::string&)>& width)
    {
        static const std::string indent = "  ";

        std::vector<std::string> rows;
        std::string current;

        auto breakLongRow = [&]() {
            // A row that is still too wide holds a word wider than the box:
            // cut it where it must be, keeping at least one character a row.
            while (current.size() > indent.size() + 1 && width(current) > maxWidth)
            {
                std::size_t fit = indent.size() + 1;
                while (fit < current.size() && width(current.substr(0, fit + 1)) <= maxWidth)
                {
                    ++fit;
                }
                rows.push_back(current.substr(0, fit));
                current = indent + current.substr(fit);
            }
        };

        std::size_t pos = 0;
        while (pos < text.size())
        {
            auto space = text.find(' ', pos);
            const auto end = space == std::string::npos ? text.size() : space;
            const auto word = text.substr(pos, end - pos);
            pos = end + 1;
            if (word.empty())
            {
                continue;
            }

            const auto candidate = current.empty() ? word : current + " " + word;
            if (current.empty() || width(candidate) <= maxWidth)
            {
                current = candidate;
            }
            else
            {
                rows.push_back(current);
                current = indent + word;
            }
            breakLongRow();
        }

        if (!current.empty())
        {
            rows.push_back(current);
        }
        return rows;
    }

    // ------------------------------------------------------------------
    // Addresses, rows, passwords
    // ------------------------------------------------------------------

    std::string hostPartOf(const std::string& sourceAddress)
    {
        if (sourceAddress.empty())
        {
            return sourceAddress;
        }

        if (sourceAddress.front() == '[')
        {
            auto close = sourceAddress.find(']');
            return close == std::string::npos ? sourceAddress : sourceAddress.substr(0, close + 1);
        }

        // One colon is host:port; none, or several, is a bare address.
        if (std::count(sourceAddress.begin(), sourceAddress.end(), ':') != 1)
        {
            return sourceAddress;
        }
        return sourceAddress.substr(0, sourceAddress.find(':'));
    }

    std::string gameAddress(const std::string& sourceAddress, unsigned int port)
    {
        return hostPartOf(sourceAddress) + ":" + std::to_string(port);
    }

    bool hasOpenSeat(const DiscoveredGame& game)
    {
        return std::any_of(game.slots.begin(), game.slots.end(), [](const auto& s) { return s.controller == "open"; });
    }

    GameListRow gameListRow(const DiscoveredGame& game)
    {
        GameListRow row;

        if (!game.label.empty())
        {
            row.gameName = game.label;
        }
        else if (!game.hostName.empty())
        {
            row.gameName = game.hostName + "'s game";
        }
        else
        {
            row.gameName = game.map;
        }
        row.mapName = game.map;

        // The column headed Cmdr: whether a dead commander ends the game.
        // The same four columns are the option stages the host announced; a
        // host that did not say leaves its column empty rather than guessed.
        if (const auto* v = findOption(game.options, "commander"))
        {
            row.commander = (*v == "0") ? "Yes" : "No";
        }
        row.ping = "-";
        if (const auto* v = findOption(game.options, "los"))
        {
            // Three letters, which is what the 27-pixel column holds.
            row.lineOfSight = (*v == "0") ? "Prm" : ((*v == "1") ? "Tru" : "Cir");
        }
        if (const auto* v = findOption(game.options, "mapping"))
        {
            row.mapping = (*v == "1") ? "Yes" : "No";
        }

        // The host's own seat is the one whose amounts a joiner is shown;
        // every seat can differ, and the list has room for one number.
        const DiscoveredSlot* shown = nullptr;
        for (const auto& slot : game.slots)
        {
            if (slot.controller == "human")
            {
                shown = &slot;
                break;
            }
        }
        if (shown == nullptr && !game.slots.empty())
        {
            shown = &game.slots.front();
        }
        if (shown != nullptr)
        {
            row.metal = std::to_string(shown->metal);
            row.energy = std::to_string(shown->energy);
        }

        // People over seats a person could sit in: a computer player takes
        // a seat and is not a player the game is waiting for.
        std::size_t people = 0;
        std::size_t places = 0;
        for (const auto& slot : game.slots)
        {
            if (slot.controller == "human" || slot.controller == "network")
            {
                ++people;
            }
            if (slot.controller != "computer")
            {
                ++places;
            }
        }
        row.players = std::to_string(people) + "/" + std::to_string(places);

        // The 40-pixel column holds four letters: "Play" is the original's
        // "tells if game is in play".
        row.status = game.launching ? "Play" : (hasOpenSeat(game) ? "Open" : "Full");
        return row;
    }

    bool passwordAccepted(const DiscoveredGame& game, const std::string& typed)
    {
        if (game.passwordHash.empty())
        {
            return true;
        }
        return LanDiscovery::hashPassword(game.session, typed) == game.passwordHash;
    }

    std::string trimmed(const std::string& text)
    {
        auto first = text.find_first_not_of(' ');
        if (first == std::string::npos)
        {
            return std::string();
        }
        auto last = text.find_last_not_of(' ');
        return text.substr(first, last - first + 1);
    }

    // ------------------------------------------------------------------
    // Keeping a visit from leaking into the skirmish screen
    // ------------------------------------------------------------------

    ModelSnapshot snapshotModel(const MainMenuModel& model)
    {
        ModelSnapshot snapshot;
        for (const auto& p : model.players)
        {
            snapshot.seats.push_back(ModelSnapshot::Seat{
                p.type.getValue(),
                p.side.getValue(),
                p.colorIndex.getValue(),
                p.teamIndex.getValue(),
                p.metal.getValue(),
                p.energy.getValue(),
                p.personality.getValue()});
        }
        snapshot.options = optionsFromModel(model);
        return snapshot;
    }

    void restoreModel(MainMenuModel& model, const ModelSnapshot& snapshot)
    {
        applyOptionsToModel(model, snapshot.options);

        for (std::size_t i = 0; i < model.players.size() && i < snapshot.seats.size(); ++i)
        {
            auto& p = model.players[i];
            const auto& s = snapshot.seats[i];
            p.name.next(std::string());
            p.networkAddress.next(std::string());
            p.side.next(s.side);
            p.colorIndex.next(s.color);
            p.teamIndex.next(s.team);
            p.metal.next(s.metal);
            p.energy.next(s.energy);
            p.personality.next(s.personality);
            p.type.next(s.type);
        }
    }

    // ------------------------------------------------------------------
    // The four panels
    // ------------------------------------------------------------------

    const std::vector<std::string>& gameListColumnNames()
    {
        static const std::vector<std::string> names{
            "GAMENAME", "MAPNAME", "COMMANDER", "PING", "LOS", "FULLMAP", "METAL", "ENERGY", "PLAYERS", "STATUS"};
        return names;
    }

    namespace
    {
        std::string GameListRow::*columnMember(std::size_t column)
        {
            static std::string GameListRow::*const members[] = {
                &GameListRow::gameName,
                &GameListRow::mapName,
                &GameListRow::commander,
                &GameListRow::ping,
                &GameListRow::lineOfSight,
                &GameListRow::mapping,
                &GameListRow::metal,
                &GameListRow::energy,
                &GameListRow::players,
                &GameListRow::status};
            return members[column];
        }

        std::vector<UiListBox*> gameListBoxes(UiPanel& panel)
        {
            std::vector<UiListBox*> boxes;
            for (const auto& name : gameListColumnNames())
            {
                if (auto box = panel.find<UiListBox>(name))
                {
                    boxes.push_back(&box->get());
                }
            }
            return boxes;
        }

        /**
         * Each box sets every other box's selection to its own. The columns
         * are separate gadgets in the GUI file and the original ties them
         * through their shared group; here the tie is made by hand, and the
         * subscriptions live in the boxes so they are handed back while the
         * subjects they listen to are still alive.
         */
        void linkSelections(const std::vector<UiListBox*>& boxes)
        {
            for (auto* box : boxes)
            {
                auto sub = box->selectedIndex().subscribe([box, boxes](const std::optional<unsigned int>& index) {
                    for (auto* other : boxes)
                    {
                        if (other != box)
                        {
                            other->setSelectedIndex(index);
                        }
                    }
                });
                box->addSubscription(std::move(sub));
            }
        }

        /** A field the bitmap already has a plate for: just the text and caret over it. */
        void useBakedPlate(UiPanel& panel, const char* name)
        {
            if (auto box = panel.find<UiTextBox>(name))
            {
                box->get().setDrawPlate(false);
            }
        }
    }

    void fillGameList(UiPanel& panel, const std::vector<GameListRow>& rows)
    {
        std::size_t column = 0;
        for (const auto& name : gameListColumnNames())
        {
            if (auto box = panel.find<UiListBox>(name))
            {
                box->get().clearItems();
                for (const auto& row : rows)
                {
                    box->get().appendItem(row.*columnMember(column));
                }
            }
            ++column;
        }
    }

    std::optional<unsigned int> selectedGameRow(UiPanel& panel)
    {
        for (auto* box : gameListBoxes(panel))
        {
            if (auto index = box->getSelectedIndex())
            {
                return index;
            }
        }
        return std::nullopt;
    }

    void selectGameRow(UiPanel& panel, const std::optional<unsigned int>& row)
    {
        // One is enough: the boxes are linked.
        auto boxes = gameListBoxes(panel);
        if (!boxes.empty())
        {
            boxes.front()->setSelectedIndex(row);
        }
    }

    std::unique_ptr<UiPanel> makeSelectConnectionPanel(UiFactory& factory)
    {
        auto panel = factory.panelFromGuiFile("SELPROV", "selconnect2");

        // SERVICEX is the template the original copies once per connection
        // service it finds (it is `active=0` in the file), and SETTINGS is
        // the service's own options, of which a UDP address has none. A
        // button that did nothing is worse than no button.
        panel->removeChildrenNamed("SERVICEX");
        panel->removeChildrenNamed("SETTINGS");

        // The only connection RWE has: it speaks UDP to an address, and has
        // no modem, serial or games-service provider.
        if (auto list = panel->find<UiListBox>("DPLAY"))
        {
            list->get().appendItem("Internet TCP/IP Connection");
            list->get().setSelectedIndex(0u);
        }

        // SELPROV.GUI names no crdefault, so Enter has no button; Select is
        // the one anybody means.
        panel->setDefaultButtons(panel->getCrDefault().empty() ? "SELECT" : panel->getCrDefault(), panel->getEscDefault());
        panel->setDefaultKeysEnabled(true);
        return panel;
    }

    std::unique_ptr<UiPanel> makeTcpAddressPanel(UiFactory& factory)
    {
        auto panel = factory.panelFromGuiFile("TCP");

        // TCP.GUI says `crdefault=HOST`, a button of the original's shared
        // address dialog that this one does not have. OK is the one that
        // means Enter here.
        panel->setDefaultButtons("OK", panel->getEscDefault().empty() ? "PREV" : panel->getEscDefault());
        panel->setDefaultKeysEnabled(true);

        // The prompt is the file's own sentence at the file's own place, but
        // 276 pixels is the width the original's font needed and this one
        // is wider: at that width the last word wrapped down onto the
        // address box. It is put back at a width that holds it. That runs
        // under the OK button's column, but not over it: the buttons are
        // fifty pixels further down.
        {
            panel->removeChildrenNamed("TEXT");
            auto prompt = factory.createLabel(51, 39, 400, 17, "Enter TCP address (leave blank to search)", UiLabel::Alignment::Left);
            prompt->setName("TEXT");
            panel->appendChild(std::move(prompt));
        }

        // The original gives no hint about the port. A typed address is
        // probed on the discovery port the other machine started on.
        auto hint = factory.createLabel(
            53,
            92,
            285,
            30,
            "An address without a port uses the discovery port, 1338.",
            UiLabel::Alignment::Left);
        panel->appendChild(std::move(hint));
        return panel;
    }

    std::unique_ptr<UiPanel> makeSelectGamePanel(UiFactory& factory)
    {
        auto panel = factory.panelFromGuiFile("SELGAME", "selectgame2x");

        // Spectating needs a join RWE does not have.
        panel->removeChildrenNamed("WATCH");

        if (auto nickname = panel->find<UiTextBox>("NICKNAME"))
        {
            // The file says 63; a name this long does not fit on the wire,
            // where a host's name is bounded to twenty bytes, so the box
            // stops where an announcement would.
            nickname->get().setMaxLength(static_cast<unsigned int>(maxPlayerNameLength));
        }
        if (auto password = panel->find<UiTextBox>("PASSWORD"))
        {
            // The password typed here is for joining a game that has one.
            password->get().setMasked(true);
        }
        useBakedPlate(*panel, "NICKNAME");
        useBakedPlate(*panel, "PASSWORD");

        linkSelections(gameListBoxes(*panel));

        panel->setDefaultKeysEnabled(true);
        return panel;
    }

    std::unique_ptr<UiPanel> makeNewGamePanel(UiFactory& factory)
    {
        auto panel = factory.panelFromGuiFile("NEWMULTI", "createnew");

        if (auto password = panel->find<UiTextBox>("PASSWORD"))
        {
            // NEWMULTI.GUI's two commonattribs differ by one bit between
            // NICKNAME (120) and PASSWORD (121), and the same bit is set on
            // both of SELGAME's boxes, which cannot both be masked -- so it
            // is not a mask bit and the mask is set by name.
            password->get().setMasked(true);
        }
        useBakedPlate(*panel, "GAMENAME");
        useBakedPlate(*panel, "NICKNAME");
        useBakedPlate(*panel, "PASSWORD");

        panel->setDefaultKeysEnabled(true);
        return panel;
    }

    std::unique_ptr<UiPanel> makeBattleroomPanel(UiFactory& factory, TextureService& textures, RoomRole role, BattleroomArt& art)
    {
        // Two layouts of one file. The original LOUNGE2.GUI has an Ally
        // column over battleroom2.pcx; the 3.1 patch moves everything over,
        // gives the team shapes a column of their own (the TEAMICONSx
        // gadget, with its own GAF entry) and brings battleroom.pcx to match.
        // Either picture under the other's gadgets puts every control a
        // finger-width from where it is drawn, so the file's own content
        // decides which.
        const auto entries = factory.readGui("LOUNGE2");
        const bool patched = std::any_of(entries.begin(), entries.end(), [](const GuiEntry& g) { return g.common.name == "TEAMICONSx"; });
        const std::string background = patched && factory.bitmapExists("battleroom") ? "battleroom" : "battleroom2";
        auto panel = factory.panelFromGuiFile("LOUNGE2", background, entries);
        const std::string teamTemplate = patched ? "TEAMICONSx" : "ALLYx";

        // What the file draws that this engine has no meaning for. Each of
        // these is a setting of TotalA's that RWE does not have -- a gadget
        // that did nothing is worse than none, and one that changed a value
        // only this end held would be a desync on the first tick.
        //
        //   CHEATING, WATCHING, GAMEOPEN   cheat codes, spectators and a
        //                                  game that closes to joiners: RWE
        //                                  has no cheat codes, no spectator
        //                                  join, and a started game is
        //                                  already closed.
        //   MAXUNITS, MAXUNITSTEXT         a unit cap, which the simulation
        //                                  does not take as a setting.
        //   RESTRICTIONS                   the build-restriction editor.
        //   RESx, MEMx, CDx                a seat's screen resolution, its
        //                                  memory and its CD: nothing is
        //                                  known about another machine's.
        //                                  The Resolution column is left
        //                                  empty rather than given to an AI
        //                                  personality picker: RWE has none
        //                                  on any screen.
        //   SYNCHING, battlestart          the "Synching" overlay, which
        //                                  covers the Start button while the
        //                                  units are checked against each
        //                                  other's -- RWE has no such step.
        //
        // and the captions of the ones above that have one.
        const auto template_of = [&](const char* name) {
            struct Geometry
            {
                int x{0};
                int y{0};
                unsigned int width{0};
                unsigned int height{0};
            } g;
            if (auto c = panel->find<UiComponent>(name))
            {
                g = {c->get().getX(), c->get().getY(), c->get().getWidth(), c->get().getHeight()};
            }
            return g;
        };
        const auto nameBox = template_of("PLAYERx");
        const auto sideBox = template_of("SIDEx");
        const auto logoBox = template_of("LOGOx");
        const auto teamBox = template_of(teamTemplate.c_str());
        const auto readyBox = template_of("READYx");
        const auto pingBox = template_of("PINGx");

        for (const char* name : {"CHEATING", "WATCHING", "GAMEOPEN", "MAXUNITS", "MAXUNITSTEXT", "RESTRICTIONS", "RESx", "MEMx", "CDx", "SYNCHING", "battlestart"})
        {
            panel->removeChildrenNamed(name);
        }

        // The templates are cloned once per seat and not themselves seats.
        //
        // The patched file's ALLYx is the original's alliances column, which
        // RWE has no counterpart of: alliances here are the teams, and those
        // are the TEAMICONSx column.
        for (const char* name : {"PLAYERx", "SIDEx", "LOGOx", "ALLYx", "TEAMICONSx", "READYx", "PINGx"})
        {
            panel->removeChildrenNamed(name);
        }

        // Every caption of the right-hand column is a label named TEXT, so
        // they are told apart by what they say.
        {
            auto& children = panel->getChildren();
            children.erase(
                std::remove_if(children.begin(), children.end(), [](const auto& c) {
                    auto* label = dynamic_cast<UiLabel*>(c.get());
                    if (label == nullptr || label->getName() != "TEXT")
                    {
                        return false;
                    }
                    const auto& text = label->getText();
                    return text == "Cheat Codes" || text == "Watching" || text == "Game Status" || text == "Max Units";
                }),
                children.end());
        }

        // -- the art the rows draw from --
        art.logos.clear();
        if (auto logos = textures.tryGetGafEntry("anims/LOGOS.GAF", "32xlogos"))
        {
            // The sheet's logos are 32 pixels square and a row has sixteen.
            for (const auto& sprite : (*logos)->sprites)
            {
                const auto bounds = Rectangle2f::fromTopLeft(0.0f, 0.0f, 16.0f, 16.0f);
                art.logos.push_back(std::make_shared<Sprite>(bounds, sprite->texture, sprite->mesh));
            }
        }
        art.allyIcons.clear();
        {
            // The patched LOUNGE2.GAF carries the shapes under the gadget's
            // own name; before the patch the skirmish screen's are the same
            // twelve faces.
            auto icons = textures.getGuiTexture("LOUNGE2", "TEAMICONSx");
            if (!icons)
            {
                icons = textures.getGuiTexture("SKIRMISH", "TEAMICONSx");
            }
            if (!icons)
            {
                icons = textures.getGuiTexture("SKIRMISH", "ally icons");
            }
            if (icons)
            {
                art.allyIcons = (*icons)->sprites;
            }
        }

        auto lampFaces = textures.getGuiTexture("LOUNGE2", "CHECKBOX");
        auto lampFont = textures.getGafEntry("anims/hattfont12.gaf", "Haettenschweiler (120)");

        // -- the seat rows --
        for (int i = 0; i < 10; ++i)
        {
            const auto seatName = "PLAYER" + std::to_string(i);
            const int rowY = nameBox.y + (i * battleroomRowPitch);
            // The other templates sit a little off the name's own top, which
            // is the file's way of centring a 16 pixel gadget on a 20 pixel row.
            const auto rowOffset = [&](int templateY) { return rowY + (templateY - nameBox.y); };

            // The gadgets of a row are drawn as pictures and say what is
            // not allowed by not answering, not by greying out: a greyed
            // frame would hide the one thing the other players' rows are
            // there to show.
            {
                auto button = factory.createButton(
                    nameBox.x, rowY, static_cast<int>(nameBox.width), static_cast<int>(nameBox.height), "LOUNGE2", "PLAYERx", "Open");
                button->setName(seatName);
                button->setTextAlign(UiStagedButton::TextAlign::Center);
                button->setDisabledSprite(nullptr);
                panel->appendChild(std::move(button));
            }
            {
                auto button = factory.createStagedButton(
                    sideBox.x, rowOffset(sideBox.y), static_cast<int>(sideBox.width), static_cast<int>(sideBox.height),
                    "LOUNGE2", "SIDEx", std::vector<std::string>(2), 2);
                button->setName(seatName + "_side");
                button->setDisabledSprite(nullptr);
                panel->appendChild(std::move(button));
            }
            {
                auto button = factory.createButton(
                    logoBox.x, rowOffset(logoBox.y), static_cast<int>(logoBox.width), static_cast<int>(logoBox.height),
                    "LOUNGE2", "LOGOx", "");
                button->setName(seatName + "_color");
                button->setDisabledSprite(nullptr);
                panel->appendChild(std::move(button));
            }
            {
                // "team" is a name no face is filed under, so the button
                // starts blank and takes its picture from the ally icons.
                auto button = factory.createButton(
                    teamBox.x, rowOffset(teamBox.y), static_cast<int>(teamBox.width), static_cast<int>(teamBox.height),
                    "LOUNGE2", "team", "");
                button->setName(seatName + "_team");
                button->setDisabledSprite(nullptr);
                panel->appendChild(std::move(button));
            }
            {
                // READYx is a 16 pixel gadget the file gives no picture:
                // the lamp is commongui's CHECKBOX -- dark, lit -- which is
                // what a status box of that size is for.
                const auto defaultSprite = textures.getDefaultSprite();
                const auto off = lampFaces && (*lampFaces)->sprites.size() > 0 ? (*lampFaces)->sprites[0] : defaultSprite;
                const auto on = lampFaces && (*lampFaces)->sprites.size() > 1 ? (*lampFaces)->sprites[1] : defaultSprite;
                const auto pressed = lampFaces && (*lampFaces)->sprites.size() > 2 ? (*lampFaces)->sprites[2] : defaultSprite;
                std::vector<UiStagedButton::StageInfo> stages;
                stages.emplace_back(off, "");
                stages.emplace_back(on, "");
                auto button = std::make_unique<UiStagedButton>(
                    readyBox.x, rowOffset(readyBox.y), readyBox.width, readyBox.height, std::move(stages), pressed, lampFont);
                button->setName(seatName + "_ready");
                panel->appendChild(std::move(button));
            }
            {
                auto label = factory.createLabel(
                    pingBox.x, rowOffset(pingBox.y), static_cast<int>(pingBox.width), static_cast<int>(pingBox.height), "-", UiLabel::Alignment::Left);
                label->setName(seatName + "_ping");
                panel->appendChild(std::move(label));
            }
        }

        // -- the options --
        // A button may carry stages this build means nothing by (the
        // commander's third, Deathmatch). It is built again without them, at
        // the place the file gave it, so that a click can never land on one.
        for (const auto& option : battleroomOptions())
        {
            auto button = panel->find<UiStagedButton>(option.button);
            if (!button)
            {
                continue;
            }
            std::vector<std::string> labels;
            for (unsigned int stage = 0; stage < button->get().getStageCount(); ++stage)
            {
                labels.push_back(button->get().getStageLabel(stage));
            }
            auto usable = usableOptionLabels(option.option, labels);
            if (!usable.empty() && usable.size() != labels.size())
            {
                factory.replaceStagedButton(*panel, "LOUNGE2", option.button, option.button, usable, 0);
            }
        }

        // -- who sees a Start button --
        if (role != RoomRole::Host)
        {
            // Only the host starts a game. A guest's button would be one it
            // could press and that would do nothing, or worse.
            panel->removeChildrenNamed("START");
        }

        // -- typing --
        if (auto message = panel->find<UiTextBox>("MESSAGE"))
        {
            // The bitmap has the line's plate painted into it.
            message->get().setDrawPlate(false);
            message->get().setMaxLength(UiTextBox::largestMaxLength);
        }

        // Enter sends the line. The file names no default button, and the
        // panel only knows how to press a button; this one is off the screen
        // and exists to be pressed.
        {
            auto send = factory.createSizedButton(-100, -100, 1, 1, "LOUNGE2", "");
            send->setName("SEND");
            panel->appendChild(std::move(send));
        }
        panel->setDefaultButtons("SEND", std::string());
        panel->setDefaultKeysEnabled(true);
        panel->setFocusByName("MESSAGE");

        return panel;
    }

    // ------------------------------------------------------------------
    // The scene's half
    // ------------------------------------------------------------------

    namespace
    {
        float secondsSince(const Timestamp& then)
        {
            return std::chrono::duration<float>(getTimestamp() - then).count();
        }

        /**
         * Random hex, for session ids and claim tokens. Display and wire
         * only: nothing in the simulation reads it, so the generator being
         * seeded from the machine is the point.
         */
        std::string randomHex(std::size_t digits)
        {
            static std::mt19937_64 generator{std::random_device{}()};
            static constexpr char alphabet[] = "0123456789abcdef";
            std::string out;
            for (std::size_t i = 0; i < digits; ++i)
            {
                out.push_back(alphabet[generator() % 16]);
            }
            return out;
        }

        /** The seat's caption: the person's name if it has one, clipped to the button. */
        std::string seatCaption(PlayerType type, const std::string& name)
        {
            switch (type)
            {
                case PlayerType::Open:
                    return "Open";
                case PlayerType::Computer:
                    return "Computer";
                case PlayerType::Human:
                    return name.empty() ? "Player" : name.substr(0, 14);
                case PlayerType::Network:
                    return name.empty() ? "Network" : name.substr(0, 14);
            }
            return std::string();
        }

        UiLabel* helpLabel(UiPanel& panel)
        {
            auto label = panel.find<UiLabel>("HELPTEXT");
            return label ? &label->get() : nullptr;
        }
    }

    namespace
    {
        RoomRole roomRoleOf(MultiplayerFlow::Role role)
        {
            switch (role)
            {
                case MultiplayerFlow::Role::Host:
                    return RoomRole::Host;
                case MultiplayerFlow::Role::Guest:
                    return RoomRole::Guest;
                case MultiplayerFlow::Role::Browsing:
                    break;
            }
            return RoomRole::None;
        }

        /** Whether seat `i` is the one this machine plays. */
        bool isOwnSeat(const MultiplayerFlow& flow, std::size_t i, PlayerType type)
        {
            if (flow.role == MultiplayerFlow::Role::Host)
            {
                // The host's own seat is the only one that is a Human.
                return type == PlayerType::Human;
            }
            return static_cast<int>(i) == flow.mySeat;
        }

        /** What the room has that the model has not, forgotten: for a new visit to a room. */
        void resetRoom(MultiplayerFlow& flow, std::size_t seats)
        {
            flow.chat = ChatTranscript{};
            flow.shownChatSequence = 0;
            flow.seatReady.assign(seats, false);
            flow.claimMemory.clear();
            flow.removedTokens.clear();
            flow.wanted = GuestChoice{};
            flow.choicePending = false;
            flow.myReady = false;
            flow.sentChat.clear();
            flow.nextChatSequence = 1;
        }

        /** The width of text in a font, as the renderer will lay it out. */
        float textWidthIn(const SpriteSeries& font, const std::string& text)
        {
            const auto valid = ensureUtf8(text);
            float width = 0.0f;
            auto it = utf8Begin(valid);
            const auto end = utf8End(valid);
            for (; it != end; ++it)
            {
                auto ch = *it;
                if (ch >= font.sprites.size())
                {
                    ch = 0;
                }
                width += font.sprites[ch]->bounds.right();
            }
            return width;
        }
    }

    MainMenuScene::~MainMenuScene() = default;

    bool MainMenuScene::isMultiplayerGuest() const
    {
        return multiplayerFlow && multiplayerFlow->role == MultiplayerFlow::Role::Guest;
    }

    bool MainMenuScene::isMultiplayerHost() const
    {
        return multiplayerFlow && multiplayerFlow->role == MultiplayerFlow::Role::Host;
    }

    std::string MainMenuScene::captionForSeat(int playerIndex) const
    {
        const auto& p = model.players[static_cast<std::size_t>(playerIndex)];
        return seatCaption(p.type.getValue(), p.name.getValue());
    }

    void MainMenuScene::goToMultiplayerMenu()
    {
        goToSelectConnectionMenu();
    }

    void MainMenuScene::goToSelectConnectionMenu()
    {
        multiplayerFlow = std::make_unique<MultiplayerFlow>();
        multiplayerFlow->snapshot = snapshotModel(model);

        auto panel = makeSelectConnectionPanel(uiFactory);

        // Double-click on the connection is Select. Deferred, like every
        // menu action: it opens a dialog from inside the list's own
        // mouse handler.
        if (auto list = panel->find<UiListBox>("DPLAY"))
        {
            auto sub = list->get().itemActivated().subscribe([this](unsigned int) {
                pendingMenuActions.push_back([this]() {
                    if (multiplayerFlow && dialogStack.empty() && panelStack.back()->getName() == "SELPROV")
                    {
                        openTcpAddressDialog();
                    }
                });
            });
            list->get().addSubscription(std::move(sub));
        }

        goToMenu(std::move(panel));
    }

    void MainMenuScene::openTcpAddressDialog()
    {
        auto panel = makeTcpAddressPanel(uiFactory);
        if (multiplayerFlow)
        {
            if (auto box = panel->find<UiTextBox>("ADDRESS"))
            {
                box->get().setText(multiplayerFlow->address);
            }
        }
        openDialog(std::move(panel));
    }

    void MainMenuScene::confirmTcpAddress()
    {
        if (!multiplayerFlow || dialogStack.empty() || dialogStack.back()->getName() != "TCP")
        {
            return;
        }

        std::string text;
        if (auto box = dialogStack.back()->find<UiTextBox>("ADDRESS"))
        {
            text = trimmed(box->get().getText());
        }

        // The original's own sentence. Said here, with the dialog still up,
        // rather than a second later as a list that never fills.
        if (!text.empty() && !LanDiscovery::isProbeAddress(text))
        {
            openMessageBox("Invalid TCP/IP Address");
            return;
        }

        multiplayerFlow->address = text;
        dialogStack.pop_back();
        openGameList();
    }

    void MainMenuScene::openGameList()
    {
        auto& flow = *multiplayerFlow;
        flow.role = MultiplayerFlow::Role::Browsing;
        flow.games.clear();
        flow.rows.clear();
        flow.listOpenedAt = getTimestamp();
        flow.lastProbeAt = Timestamp{};

        if (!flow.discoveryRunning)
        {
            flow.discovery.start(LanDiscovery::defaultPort);
            flow.discoveryRunning = flow.discovery.isRunning();
            if (!flow.discoveryRunning)
            {
                // Nothing fatal: a game can still be started by typing
                // addresses on the setup screen. One more machine holding
                // port 1338 is enough to land here.
                LOG_INFO << "LAN discovery did not start; the game list will be empty";
            }
        }

        auto panel = makeSelectGamePanel(uiFactory);

        if (auto box = panel->find<UiTextBox>("NICKNAME"))
        {
            box->get().setText(rememberedNickname);
        }
        // The file focuses New. Typing a name is what the screen is for, so
        // an empty name box takes the focus instead; Enter still means New.
        if (rememberedNickname.empty())
        {
            panel->setFocusByName("NICKNAME");
        }

        // Double-click on a game is Join.
        for (const auto& columnName : gameListColumnNames())
        {
            if (auto list = panel->find<UiListBox>(columnName))
            {
                auto sub = list->get().itemActivated().subscribe([this](unsigned int) {
                    pendingMenuActions.push_back([this]() {
                        if (multiplayerFlow && dialogStack.empty() && panelStack.back()->getName() == "SELGAME")
                        {
                            joinSelectedGame();
                        }
                    });
                });
                list->get().addSubscription(std::move(sub));
            }
        }

        goToMenu(std::move(panel));
    }

    void MainMenuScene::updateGameList()
    {
        auto& flow = *multiplayerFlow;
        auto& panel = *panelStack.back();

        // A typed address is asked once a second. The answer arrives as an
        // ordinary announcement and is listed with the ones heard by
        // broadcast: a machine with two interfaces answers from the one it
        // routes by, which need not be the address typed, so the list is not
        // narrowed to it.
        if (flow.discoveryRunning && !flow.address.empty() && secondsSince(flow.lastProbeAt) >= 1.0f)
        {
            flow.discovery.probe(flow.address);
            flow.lastProbeAt = getTimestamp();
        }

        std::vector<DiscoveredGame> found;
        if (flow.discoveryRunning)
        {
            for (auto& g : flow.discovery.games())
            {
                if (!isClaimSession(g.session))
                {
                    found.push_back(std::move(g));
                }
            }
        }

        std::vector<GameListRow> rows;
        for (const auto& g : found)
        {
            rows.push_back(gameListRow(g));
        }

        // The same text from a different session is a different game: what
        // Join acts on is the session, so it has to be kept current even
        // when the rows are not.
        const bool sameSessions = found.size() == flow.games.size()
            && std::equal(found.begin(), found.end(), flow.games.begin(), [](const auto& a, const auto& b) { return a.session == b.session; });

        if (rows != flow.rows || !sameSessions)
        {
            // Keep the selection on the same game when others come and go.
            std::string selectedSession;
            if (auto row = selectedGameRow(panel); row && *row < flow.games.size())
            {
                selectedSession = flow.games[*row].session;
            }

            flow.games = std::move(found);
            flow.rows = std::move(rows);
            fillGameList(panel, flow.rows);

            if (!selectedSession.empty())
            {
                for (std::size_t i = 0; i < flow.games.size(); ++i)
                {
                    if (flow.games[i].session == selectedSession)
                    {
                        selectGameRow(panel, static_cast<unsigned int>(i));
                        break;
                    }
                }
            }
        }
        else
        {
            flow.games = std::move(found);
        }

        if (auto* help = helpLabel(panel))
        {
            std::string text;
            if (auto row = selectedGameRow(panel); row && *row < flow.games.size())
            {
                const auto& g = flow.games[*row];
                text = g.hostName.empty() ? std::string("Hosted at ") + g.sourceAddress : "Hosted by " + g.hostName;
                if (!g.passwordHash.empty())
                {
                    text += ". Enter its password to join.";
                }
            }
            else if (!flow.discoveryRunning)
            {
                text = "Could not listen on this network. Press New to host a game.";
            }
            else if (flow.games.empty())
            {
                if (secondsSince(flow.listOpenedAt) < 2.0f)
                {
                    text = "Connecting (ESC to abort)";
                }
                else if (flow.address.empty())
                {
                    text = "No games found on this network. Press New to host one.";
                }
                else
                {
                    text = "No game found at " + flow.address + ". Press New to host one.";
                }
            }
            else
            {
                text = "Select a game and press Join.";
            }
            help->setText(text);
        }
    }

    void MainMenuScene::joinSelectedGame()
    {
        if (!multiplayerFlow || panelStack.back()->getName() != "SELGAME")
        {
            return;
        }
        auto& flow = *multiplayerFlow;
        auto& panel = *panelStack.back();

        std::string nickname;
        if (auto box = panel.find<UiTextBox>("NICKNAME"))
        {
            nickname = trimmed(box->get().getText());
        }
        if (nickname.empty())
        {
            openMessageBox("You must enter your name");
            return;
        }

        auto row = selectedGameRow(panel);
        if (!row || *row >= flow.games.size())
        {
            openMessageBox("Select a game to join");
            return;
        }
        const auto game = flow.games[*row];

        if (game.launching)
        {
            openMessageBox("That game has already started");
            return;
        }

        std::string typedPassword;
        if (auto box = panel.find<UiTextBox>("PASSWORD"))
        {
            typedPassword = box->get().getText();
        }
        if (!passwordAccepted(game, typedPassword))
        {
            openMessageBox("Incorrect password");
            return;
        }

        // A guest on a map it does not have would start a different game
        // from the host's, which is a desync and not a loading error.
        if (!sceneContext.vfs->readFile("maps/" + game.map + ".ota"))
        {
            openMessageBox("You do not have the map " + game.map);
            return;
        }

        auto seat = firstOpenSeat(game);
        if (!seat)
        {
            openMessageBox("The game is full");
            return;
        }

        rememberedNickname = nickname;
        flow.nickname = nickname;
        flow.role = MultiplayerFlow::Role::Guest;
        flow.hostSession = game.session;
        flow.mySeat = *seat;
        flow.claimToken = randomHex(claimTokenLength);
        flow.accepted = false;
        flow.startRequested = false;
        flow.claimedAt = getTimestamp();
        flow.hostSeenAt = getTimestamp();
        resetRoom(flow, model.players.size());

        // A broadcast stays on this subnet. The claim is also sent to the
        // address the host's announcement came from, which is what makes a
        // game found by typing an address joinable.
        flow.discovery.setAnnounceTargets({game.sourceAddress});

        setCandidateSelectedMap(game.map);
        commitSelectedMap();
        if (!model.selectedMap.getValue() || model.selectedMap.getValue()->name != game.map)
        {
            // The map would not read (the box that says why is already up).
            // A mirror on another map is a game that will not agree.
            flow.role = MultiplayerFlow::Role::Browsing;
            flow.discovery.setAnnounceTargets({});
            return;
        }
        applyLobbyToModel(model, game, *seat, gameAddress(game.sourceAddress, game.port), nickname);

        // The choices a guest starts from are the ones the seat is shown
        // with, and it is not waiting for anything to be echoed.
        {
            const auto& mine = model.players[static_cast<std::size_t>(*seat)];
            flow.wanted = GuestChoice{mine.side.getValue(), mine.colorIndex.getValue(), mine.teamIndex.getValue(), false};
        }

        goToBattleroom();
    }

    void MainMenuScene::openNewGameScreen()
    {
        if (!multiplayerFlow)
        {
            return;
        }

        // What was typed on the list carries over, so the name is asked for
        // once.
        if (panelStack.back()->getName() == "SELGAME")
        {
            if (auto box = panelStack.back()->find<UiTextBox>("NICKNAME"))
            {
                rememberedNickname = trimmed(box->get().getText());
            }
        }

        auto panel = makeNewGamePanel(uiFactory);
        if (auto box = panel->find<UiTextBox>("NICKNAME"))
        {
            box->get().setText(rememberedNickname.substr(0, box->get().getMaxLength()));
        }
        panel->setFocusByName(rememberedNickname.empty() ? "NICKNAME" : "GAMENAME");
        goToMenu(std::move(panel));
    }

    void MainMenuScene::confirmNewGame()
    {
        if (!multiplayerFlow || panelStack.back()->getName() != "NEWMULTI")
        {
            return;
        }
        auto& flow = *multiplayerFlow;
        auto& panel = *panelStack.back();

        std::string nickname;
        std::string gameName;
        std::string password;
        if (auto box = panel.find<UiTextBox>("NICKNAME"))
        {
            nickname = trimmed(box->get().getText());
        }
        if (auto box = panel.find<UiTextBox>("GAMENAME"))
        {
            gameName = trimmed(box->get().getText());
        }
        if (auto box = panel.find<UiTextBox>("PASSWORD"))
        {
            password = box->get().getText();
        }

        // The original's own sentences, in the order it asks for them.
        if (nickname.empty())
        {
            openMessageBox("You must enter your name");
            return;
        }
        if (gameName.empty())
        {
            openMessageBox("You must enter a game name");
            return;
        }

        rememberedNickname = nickname;
        flow.nickname = nickname;
        flow.gameName = gameName;
        flow.password = password;
        flow.role = MultiplayerFlow::Role::Host;
        flow.session = randomHex(16);
        flow.seatTokens.assign(model.players.size(), std::string());
        flow.launching = false;
        resetRoom(flow, model.players.size());

        // This machine plays seat one and every other seat is on offer: a
        // joiner takes the first open one. A computer player is something the
        // host adds from the setup screen.
        for (std::size_t i = 0; i < model.players.size(); ++i)
        {
            auto& p = model.players[i];
            p.name.next(std::string());
            p.networkAddress.next(std::string());
            p.type.next(i == 0 ? PlayerType::Human : PlayerType::Open);
        }
        model.players[0].name.next(nickname);

        goToBattleroom();
    }

    bool MainMenuScene::multiplayerGoBack()
    {
        if (!multiplayerFlow || panelStack.empty())
        {
            return false;
        }

        const auto name = panelStack.back()->getName();

        if (name == "LOUNGE2")
        {
            // The battleroom's Previous Menu, for the host and a guest alike.
            returnToGameList(std::string());
            return true;
        }

        if (name == "SELGAME")
        {
            // Previous Menu from the list is the address dialog again, over
            // the connection screen it came from.
            multiplayerFlow->games.clear();
            multiplayerFlow->rows.clear();
            panelStack.pop_back();
            openTcpAddressDialog();
            return true;
        }

        if (name == "SELPROV")
        {
            panelStack.pop_back();
            endMultiplayer();
            return true;
        }

        return false;
    }

    void MainMenuScene::returnToGameList(const std::string& message)
    {
        if (!multiplayerFlow)
        {
            return;
        }
        auto& flow = *multiplayerFlow;

        // Nothing may be left standing over the screen being closed: the map
        // dialog, or a message box about to be replaced by another.
        dialogStack.clear();

        while (!panelStack.empty() && panelStack.back()->getName() != "SELGAME" && panelStack.size() > 1)
        {
            panelStack.pop_back();
        }

        flow.role = MultiplayerFlow::Role::Browsing;
        flow.discovery.stopAdvertising();
        flow.discovery.setAnnounceTargets({});
        flow.launching = false;
        flow.seatTokens.assign(model.players.size(), std::string());
        resetRoom(flow, model.players.size());
        flow.games.clear();
        flow.rows.clear();
        flow.listOpenedAt = getTimestamp();
        multiplayerSetup = false;
        restoreModel(model, flow.snapshot);

        if (!message.empty())
        {
            openMessageBox(message);
        }
    }

    void MainMenuScene::endMultiplayer()
    {
        multiplayerSetup = false;
        if (multiplayerFlow)
        {
            restoreModel(model, multiplayerFlow->snapshot);
        }
        // Dropping the flow stops discovery: the destructor of LanDiscovery
        // joins its thread, which is the only thing that holds the port.
        multiplayerFlow.reset();
    }

    void MainMenuScene::goToBattleroom()
    {
        auto& flow = *multiplayerFlow;
        multiplayerSetup = true;

        // A host that has not chosen a map plays the default one, as the
        // setup screen does; a guest has already been given the host's.
        selectDefaultMap();

        const auto role = roomRoleOf(flow.role);
        auto panel = makeBattleroomPanel(uiFactory, *sceneContext.textureService, role, flow.art);

        if (role == RoomRole::Host)
        {
            // Starting metal and energy are one setting for the room, as
            // they are in the original: a slider that sets every seat's.
            for (const char* name : {"METAL", "ENERGY"})
            {
                auto bar = panel->find<UiScrollBar>(name);
                if (!bar)
                {
                    continue;
                }
                const bool isMetal = std::string(name) == "METAL";
                auto sub = bar->get().scrollChanged().subscribe([this, isMetal](float percent) {
                    if (!multiplayerFlow || multiplayerFlow->role != MultiplayerFlow::Role::Host)
                    {
                        return;
                    }
                    const auto amount = static_cast<float>(resourceStopValue(resourceStopAtPercent(percent)));
                    for (auto& p : model.players)
                    {
                        if (isMetal)
                        {
                            p.metal.next(Metal(amount));
                        }
                        else
                        {
                            p.energy.next(Energy(amount));
                        }
                    }
                });
                bar->get().addSubscription(std::move(sub));
            }
        }

        goToMenu(std::move(panel));
        refreshBattleroom();
    }

    void MainMenuScene::refreshBattleroom()
    {
        if (!multiplayerFlow || panelStack.empty() || panelStack.back()->getName() != "LOUNGE2")
        {
            return;
        }
        auto& flow = *multiplayerFlow;
        if (flow.role == MultiplayerFlow::Role::Browsing)
        {
            return;
        }
        auto& panel = *panelStack.back();
        const auto role = roomRoleOf(flow.role);

        // -- the seats --
        for (std::size_t i = 0; i < model.players.size(); ++i)
        {
            const auto seatName = "PLAYER" + std::to_string(i);
            const auto& p = model.players[i];
            const auto type = p.type.getValue();
            const bool occupied = type != PlayerType::Open;
            const auto controls = seatControls(role, type, isOwnSeat(flow, i, type));

            if (auto b = panel.find<UiStagedButton>(seatName))
            {
                b->get().setLabel(seatCaption(type, p.name.getValue()));
                b->get().setEnabled(controls.name);
            }
            if (auto b = panel.find<UiStagedButton>(seatName + "_side"))
            {
                b->get().setVisible(occupied);
                b->get().setEnabled(controls.side);
                b->get().setStage(p.side.getValue() == PlayerSide::Core ? 1u : 0u);
            }
            if (auto b = panel.find<UiStagedButton>(seatName + "_color"))
            {
                b->get().setVisible(occupied);
                b->get().setEnabled(controls.colour);
                const auto colour = p.colorIndex.getValue().value;
                if (colour < flow.art.logos.size())
                {
                    b->get().setNormalSprite(flow.art.logos[colour]);
                    b->get().setPressedSprite(flow.art.logos[colour]);
                }
            }
            if (auto b = panel.find<UiStagedButton>(seatName + "_team"))
            {
                b->get().setVisible(occupied);
                b->get().setEnabled(controls.team);

                // A pair of faces a team -- the plain one when the team is
                // shared, the other when it is on its own -- and a blank for
                // no team: the same reading the setup screen has of it.
                const auto& team = p.teamIndex.getValue();
                std::size_t face = 10;
                if (team)
                {
                    face = static_cast<std::size_t>(*team) * 2 + (model.isTeamShared(*team) ? 0 : 1);
                }
                if (face < flow.art.allyIcons.size())
                {
                    b->get().setNormalSprite(flow.art.allyIcons[face]);
                    b->get().setPressedSprite(flow.art.allyIcons[face]);
                }
            }
            if (auto b = panel.find<UiStagedButton>(seatName + "_ready"))
            {
                bool ready = false;
                switch (type)
                {
                    case PlayerType::Open:
                        break;
                    case PlayerType::Computer:
                        ready = true;
                        break;
                    case PlayerType::Human:
                        // The host's own seat, or a guest's own: the host is
                        // never waited for, and a guest's is what it set.
                        ready = role == RoomRole::Host || flow.myReady;
                        break;
                    case PlayerType::Network:
                        ready = i < flow.seatReady.size() && flow.seatReady[i];
                        break;
                }
                b->get().setVisible(occupied);
                b->get().setEnabled(controls.ready);
                b->get().setStage(ready ? 1u : 0u);
            }
            if (auto l = panel.find<UiLabel>(seatName + "_ping"))
            {
                // No measurement exists on this transport. A dash says so,
                // and says it only where a machine is on the other end.
                l->get().setVisible(type == PlayerType::Human || type == PlayerType::Network);
            }
        }

        // -- the options --
        const auto options = optionsFromModel(model);
        for (const auto& option : battleroomOptions())
        {
            auto b = panel.find<UiStagedButton>(option.button);
            if (!b)
            {
                continue;
            }
            std::vector<std::string> labels;
            for (unsigned int stage = 0; stage < b->get().getStageCount(); ++stage)
            {
                labels.push_back(b->get().getStageLabel(stage));
            }
            if (const auto* value = findOption(options, option.option))
            {
                if (auto optionStage = parseStage(*value, 8))
                {
                    if (auto stage = buttonStageOfOptionStage(option.option, labels, *optionStage))
                    {
                        b->get().setStage(*stage);
                    }
                }
            }
            b->get().setEnabled(role == RoomRole::Host);
        }

        // -- the map --
        if (auto b = panel.find<UiStagedButton>("MAP"))
        {
            b->get().setEnabled(role == RoomRole::Host);
        }
        const auto& map = model.selectedMap.getValue();
        if (auto l = panel.find<UiLabel>("MAPNAME"))
        {
            l->get().setText(map ? map->name : std::string());
        }

        // -- starting resources, as the first seat has them --
        if (auto bar = panel.find<UiScrollBar>("METAL"))
        {
            bar->get().setScrollPercent(resourceStopPercent(resourceStopNearest(model.players[0].metal.getValue().value)));
        }
        if (auto bar = panel.find<UiScrollBar>("ENERGY"))
        {
            bar->get().setScrollPercent(resourceStopPercent(resourceStopNearest(model.players[0].energy.getValue().value)));
        }
        if (auto l = panel.find<UiLabel>("METALTEXT"))
        {
            l->get().setText(std::to_string(static_cast<int>(model.players[0].metal.getValue().value)));
        }
        if (auto l = panel.find<UiLabel>("ENERGYTEXT"))
        {
            l->get().setText(std::to_string(static_cast<int>(model.players[0].energy.getValue().value)));
        }

        // -- Start, and the line that says why not --
        const auto blocker = startBlocker(model, flow.seatReady, map.has_value());
        if (auto b = panel.find<UiStagedButton>("START"))
        {
            b->get().setEnabled(role == RoomRole::Host && !blocker && !flow.launching);
        }
        if (auto l = panel.find<UiLabel>("HELPTEXT"))
        {
            std::string text;
            if (role == RoomRole::Host)
            {
                text = flow.launching ? "Starting game..." : blocker.value_or("Everyone is ready. Press Start Game.");
            }
            else if (!flow.accepted)
            {
                text = "Asking the host for a seat...";
            }
            else
            {
                text = flow.myReady
                    ? "You are ready. Waiting for the host to start."
                    : "Click the box beside your name when you are ready.";
            }
            l->get().setText(text);
        }

        // -- the transcript --
        if (flow.chat.lastSequence != flow.shownChatSequence)
        {
            if (auto output = panel.find<UiListBox>("OUTPUT"))
            {
                const auto font = sceneContext.textureService->getGafEntry("anims/hattfont12.gaf", "Haettenschweiler (120)");
                const auto widthOf = [&font](const std::string& text) { return textWidthIn(*font, text); };
                const auto rowWidth = static_cast<float>(output->get().getWidth()) - 4.0f;

                output->get().clearItems();
                for (const auto& line : flow.chat.lines)
                {
                    for (const auto& row : wrapChatLine(chatLineText(line), rowWidth, widthOf))
                    {
                        output->get().appendItem(row);
                    }
                }
                output->get().scrollToEnd();
            }
            flow.shownChatSequence = flow.chat.lastSequence;
        }
    }

    void MainMenuScene::sendChatLine()
    {
        if (!multiplayerFlow || panelStack.empty() || panelStack.back()->getName() != "LOUNGE2")
        {
            return;
        }
        auto& flow = *multiplayerFlow;
        auto box = panelStack.back()->find<UiTextBox>("MESSAGE");
        if (!box)
        {
            return;
        }

        const auto text = sanitisedChatText(box->get().getText());
        box->get().setText(std::string());
        if (text.empty())
        {
            // A line with nothing in it is not a line.
            return;
        }

        if (flow.role == MultiplayerFlow::Role::Host)
        {
            flow.chat.say(flow.nickname, text);
        }
        else if (flow.role == MultiplayerFlow::Role::Guest)
        {
            // Put on the claim, and shown when the host has taken it.
            DiscoveredChatLine line;
            line.sequence = flow.nextChatSequence++;
            line.name = flow.nickname.substr(0, maxPlayerNameLength);
            line.text = text;
            flow.sentChat.push_back(std::move(line));
            // The claim is a datagram: the last few are repeated, so that
            // one that is lost is not a line that is lost.
            const std::size_t repeated = 4;
            if (flow.sentChat.size() > repeated)
            {
                flow.sentChat.erase(flow.sentChat.begin(), flow.sentChat.end() - repeated);
            }
        }
    }

    bool MainMenuScene::battleroomMessage(const std::string& message, const ActivateMessage& details)
    {
        if (!multiplayerFlow || multiplayerFlow->role == MultiplayerFlow::Role::Browsing)
        {
            return false;
        }
        auto& flow = *multiplayerFlow;
        const bool host = flow.role == MultiplayerFlow::Role::Host;

        if (message == "PrevMenu" || message == "PREVMENU")
        {
            return false;
        }

        // Typing goes to the line box again after anything on the screen has
        // been pressed: pressing a button moves the focus onto it, and a
        // room is where a person types. Not when the press opened a dialog.
        const auto keepTyping = [this]() {
            if (dialogStack.empty() && !panelStack.empty() && panelStack.back()->getName() == "LOUNGE2")
            {
                panelStack.back()->setFocusByName("MESSAGE");
            }
        };

        if (message == "SEND")
        {
            sendChatLine();
        }
        else if (message == "MAP")
        {
            if (host && dialogStack.empty())
            {
                resetCandidateSelectedMap();
                openMapSelectionDialog();
                return true;
            }
        }
        else if (message == "START")
        {
            if (host && !flow.launching && !startBlocker(model, flow.seatReady, model.selectedMap.getValue().has_value()))
            {
                beginHostedLaunch();
                return true;
            }
        }
        else if (const auto option = std::find_if(battleroomOptions().begin(), battleroomOptions().end(), [&](const auto& o) { return message == o.button; });
                 option != battleroomOptions().end())
        {
            auto button = host ? panelStack.back()->find<UiStagedButton>(option->button) : std::nullopt;
            if (button)
            {
                std::vector<std::string> labels;
                for (unsigned int stage = 0; stage < button->get().getStageCount(); ++stage)
                {
                    labels.push_back(button->get().getStageLabel(stage));
                }
                // The model's own stage for the button's current face, then
                // the next face that means something -- by label.
                const auto current = optionsFromModel(model);
                const auto* value = findOption(current, option->option);
                const auto currentStage = value ? parseStage(*value, 8) : std::nullopt;
                if (currentStage)
                {
                    if (auto next = nextOptionStage(option->option, labels, *currentStage))
                    {
                        applyOptionsToModel(model, {{option->option, std::to_string(*next)}});
                    }
                }
            }
        }
        else if (const auto seatMessage = parseSeatMessage(message, model.players.size()))
        {
            const auto i = seatMessage->seat;
            auto& p = model.players[i];
            const auto type = p.type.getValue();
            const auto controls = seatControls(roomRoleOf(flow.role), type, isOwnSeat(flow, i, type));
            const auto& control = seatMessage->control;
            const auto index = static_cast<int>(i);

            // A guest's click on its own seat is a request: the model shows
            // it at once and the claim carries it, and the host's
            // announcement has the last word.
            const auto noteWanted = [&]() {
                if (!host)
                {
                    flow.wanted = GuestChoice{p.side.getValue(), p.colorIndex.getValue(), p.teamIndex.getValue(), flow.myReady};
                    flow.choicePending = true;
                    flow.choiceAt = getTimestamp();
                }
            };

            if (control.empty())
            {
                if (controls.name && host)
                {
                    switch (type)
                    {
                        case PlayerType::Open:
                            // A colour the seat comes in with that another
                            // seat has is changed for the first that is free.
                            p.type.next(PlayerType::Computer);
                            ensureFreeColour(model, i);
                            break;
                        case PlayerType::Computer:
                            p.type.next(PlayerType::Open);
                            break;
                        case PlayerType::Network:
                        {
                            // Out of the room. The guest's claim goes on
                            // being heard until it notices, and must not
                            // seat it again, so the token is remembered.
                            const auto name = p.name.getValue();
                            const auto token = flow.seatTokens[i];
                            if (!token.empty())
                            {
                                flow.removedTokens.push_back(token);
                                flow.claimMemory.erase(token);
                            }
                            flow.seatTokens[i].clear();
                            flow.seatReady[i] = false;
                            p.name.next(std::string());
                            p.networkAddress.next(std::string());
                            p.type.next(PlayerType::Open);
                            flow.chat.say(std::string(), (name.empty() ? "A player" : name) + " was removed");
                            break;
                        }
                        case PlayerType::Human:
                            break;
                    }
                }
            }
            else if (control == "side")
            {
                if (controls.side)
                {
                    togglePlayerSide(index);
                    noteWanted();
                }
            }
            else if (control == "color")
            {
                if (controls.colour)
                {
                    if (details.type == ActivateMessage::Type::Secondary)
                    {
                        reverseCyclePlayerColor(index);
                    }
                    else
                    {
                        cyclePlayerColor(index);
                    }
                    noteWanted();
                }
            }
            else if (control == "team")
            {
                if (controls.team)
                {
                    cyclePlayerTeam(index);
                    noteWanted();
                }
            }
            else if (control == "ready")
            {
                if (controls.ready && !host)
                {
                    flow.myReady = !flow.myReady;
                }
            }
        }

        refreshBattleroom();
        keepTyping();
        return true;
    }

    void MainMenuScene::beginHostedLaunch()
    {
        if (!multiplayerFlow || multiplayerFlow->role != MultiplayerFlow::Role::Host || multiplayerFlow->launching)
        {
            return;
        }

        readNetworkAddressesFromPanel();

        // The same checks the real start makes, now, so that a problem is
        // said before the guests are told to go.
        if (!prepareGameParameters())
        {
            return;
        }

        multiplayerFlow->launching = true;
        multiplayerFlow->launchBeganAt = getTimestamp();
        openMessageBox("Starting game...");
    }

    void MainMenuScene::updateHostedLobby()
    {
        auto& flow = *multiplayerFlow;
        if (!flow.discoveryRunning)
        {
            return;
        }

        // -- who has asked for a seat --
        const auto claims = claimsOn(flow.discovery.games(flow.session), flow.session);

        // A guest that was put out stays in `removedTokens` for as long as
        // its claim is still being heard, and not a moment longer.
        flow.removedTokens.erase(
            std::remove_if(flow.removedTokens.begin(), flow.removedTokens.end(), [&](const auto& token) {
                return std::none_of(claims.begin(), claims.end(), [&](const auto& c) { return c.token == token; });
            }),
            flow.removedTokens.end());

        // A claim that has gone quiet frees its seat, unless the host has
        // already changed the seat itself.
        for (std::size_t seat = 0; seat < flow.seatTokens.size(); ++seat)
        {
            auto& token = flow.seatTokens[seat];
            if (token.empty())
            {
                continue;
            }
            const bool stillClaiming = std::any_of(claims.begin(), claims.end(), [&](const auto& c) { return c.token == token; });
            if (!stillClaiming)
            {
                auto& p = model.players[seat];
                if (p.type.getValue() == PlayerType::Network)
                {
                    const auto name = p.name.getValue();
                    flow.chat.say(std::string(), (name.empty() ? std::string("A player") : name) + " has left");
                    p.name.next(std::string());
                    p.networkAddress.next(std::string());
                    p.type.next(PlayerType::Open);
                }
                flow.claimMemory.erase(token);
                flow.seatReady[seat] = false;
                token.clear();
            }
        }

        for (const auto& claim : claims)
        {
            if (claim.seat < 0 || static_cast<std::size_t>(claim.seat) >= model.players.size())
            {
                continue;
            }
            if (std::find(flow.removedTokens.begin(), flow.removedTokens.end(), claim.token) != flow.removedTokens.end())
            {
                continue;
            }

            auto held = std::find(flow.seatTokens.begin(), flow.seatTokens.end(), claim.token);
            if (held != flow.seatTokens.end())
            {
                // Already seated: its address can move (a different port
                // typed on the guest), and that follows.
                const auto seat = static_cast<std::size_t>(held - flow.seatTokens.begin());
                auto& p = model.players[seat];
                if (p.type.getValue() == PlayerType::Network && p.networkAddress.getValue() != claim.address)
                {
                    p.networkAddress.next(claim.address);
                }

                // What the guest chose, acted on when it is a change from
                // what was last heard, so that a choice the host refused is
                // not tried again every time it is repeated.
                auto& memory = flow.claimMemory[claim.token];
                if (memory.side != claim.side || memory.colour != claim.colour || memory.team != claim.team)
                {
                    applyClaimChoice(model, seat, claim);
                    memory.side = claim.side;
                    memory.colour = claim.colour;
                    memory.team = claim.team;
                }

                flow.seatReady[seat] = claim.ready;
                takeClaimChat(flow.chat, memory.lastChat, p.name.getValue(), claim.chat);
                continue;
            }

            // First come, first seated. A claim for a seat that is not open
            // is simply not honoured, and the guest sees that from the
            // seat's description and asks for another.
            auto seat = static_cast<std::size_t>(claim.seat);
            if (model.players[seat].type.getValue() != PlayerType::Open || !flow.seatTokens[seat].empty())
            {
                continue;
            }

            auto& p = model.players[seat];
            p.name.next(claim.name);
            p.networkAddress.next(claim.address);
            p.type.next(PlayerType::Network);
            flow.seatTokens[seat] = claim.token;

            // The seat comes in with a colour no other seat has; then what
            // the guest asked for on the way in, which may be refused.
            ensureFreeColour(model, seat);
            applyClaimChoice(model, seat, claim);

            auto& memory = flow.claimMemory[claim.token];
            memory.side = claim.side;
            memory.colour = claim.colour;
            memory.team = claim.team;
            memory.lastChat = 0;

            flow.seatReady[seat] = claim.ready;
            flow.chat.say(std::string(), (claim.name.empty() ? std::string("A player") : claim.name) + " has joined");
            takeClaimChat(flow.chat, memory.lastChat, claim.name, claim.chat);
        }

        // -- what is announced --
        LobbyIdentity identity;
        identity.session = flow.session;
        identity.label = flow.gameName;
        identity.hostName = flow.nickname;
        if (!flow.password.empty())
        {
            identity.passwordHash = LanDiscovery::hashPassword(flow.session, flow.password);
        }
        try
        {
            identity.port = isUsablePort(model.localNetworkPort.getValue())
                ? static_cast<unsigned int>(std::stoul(model.localNetworkPort.getValue()))
                : 1337u;
        }
        catch (const std::exception&)
        {
            identity.port = 1337;
        }
        identity.launching = flow.launching;
        identity.ready = flow.seatReady;
        identity.chat = flow.chat.tail();

        if (const auto& map = model.selectedMap.getValue())
        {
            flow.discovery.advertise(lobbyFromModel(model, identity, map->name, flow.seatTokens));
        }
        else
        {
            flow.discovery.stopAdvertising();
        }

        // -- the couple of seconds in which the guests are told to start --
        if (flow.launching && secondsSince(flow.launchBeganAt) >= 2.5f)
        {
            flow.launching = false;
            startGame();
        }
    }

    void MainMenuScene::updateJoinedLobby()
    {
        auto& flow = *multiplayerFlow;
        if (!flow.discoveryRunning || flow.startRequested)
        {
            return;
        }

        const auto claimOwnSession = claimSession(flow.hostSession, flow.claimToken);
        const auto heard = flow.discovery.games(claimOwnSession);
        const auto host = std::find_if(heard.begin(), heard.end(), [&](const auto& g) { return g.session == flow.hostSession; });

        if (host == heard.end())
        {
            // An announcement every second and a memory of three and a half
            // seconds: five seconds of nothing is a host that has gone.
            if (secondsSince(flow.hostSeenAt) > 5.0f)
            {
                returnToGameList("The host has left");
            }
            return;
        }
        flow.hostSeenAt = getTimestamp();

        // Keep asking the host to announce. On the same network its own
        // broadcasts do it; across one a host only speaks when spoken to,
        // and an announcement is forgotten after three and a half seconds.
        if (secondsSince(flow.lastProbeAt) >= 1.0f)
        {
            flow.discovery.probe(host->sourceAddress);
            flow.lastProbeAt = getTimestamp();
        }

        // The map first: everything else is relative to it. A host that
        // changes to one this machine does not have cannot be followed.
        const auto& selected = model.selectedMap.getValue();
        if (!selected || selected->name != host->map)
        {
            if (!sceneContext.vfs->readFile("maps/" + host->map + ".ota"))
            {
                returnToGameList("You do not have the map " + host->map);
                return;
            }
            setCandidateSelectedMap(host->map);
            commitSelectedMap();
        }

        // -- is the seat mine --
        switch (claimState(*host, flow.mySeat, flow.claimToken))
        {
            case ClaimState::Accepted:
                flow.accepted = true;
                break;
            case ClaimState::Waiting:
                if (flow.accepted)
                {
                    returnToGameList("You were removed from the game");
                    return;
                }
                if (secondsSince(flow.claimedAt) > 8.0f)
                {
                    returnToGameList("The host did not answer");
                    return;
                }
                break;
            case ClaimState::Lost:
                if (flow.accepted)
                {
                    returnToGameList("You were removed from the game");
                    return;
                }
                else
                {
                    // Somebody else got the seat first. Ask for another,
                    // with a token of its own so that the host does not
                    // mistake this for the claim it has already turned down.
                    auto next = firstOpenSeat(*host);
                    if (!next)
                    {
                        returnToGameList("The game is full");
                        return;
                    }
                    flow.mySeat = *next;
                    flow.claimToken = randomHex(claimTokenLength);
                    flow.claimedAt = getTimestamp();
                }
                break;
        }

        // -- mirror the host, whole --
        applyLobbyToModel(model, *host, flow.mySeat, gameAddress(host->sourceAddress, host->port), flow.nickname);

        // -- the room: what was said, and who is ready --
        flow.chat.merge(host->chat);
        for (const auto& slot : host->slots)
        {
            if (slot.index >= 0 && static_cast<std::size_t>(slot.index) < flow.seatReady.size())
            {
                flow.seatReady[static_cast<std::size_t>(slot.index)] = slot.ready;
            }
        }

        // -- what this guest asked for, until the host says the same --
        if (flow.choicePending && flow.mySeat >= 0 && static_cast<std::size_t>(flow.mySeat) < model.players.size())
        {
            auto& mine = model.players[static_cast<std::size_t>(flow.mySeat)];
            const auto* slot = findSlot(*host, flow.mySeat);
            const bool echoed = slot != nullptr
                && mine.side.getValue() == flow.wanted.side
                && mine.colorIndex.getValue() == flow.wanted.colour
                && mine.teamIndex.getValue() == flow.wanted.team;
            if (echoed || secondsSince(flow.choiceAt) > 2.0f)
            {
                // Said back to it, or refused: either way the host's word
                // is what is shown from here.
                flow.choicePending = false;
            }
            else
            {
                mine.side.next(flow.wanted.side);
                mine.colorIndex.next(flow.wanted.colour);
                mine.teamIndex.next(flow.wanted.team);
            }
        }

        // -- say where this machine is --
        readNetworkAddressesFromPanel();
        unsigned int port = 1337;
        if (isUsablePort(model.localNetworkPort.getValue()))
        {
            try
            {
                port = static_cast<unsigned int>(std::stoul(model.localNetworkPort.getValue()));
            }
            catch (const std::exception&)
            {
            }
        }
        std::optional<GuestChoice> choice;
        if (flow.mySeat >= 0 && static_cast<std::size_t>(flow.mySeat) < model.players.size())
        {
            const auto& mine = model.players[static_cast<std::size_t>(flow.mySeat)];
            choice = GuestChoice{mine.side.getValue(), mine.colorIndex.getValue(), mine.teamIndex.getValue(), flow.myReady};
        }
        flow.discovery.advertise(claimAnnouncement(*host, flow.mySeat, flow.claimToken, flow.nickname, port, choice, flow.sentChat));

        // -- the host has pressed Start --
        if (host->launching && flow.accepted)
        {
            // The mirror is what is started from: it was just applied, so
            // the game that starts is the one the host announced.
            const auto& now = model.selectedMap.getValue();
            if (!now || now->name != host->map)
            {
                returnToGameList("Could not load the map " + host->map);
                return;
            }
            flow.startRequested = true;
            startGame();
        }
    }

    void MainMenuScene::updateMultiplayer()
    {
        if (!multiplayerFlow)
        {
            return;
        }

        switch (multiplayerFlow->role)
        {
            case MultiplayerFlow::Role::Browsing:
                if (!panelStack.empty() && panelStack.back()->getName() == "SELGAME")
                {
                    updateGameList();
                }
                break;
            case MultiplayerFlow::Role::Host:
                updateHostedLobby();
                break;
            case MultiplayerFlow::Role::Guest:
                updateJoinedLobby();
                break;
        }

        // After the update, so that what is on screen is what the lobby
        // became this frame and not last frame's. A guest that was just put
        // back on the list has no room to refresh, and refreshBattleroom
        // says so by returning.
        refreshBattleroom();
    }

    bool MainMenuScene::multiplayerMessage(const std::string& topic, const std::string& message, const ActivateMessage& details)
    {
        if (!multiplayerFlow)
        {
            return false;
        }
        const auto role = multiplayerFlow->role;

        if (topic == "SELPROV" && message == "SELECT")
        {
            if (dialogStack.empty())
            {
                openTcpAddressDialog();
            }
            return true;
        }

        if (topic == "TCP")
        {
            if (message == "OK")
            {
                confirmTcpAddress();
                return true;
            }
            if (message == "PREV")
            {
                // Cancel closes the dialog and leaves the connection screen
                // as it was.
                if (!dialogStack.empty() && dialogStack.back()->getName() == "TCP")
                {
                    dialogStack.pop_back();
                }
                return true;
            }
            return false;
        }

        if (topic == "SELGAME")
        {
            if (message == "STARTNEW")
            {
                openNewGameScreen();
                return true;
            }
            if (message == "JOINGAME")
            {
                joinSelectedGame();
                return true;
            }
            if (message == "UPDATE")
            {
                // Asks again now rather than at the next second.
                multiplayerFlow->lastProbeAt = Timestamp{};
                multiplayerFlow->listOpenedAt = getTimestamp();
                return true;
            }
            return false;
        }

        if (topic == "NEWMULTI")
        {
            if (message == "OK")
            {
                confirmNewGame();
                return true;
            }
            if (message == "CANCEL")
            {
                goToPreviousMenu();
                return true;
            }
            return false;
        }

        if (topic == "LOUNGE2" && role != MultiplayerFlow::Role::Browsing)
        {
            return battleroomMessage(message, details);
        }

        return false;
    }
}
