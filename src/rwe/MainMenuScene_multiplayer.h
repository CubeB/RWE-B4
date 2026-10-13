#pragma once

#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <rwe/MainMenuModel.h>
#include <rwe/net/LanDiscovery.h>
#include <rwe/net/NatTraversal.h>
#include <rwe/rwe_time.h>
#include <string>
#include <utility>
#include <vector>

/**
 * The multiplayer front end's logic, apart from the scene that drives it.
 *
 * TotalA.exe reaches a network game by MULTI -> SELPROV.GUI (the connection)
 * -> TCP.GUI (an address, or blank to search) -> SELGAME.GUI (the list, with
 * New and Join) -> NEWMULTI.GUI (name, game name, password) -> the battleroom,
 * LOUNGE2.GUI over battleroom.pcx (the 3.1 patch's layout) or battleroom2.pcx
 * (the original's). What is here is everything about that
 * which does not need a window: the translation between the setup screen's
 * model and a LAN announcement, who may sit where, who may touch which control
 * of a seat, the room's chat, what a row of the game list says, and the
 * panels as the GUI files lay them out. `ui_probe` builds the same panels with
 * the same functions, which is how they are checked without a game running.
 *
 * The model is still the one source of truth for a seat -- type, side, colour,
 * team, resources -- and the announcement still carries it; the battleroom is
 * a view of that, plus what only a room has: who is ready and what was said.
 */
namespace rwe
{
    class TextureService;
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

        /**
         * Where this machine's game port is as the internet sees it, once
         * it has found out (NatTraversal), or empty. See
         * DiscoveredGame::gameAddress for why a LAN lobby leaves it empty.
         */
        std::string gameAddress{};

        /**
         * Per seat, whether the person on another machine in it has said they
         * are ready. Read only for a Network seat: the host's own seat and a
         * computer's are always announced ready, and an open one never is.
         */
        std::vector<bool> ready{};

        /** The room's transcript, as announced: the newest lines, oldest first. */
        std::vector<DiscoveredChatLine> chat{};
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

        /**
         * What the guest has chosen for its own seat, as it announced it:
         * "ARM" or "CORE", a colour, a team (-1 for none) and whether it is
         * ready. The host decides what to do with them (see applyClaimChoice).
         */
        std::string side{"ARM"};
        unsigned int colour{0};
        int team{-1};
        bool ready{false};

        /** The guest's most recent lines, by its own sequence numbers. */
        std::vector<DiscoveredChatLine> chat;
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

    /**
     * What a guest wants for its own seat: the three things a person in a
     * seat chooses, and whether they are ready.
     */
    struct GuestChoice
    {
        MainMenuModel::PlayerSettings::Side side{MainMenuModel::PlayerSettings::Side::Arm};
        PlayerColorIndex colour{0};
        std::optional<int> team;
        bool ready{false};

        bool operator==(const GuestChoice& rhs) const = default;
    };

    /**
     * The announcement that makes a claim. Without a `choice` the seat's
     * side, colour and team are the host's own for it, which is what a guest
     * that has not touched anything wants; with one they are the guest's,
     * and `chat` is the lines it wants said.
     */
    DiscoveredGame claimAnnouncement(
        const DiscoveredGame& host,
        int seat,
        const std::string& token,
        const std::string& name,
        unsigned int gamePort,
        const std::optional<GuestChoice>& choice = std::nullopt,
        const std::vector<DiscoveredChatLine>& chat = {},
        const std::string& reachableAt = {});

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

    // ---- the battleroom: what each person may touch ----

    enum class RoomRole
    {
        /** Not in a room. */
        None,
        Host,
        Guest
    };

    /** Which of a seat's controls somebody in a given role may use. */
    struct SeatControls
    {
        /** The name button: the host opens a seat, or makes it a computer, or removes a guest. */
        bool name{false};
        bool side{false};
        bool colour{false};
        bool team{false};
        bool ready{false};
    };

    /**
     * What `role` may do to the seat of type `type`; `ownSeat` is whether the
     * seat is the one this machine plays. The host chooses for itself and for
     * the computers, and may open a seat or remove a guest from it; a guest
     * chooses for itself and nothing else, and says for itself that it is
     * ready. Nobody chooses a side, colour or team for another person.
     */
    SeatControls seatControls(RoomRole role, MainMenuModel::PlayerSettings::Type type, bool ownSeat);

    /**
     * Why the host cannot start, in the sentence the help line says; nothing
     * when it can. `ready` is per seat, as in LobbyIdentity. A game with no
     * map has nothing to start, a seat held by a person who has not said
     * they are ready is a player the others would be left waiting for, and a
     * room with nobody else in it is not a network game.
     */
    std::optional<std::string> startBlocker(const MainMenuModel& model, const std::vector<bool>& ready, bool mapChosen);

