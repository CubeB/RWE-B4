#include "MainMenuScene_multiplayer.h"

#include "MainMenuScene.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <rwe/MultiplayerSetup.h>
#include <rwe/rwe_time.h>
#include <rwe/ui/UiLabel.h>
#include <rwe/ui/UiListBox.h>
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
                    seat.controller = "human";
                    seat.name = p.name.getValue().substr(0, maxPlayerNameLength);
                    break;
                case PlayerType::Computer:
                    seat.controller = "computer";
                    seat.personality = p.personality.getValue().substr(0, LanDiscovery::maxPersonalityLength);
                    break;
                case PlayerType::Network:
                    seat.controller = "network";
                    seat.address = p.networkAddress.getValue();
                    seat.name = p.name.getValue().substr(0, maxPlayerNameLength);
                    if (i < seatTokens.size())
                    {
                        seat.personality = seatTokens[i];
                    }
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
            claims.push_back(std::move(claim));
        }
        return claims;
    }

    DiscoveredGame claimAnnouncement(
        const DiscoveredGame& host,
        int seat,
        const std::string& token,
        const std::string& name,
        unsigned int gamePort)
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
        claim.slots.push_back(std::move(slot));
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

        goToPlayerSetupMenu(true);
        lockMirroredControls();
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

        goToPlayerSetupMenu(true);
    }

    bool MainMenuScene::multiplayerGoBack()
    {
        if (!multiplayerFlow || panelStack.empty())
        {
            return false;
        }

        const auto name = panelStack.back()->getName();

        if (name == "SKIRMISH" && multiplayerSetup)
        {
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

    void MainMenuScene::lockMirroredControls()
    {
        if (panelStack.empty() || panelStack.back()->getName() != "SKIRMISH")
        {
            return;
        }
        auto& panel = *panelStack.back();

        for (auto& child : panel.getChildren())
        {
            const auto& name = child->getName();

            if (auto* button = dynamic_cast<UiStagedButton*>(child.get()))
            {
                // Previous Menu is how a guest leaves. Everything else on
                // this screen is state the host owns -- but a side, colour
                // or team button is its own picture, and its greyed frame
                // would hide the one thing a guest looks here for. Those
                // stay as they are and their clicks are ignored (see
                // multiplayerMessage).
                const bool isPicture = endsWith(name, "_side") || endsWith(name, "_color") || endsWith(name, "_team");
                if (name != "PrevMenu" && name != "PREVMENU" && !isPicture)
                {
                    button->setEnabled(false);
                }
            }
            else if (auto* box = dynamic_cast<UiTextBox*>(child.get()))
            {
                // The port is this machine's own, and stays editable.
                if (name != "LOCALPORT")
                {
                    box->setEditable(false);
                }
            }
        }

        if (auto* help = helpLabel(panel))
        {
            help->setText("Waiting for the host to start");
        }
    }

    void MainMenuScene::showAddressesInBoxes()
    {
        if (panelStack.empty())
        {
            return;
        }
        auto& panel = *panelStack.back();
        for (std::size_t i = 0; i < model.players.size(); ++i)
        {
            if (auto box = panel.find<UiTextBox>("PLAYER" + std::to_string(i) + "_address"))
            {
                const auto& wanted = model.players[i].networkAddress.getValue();
                if (box->get().getText() != wanted)
                {
                    box->get().setText(wanted);
                }
            }
        }
    }

    void MainMenuScene::updateHostedLobby()
    {
        auto& flow = *multiplayerFlow;
        if (!flow.discoveryRunning)
        {
            return;
        }

        // What is typed in the address and port boxes is read before
        // anything announces it.
        readNetworkAddressesFromPanel();

        // -- who has asked for a seat --
        const auto claims = claimsOn(flow.discovery.games(flow.session), flow.session);

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
                    p.name.next(std::string());
                    p.networkAddress.next(std::string());
                    p.type.next(PlayerType::Open);
                }
                token.clear();
                showAddressesInBoxes();
            }
        }

        for (const auto& claim : claims)
        {
            if (claim.seat < 0 || static_cast<std::size_t>(claim.seat) >= model.players.size())
            {
                continue;
            }

            auto held = std::find(flow.seatTokens.begin(), flow.seatTokens.end(), claim.token);
            if (held != flow.seatTokens.end())
            {
                // Already seated: its address can move (a different port
                // typed on the guest), and that follows.
                auto& p = model.players[static_cast<std::size_t>(held - flow.seatTokens.begin())];
                if (p.type.getValue() == PlayerType::Network && p.networkAddress.getValue() != claim.address)
                {
                    p.networkAddress.next(claim.address);
                    showAddressesInBoxes();
                }
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
            showAddressesInBoxes();
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
        showAddressesInBoxes();
        lockMirroredControls();

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
        flow.discovery.advertise(claimAnnouncement(*host, flow.mySeat, flow.claimToken, flow.nickname, port));

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
    }

    bool MainMenuScene::multiplayerMessage(const std::string& topic, const std::string& message, const ActivateMessage& /*details*/)
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

        if (topic == "SKIRMISH" && multiplayerSetup)
        {
            if (message == "PrevMenu" || message == "PREVMENU")
            {
                return false;
            }
            if (role == MultiplayerFlow::Role::Guest)
            {
                // A read-only mirror: nothing on this screen changes shared
                // state, whichever way the click got through.
                return true;
            }
            if (role == MultiplayerFlow::Role::Host && message == "Start")
            {
                beginHostedLaunch();
                return true;
            }
        }

        return false;
    }
}
