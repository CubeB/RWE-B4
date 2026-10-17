#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * A slot on the multiplayer setup screen, as far as starting a game is
     * concerned: whether it is somebody on the other end of a wire, and the
     * address typed for them if so.
     */
    struct MultiplayerSlot
    {
        bool isLocalHuman{false};
        bool isNetwork{false};
        std::string address;
    };

    /**
     * What is wrong with a direct-connect setup, or nothing if it is ready to
     * play. The screen says this and stays put rather than starting a game that
     * cannot run.
     *
     * A direct-connect game has no lobby to agree anything, so every peer types
     * the same screen on its own machine with its own slot set to Player and
     * everybody else set to Network. The checks here are the ones whose failure
     * the engine would otherwise meet much later and much less clearly: without
     * a local player the loader throws "No local player!", with two it throws
     * "Multiple local human players found", and an address that does not parse
     * reaches the resolver as a hostname nobody has.
     *
     * What it deliberately does not check is that both peers chose the same map,
     * the same options and the same starting resources. Nothing here can know
     * that, and the first tick's sync hash is what does know it.
     */
    std::optional<std::string> multiplayerSetupProblem(
        const std::vector<MultiplayerSlot>& slots,
        const std::string& localPort);

    /**
     * Whether a port is one this machine can listen on: a number, and inside
     * the range a port has. Ports below 1024 are left alone rather than
     * refused -- they need privilege on most systems, but that is the system's
     * business and a player who asks for one on purpose is entitled to it.
     */
    bool isUsablePort(const std::string& port);

    /**
     * One occupied-or-open seat on a setup screen, as far as the team rules are
     * concerned. `team` is the allegiance group; none means the seat is on no
     * team (the original's team 5), which is nobody's ally, another such seat
     * included.
     */
    struct SetupSeat
    {
        enum class Kind
        {
            Open,
            Human,
            Computer,
            Network,
        };

        Kind kind{Kind::Open};
        std::optional<int> team;
    };

    /**
     * The original's fourth launch check (BT-194): the occupied seats must not
     * all fall in one allegiance group, or the game is won on the first tick.
     * Seats on no team are each their own group. Both the skirmish Start button
     * and the multiplayer battle room make it.
     */
    std::optional<std::string> allOnOneTeamProblem(const std::vector<SetupSeat>& seats);

    /**
     * The skirmish Start button's own launch checks (BT-194), in the original's
     * order: at least one computer and at least one human seat, then the team
     * check. The map and start-position checks need the map and are made by
     * the scene.
     */
    std::optional<std::string> skirmishSetupProblem(const std::vector<SetupSeat>& seats);
}