    /**
     * Applies what a guest chose for its seat to the host's model. Side and
     * team are taken as they are; a colour is taken only if no other seat that
     * is in the game holds it, and is otherwise refused -- the seat keeps the
     * colour it had, and the announcement that follows tells the guest so.
     * Returns whether anything of the seat changed.
     */
    bool applyClaimChoice(MainMenuModel& model, std::size_t seat, const SeatClaim& claim);

    /** Whether any seat other than `seat` that is in the game has this colour. */
    bool colourHeldByAnotherSeat(const MainMenuModel& model, std::size_t seat, const PlayerColorIndex& colour);

    /**
     * Gives a seat a colour no other seat in the game holds, if the one it has
     * is held. For a seat that has just come into the game -- an open one a
     * computer takes, or a claim that is seated.
     */
    void ensureFreeColour(MainMenuModel& model, std::size_t seat);

    /**
     * A message from a seat's control, `PLAYER3` or `PLAYER3_side`, as its seat
     * and the rest of the name (empty for the name button). Nothing for a name
     * that is not one, or a seat past the table.
     */
    struct SeatMessage
    {
        std::size_t seat;
        std::string control;
    };
    std::optional<SeatMessage> parseSeatMessage(const std::string& name, std::size_t seatCount);

    // ---- the battleroom: the game-wide options ----

    /**
     * The battleroom's option buttons read in the stage order LOUNGE2.GUI gives
     * them, which is not the order the setup screen's buttons have: the
     * commander button there is `Continues|Game ends` and here the model's
     * stage 0 is "game ends". So a stage is turned into the model's by its
     * label, here, and nowhere by its position.
     *
     * `optionName` is the name optionsFromModel uses (commander, startloc,
     * mapping, los). `labels` are the button's own captions, left to right.
     * A label this build has no meaning for -- the battleroom's `Deathmatch`
     * -- maps to nothing, and the screen builds the button without it.
     */
    std::optional<unsigned int> optionStageForLabel(const std::string& optionName, const std::string& label);
    std::optional<unsigned int> optionStageOfButtonStage(const std::string& optionName, const std::vector<std::string>& labels, unsigned int buttonStage);
    std::optional<unsigned int> buttonStageOfOptionStage(const std::string& optionName, const std::vector<std::string>& labels, unsigned int optionStage);

    /** The labels that mean something, in the button's order. */
    std::vector<std::string> usableOptionLabels(const std::string& optionName, const std::vector<std::string>& labels);

    /** What a click on the button does: the option's next stage, in the button's own order. */
    std::optional<unsigned int> nextOptionStage(const std::string& optionName, const std::vector<std::string>& labels, unsigned int currentOptionStage);

    /** The four buttons of the right-hand column that are options, by gadget name. */
    struct BattleroomOption
    {
        const char* button;
        const char* option;
    };
    const std::vector<BattleroomOption>& battleroomOptions();

    /**
     * The slider's stops for starting metal and energy, the same ladder the
     * setup screen's plus and minus walk: 200, 500, then steps of 500 to
     * 10000. The index of the stop nearest a value, a stop's value, and the
     * knob position of a stop.
     */
    constexpr unsigned int resourceStopCount = 21;
    unsigned int resourceStopValue(unsigned int stop);
    unsigned int resourceStopNearest(float value);
    float resourceStopPercent(unsigned int stop);
    unsigned int resourceStopAtPercent(float percent);

    // ---- the battleroom: chat ----

    /**
     * What can be said: control characters dropped, outer spaces trimmed, and
     * at most what a line of chat carries on the wire. Nothing for a line that
     * has nothing in it, which is not sent.
     */
    std::string sanitisedChatText(const std::string& text);

    /**
     * The room's transcript, kept the same way on every machine. The host
     * assigns each line the next sequence number and every other machine
     * appends the lines of the host's announcements that are newer than the
     * newest it has, so a guest's own line shows once the host has taken it
     * and in the same place for everyone.
     */
    struct ChatTranscript
    {
        static constexpr std::size_t maxKept = 200;

        std::vector<DiscoveredChatLine> lines;
        unsigned int lastSequence{0};

        /** Host: a line from `name` ("" for the room itself). False if there was nothing to say. */
        bool say(const std::string& name, const std::string& text);

        /** The newest lines an announcement can carry. */
        std::vector<DiscoveredChatLine> tail() const;

        /** Appends the lines of an announced tail that are newer than the transcript; how many. */
        std::size_t merge(const std::vector<DiscoveredChatLine>& announced);
    };

    /**
     * Host: says the lines of a guest's claim that have not been taken yet,
     * as `speaker`, in the guest's order. `lastTaken` is the newest sequence
     * of that guest already said, and moves up. A line is told from one
     * already taken by its sequence alone, so a claim repeated in every
     * announcement is said once.
     */
    std::size_t takeClaimChat(ChatTranscript& room, unsigned int& lastTaken, const std::string& speaker, const std::vector<DiscoveredChatLine>& claimed);

    /** `name: text`, or just the text of a line from the room. */
    std::string chatLineText(const DiscoveredChatLine& line);

