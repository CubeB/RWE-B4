#pragma once

#include <map>
#include <memory>
#include <optional>
#include <rwe/MainMenuModel.h>
#include <rwe/net/LanDiscovery.h>
#include <rwe/rwe_time.h>
#include <string>
#include <utility>
#include <vector>

/**
 * The multiplayer front end's logic, apart from the scene that drives it.
 *
 * TotalA.exe reaches a network game by MULTI -> SELPROV.GUI (the connection)
 * -> TCP.GUI (an address, or blank to search) -> SELGAME.GUI (the list, with
 * New and Join) -> NEWMULTI.GUI (name, game name, password) -> the battleroom.
 * RWE has no battleroom of its own, so its stand-in is the skirmish screen
 * run as a read-only mirror for a guest and as the editable screen for the
 * host. What is here is everything about that which does not need a window:
 * the translation between the setup screen's model and a LAN announcement,
 * who may sit where, what a row of the game list says, and the four panels
 * as the GUI files lay them out. `ui_probe` builds the same four panels with
 * the same functions, which is how they are checked without a game running.
 */
namespace rwe
{
    class UiFactory;
    class UiPanel;

    using LobbyOptions = std::vector<std::pair<std::string, std::string>>;

    /**
     * The game-wide options the setup screen has, as the announcement names
     * them. One list, read both ways, so the host that writes them and the
     * guest that reads them cannot drift apart: a guest left with a different
     * line-of-sight mode from its host is a lockstep desync on the first
     * tick. Every value is the stage the staged button on the screen is
     * showing, as a decimal number.
     *
     *   commander   0 game ends, 1 continues
     *   startloc    0 fixed, 1 random
     *   mapping     0 unmapped, 1 mapped
     *   los         0 permanent, 1 true, 2 circular
     *   difficulty  0 easy, 1 medium, 2 hard
     */
    LobbyOptions optionsFromModel(const MainMenuModel& model);

    /**
     * Sets the model's options from an announcement's. A name it does not
     * know is ignored (a newer build's), and so is a value out of its range
     * -- that option keeps what it had.
     */
    void applyOptionsToModel(MainMenuModel& model, const LobbyOptions& options);

    /** What a host says about itself, beside the seats. */
    struct LobbyIdentity
    {
        std::string session;
        std::string label;
        std::string hostName;
        std::string passwordHash;
        unsigned int port{1337};
        bool launching{false};
    };

    /**
     * The longest name the wire carries for a person: DiscoveredGame::hostName
     * is bounded to twenty bytes and a name that is longer is a lobby that
     * will not announce at all.
     */
    constexpr std::size_t maxPlayerNameLength = LanDiscovery::maxHostNameLength;

    /**
     * The setup screen as an announcement, from the host's side: its own seat
     * is `human`, a seat somebody on another machine plays is `network` with
     * that machine's address, and that seat's `personality` carries the token
     * the guest who took it announced itself with, so that it can tell the
     * seat is its own (see SeatClaim). `seatTokens` has one entry per seat,
     * empty for a seat nobody claimed.
     *
     * A name is carried only for a seat a person plays. The game seeds its
     * random generator from every player's name, so two machines that
     * disagree about one -- a stale name on a seat that is now a computer's --
     * play different games from tick zero.
     */
    DiscoveredGame lobbyFromModel(
        const MainMenuModel& model,
        const LobbyIdentity& identity,
        const std::string& mapName,
        const std::vector<std::string>& seatTokens);

    /**
     * The first seat of a lobby that is open, if there is one. This is the
     * seat a joining machine asks for, and the only kind it can have: a seat
     * the host has set to a computer or a person is not on offer.
     */
    std::optional<int> firstOpenSeat(const DiscoveredGame& game, const std::vector<int>& notThese = {});

    /**
     * Fills the model so it shows what the host announced, from the guest's
     * side: the guest's own seat is Player, the host's own seat -- which the
     * host calls `human` -- is Network at `hostGameAddress`, and every other
     * seat is as the host said. Everything that is shared state comes from
     * the announcement and nothing from what was on screen before; the
     * options go through applyOptionsToModel.
     *
     * Called again on every update while joined, so that a guest cannot be
     * left holding a stale copy of a lobby the host has since changed. It is
     * cheap when nothing moved, because a BehaviorSubject does not emit an
     * unchanged value.
     */
    void applyLobbyToModel(
        MainMenuModel& model,
        const DiscoveredGame& game,
        int mySeat,
        const std::string& hostGameAddress,
        const std::string& myName);

    // ---- who is sitting where ----

    /**
     * How a guest asks for a seat. It announces a one-seat lobby whose session
     * is the host's, then `-g`, then a token of its own; the host reads the
     * seat and the guest's name off it and the guest's address off the
     * datagram. The token is what lets the guest recognise its seat in the
     * host's next announcement without having to know what address the host
     * saw it from, which a machine with two interfaces or behind a translator
     * cannot know.
     */
    struct SeatClaim
    {
        std::string token;
        int seat{-1};
        std::string name;
        std::string address;
    };

    constexpr std::size_t claimTokenLength = 8;

    std::string claimSession(const std::string& hostSession, const std::string& token);

    /** Whether a session is a guest's claim, and not a lobby of its own. */
    bool isClaimSession(const std::string& session);

    /**
     * The claims on one host's lobby, in the order they were first heard.
     * Anything that is not a well-formed claim on `hostSession` -- another
     * host's guest, a lobby, a claim with no seat -- is left out.
     */
    std::vector<SeatClaim> claimsOn(const std::vector<DiscoveredGame>& heard, const std::string& hostSession);

    /** The announcement that makes a claim. */
    DiscoveredGame claimAnnouncement(
        const DiscoveredGame& host,
        int seat,
        const std::string& token,
        const std::string& name,
        unsigned int gamePort);

    enum class ClaimState
    {
        /** The seat is still open: the host has not heard the claim yet. */
        Waiting,
        /** The host lists the seat as a network seat with this token. */
        Accepted,
        /** The seat is something else now: taken by another, or changed. */
        Lost
    };

    ClaimState claimState(const DiscoveredGame& host, int seat, const std::string& token);

    // ---- addresses ----

    /**
     * The host half of a `host:port` as LanDiscovery reports a sender, with
     * an IPv6 address's brackets kept: `[::1]:1338` gives `[::1]`.
     */
    std::string hostPartOf(const std::string& sourceAddress);

    /** `hostPartOf(source)` and a game port, written the way a --player address is. */
    std::string gameAddress(const std::string& sourceAddress, unsigned int port);

    // ---- the game list ----

    /** One game as SELGAME.GUI's ten columns show it. */
    struct GameListRow
    {
        std::string gameName;
        std::string mapName;
        /** "Yes" if a dead commander ends the game: the column headed Cmdr. */
        std::string commander;
        std::string ping;
        std::string lineOfSight;
        /** "Yes" if the map starts uncovered: the column headed Map. */
        std::string mapping;
        std::string metal;
        std::string energy;
        std::string players;
        std::string status;

        bool operator==(const GameListRow& rhs) const = default;
    };

    GameListRow gameListRow(const DiscoveredGame& game);

    /** True with no password set, or when `typed` hashes to the lobby's. */
    bool passwordAccepted(const DiscoveredGame& game, const std::string& typed);

    /** Whether any seat of the lobby is open: what "Open" in the Status column means. */
    bool hasOpenSeat(const DiscoveredGame& game);

    /** Strips leading and trailing spaces. A name is what is left. */
    std::string trimmed(const std::string& text);

    // ---- the model across a visit ----

    /**
     * Everything on the setup screen a mirrored lobby overwrites, taken when
     * the multiplayer screens are entered and put back when they are left.
     * Without it a skirmish started afterwards finds seats set to Network
     * with addresses that were someone else's.
     */
    struct ModelSnapshot
    {
        struct Seat
        {
            MainMenuModel::PlayerSettings::Type type;
            MainMenuModel::PlayerSettings::Side side;
            PlayerColorIndex color{0};
            std::optional<int> team;
            Metal metal;
            Energy energy;
            std::string personality;
        };
        std::vector<Seat> seats;
        LobbyOptions options;
    };

    ModelSnapshot snapshotModel(const MainMenuModel& model);
    void restoreModel(MainMenuModel& model, const ModelSnapshot& snapshot);

    // ---- the four panels ----

    /** SELPROV.GUI over `selconnect2`: one connection, Internet TCP/IP, already selected. */
    std::unique_ptr<UiPanel> makeSelectConnectionPanel(UiFactory& factory);

    /** TCP.GUI: the address dialog, with Enter bound to OK and Escape to Cancel. */
    std::unique_ptr<UiPanel> makeTcpAddressPanel(UiFactory& factory);

    /** SELGAME.GUI over `selectgame2x`, its nine columns' selections tied together. */
    std::unique_ptr<UiPanel> makeSelectGamePanel(UiFactory& factory);

    /** NEWMULTI.GUI over `createnew`. */
    std::unique_ptr<UiPanel> makeNewGamePanel(UiFactory& factory);

    /** The gadget names of SELGAME.GUI's columns, left to right. */
    const std::vector<std::string>& gameListColumnNames();

    /** Replaces every column's rows; selection and scroll go with them. */
    void fillGameList(UiPanel& panel, const std::vector<GameListRow>& rows);

    /** The selected row of the game list, or nothing. */
    std::optional<unsigned int> selectedGameRow(UiPanel& panel);

    void selectGameRow(UiPanel& panel, const std::optional<unsigned int>& row);

    /**
     * Everything the multiplayer screens remember while they are up, and
     * nothing before or after: the scene holds one only between MULTI and
     * the game starting (or the main menu again). It is its own type, not
     * members of the scene, so that MainMenuScene.h -- which most of the
     * engine includes -- does not carry asio and a LanDiscovery by value;
     * the scene's constructor and destructor are in the two .cpp files that
     * include this header.
     */
    struct MultiplayerFlow
    {
        enum class Role
        {
            /** On the connection screens or the game list. */
            Browsing,
            /** On the setup screen, with a game of its own advertised. */
            Host,
            /** On the setup screen, mirroring somebody else's game. */
            Guest
        };

        Role role{Role::Browsing};

        LanDiscovery discovery;
        bool discoveryRunning{false};

        ModelSnapshot snapshot;

        /** The TCP dialog's address; empty is "scan this network". */
        std::string address;

        /** The name this machine plays under, once one has been given. */
        std::string nickname;

        // -- the game list --
        std::vector<DiscoveredGame> games;
        std::vector<GameListRow> rows;
        Timestamp listOpenedAt{};
        Timestamp lastProbeAt{};

        // -- hosting --
        std::string session;
        std::string gameName;
        std::string password;
        std::vector<std::string> seatTokens = std::vector<std::string>(10);
        bool launching{false};
        Timestamp launchBeganAt{};

        // -- joined --
        std::string hostSession;
        std::string claimToken;
        int mySeat{-1};
        bool accepted{false};
        bool startRequested{false};
        Timestamp claimedAt{};
        Timestamp hostSeenAt{};
    };
}