    /**
     * A line broken to rows no wider than `maxWidth` by `width`, at spaces
     * where there is one, and a word longer than a row broken where it must.
     * Rows after the first are indented two spaces, so they read as one line.
     */
    std::vector<std::string> wrapChatLine(const std::string& text, float maxWidth, const std::function<float(const std::string&)>& width);

    // ---- addresses ----

    /**
     * The host half of a `host:port` as LanDiscovery reports a sender, with
     * an IPv6 address's brackets kept: `[::1]:1338` gives `[::1]`.
     */
    std::string hostPartOf(const std::string& sourceAddress);

    /** `hostPartOf(source)` and a game port, written the way a --player address is. */
    std::string gameAddress(const std::string& sourceAddress, unsigned int port);

    /**
     * Where the machine that sent `announcement` plays: the game address it
     * announced if `believeAnnounced` and that address is the announcement's
     * own machine's, otherwise the announcement's source address and port.
     *
     * Only the machines that have gone to the trouble of finding out their
     * outside address announce one, and a reader that is itself reaching the
     * other across routers is the only one that should use it: on a LAN the
     * source address is the better answer, and a router that will not send a
     * machine's own packets back to it makes the outside one a worse one.
     * The host half has to be the same machine's -- an announcement that
     * points a game's packets at somebody else's address is somebody using
     * a lobby to aim traffic -- so a reflexive address with another host
     * part is not believed.
     */
    std::string announcedGameAddress(const DiscoveredGame& announcement, bool believeAnnounced);

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

    /**
     * The art the battleroom's rows draw from, held by the flow so that the
     * scene can set a seat's colour and team each update without asking the
     * texture service for them again.
     */
    struct BattleroomArt
    {
        /** The ten team colours' logos at the 16 pixels a row has room for. */
        std::vector<std::shared_ptr<Sprite>> logos;
        /** `ally icons`: a pair of faces per team, then two blanks. */
        std::vector<std::shared_ptr<Sprite>> allyIcons;
    };

    /** The step between seat rows, in LOUNGE2.GUI's pixels. */
    constexpr int battleroomRowPitch = 20;

    /**
     * LOUNGE2.GUI over the bitmap that goes with the layout the data set has,
     * built for `role`: ten seat rows cloned from the file's per-seat
     * templates, the options this engine has a meaning for, and nothing of
     * the rest. A guest's panel has no Start button.
     */
    std::unique_ptr<UiPanel> makeBattleroomPanel(UiFactory& factory, TextureService& textures, RoomRole role, BattleroomArt& art);

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

        // -- reaching a machine on another network --
        /**
         * Finds this machine's outside addresses and keeps a way open to the
         * peers. Declared after `discovery`, which it uses and which has to
         * outlive it. Idle until somebody asks for an invite or joins by one:
         * a LAN game never starts it.
         */
        NatTraversal nat{discovery};

        /** Guest: the address came from an invite code, so the host is across a router. */
        bool invited{false};
        /** Guest: this machine's own code, to be sent back to the host once it is known. */
        std::string replyCode;
        /** Host: the invite was asked for and is waiting on the lookup. */
        bool inviteRequested{false};
        /** Host: the discovery address of each guest whose reply code was pasted. */
        std::vector<std::string> invitedGuests;
        /** The punch targets last handed to `nat`, so only a change is sent. */
        std::vector<std::string> punchTargetsGiven;
        /** Host: the addresses whose first datagram has been said to the room. */
        std::vector<std::string> punchReported;

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

        /** What the host has heard of each guest, by claim token, so that only a change is acted on. */
        struct ClaimMemory
        {
            std::string side;
            unsigned int colour{0};
            int team{-1};
            unsigned int lastChat{0};
        };
        std::map<std::string, ClaimMemory> claimMemory;

        /** Guests the host has removed. Their claims go on until they notice, and must not seat them again. */
        std::vector<std::string> removedTokens;

        // -- joined --
        std::string hostSession;
        std::string claimToken;
        int mySeat{-1};
        bool accepted{false};
        bool startRequested{false};
        Timestamp claimedAt{};
        Timestamp hostSeenAt{};

        /**
         * What this guest has asked for, kept until the host's announcement
         * says the same or two seconds go by, so that a click does not
         * flicker back to the old value for the round trip it takes to be
         * heard. `ready` is the guest's own and never pending.
         */
        GuestChoice wanted;
        bool choicePending{false};
        Timestamp choiceAt{};
        bool myReady{false};

        /** The guest's own lines, newest last, by its own sequence numbers. */
        std::vector<DiscoveredChatLine> sentChat;
        unsigned int nextChatSequence{1};

        // -- the room, either role --
        /** Host: what each claim said about being ready. Guest: what the host announced. */
        std::vector<bool> seatReady = std::vector<bool>(10);
        ChatTranscript chat;
        /** The newest line the OUTPUT box is showing, so that it is refilled only when something is new. */
        unsigned int shownChatSequence{0};
        BattleroomArt art;
    };
}
