#pragma once

#include <rwe/game/SceneTime.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitMovementOrders.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/UnitOrder.h>
#include <optional>
#include <variant>

namespace rwe
{
    struct PlayerUnitCommand
    {
        struct IssueOrder
        {
            enum IssueKind
            {
                Immediate,
                Queued
            };
            UnitOrder order;
            IssueKind issueKind;

            IssueOrder(const UnitOrder& order, IssueKind issueKind) : order(order), issueKind(issueKind)
            {
            }
        };

        struct ModifyBuildQueue
        {
            int count;
            std::string unitType;
        };

        /**
         * Add to (or take off) the number of rounds a stockpiled weapon is
         * building. The original queues these as ordinary orders on the unit
         * and shows the outstanding count next to the ready one as "N +M"
         * (0x419A2B).
         */
        struct ModifyStockpile
        {
            int count;
        };

        struct Stop
        {
        };

        struct SetMovementOrders
        {
            UnitMovementOrders orders;
        };

        struct SetFireOrders
        {
            UnitFireOrders orders;
        };

        struct SetOnOff
        {
            bool on;
        };

        /**
         * Ask for or drop the cloak. Whether the unit actually cloaks is still
         * settled a second at a time by the energy and by whether an enemy is
         * standing too close.
         */
        struct SetCloak
        {
            bool cloaked;
        };

        struct SelfDestruct
        {
        };

        /** Remove the queued build order whose footprint covers position. */
        struct CancelBuildOrder
        {
            SimVector position;
        };

        /**
         * Put each item a factory finishes back on the end of its own build
         * queue instead of consuming it, so one queued unit type loops.
         *
         * A command rather than a field the panel writes, because a toggle a
         * player clicks is state two machines have to agree about: a factory
         * that loops and one that does not build different units from the
         * same queue. It rides the ordinary per-tick command stream like every
         * other order, so it is applied on the same tick on every peer (#417).
         *
         * The setting is per unit rather than a game-wide option, so it needs
         * no replay-header change: the command that sets it is in the command
         * stream, and the flag it lands on is hashed and saved like the rest
         * of the unit.
         */
        struct SetBuildQueueRepeat
        {
            bool repeat;
        };

        using Command = std::variant<IssueOrder, ModifyBuildQueue, ModifyStockpile, Stop, SetFireOrders, SetMovementOrders, SetOnOff, SetCloak, SelfDestruct, CancelBuildOrder, SetBuildQueueRepeat>;

        UnitId unit;
        Command command;

        PlayerUnitCommand(const UnitId& unit, const Command& command) : unit(unit), command(command)
        {
        }
    };

    struct PlayerPauseGameCommand
    {
    };

    struct PlayerUnpauseGameCommand
    {
    };

    struct PlayerSetGameSpeedCommand
    {
        int speedIndex;
    };

    /**
     * A peer has stopped answering and the rest of the game is carrying on
     * without it. Issued by one player only -- see droppingPlayerFor -- and
     * sent through the ordinary command stream, so it arrives reliably and in
     * order like anything else.
     *
     * Unlike every other command it is applied the moment it arrives rather
     * than when it is popped, because what it unblocks IS the pop: the tick
     * cannot advance while the lost peer's buffer is empty, and the command
     * sits behind that same wall. That is safe because what it says is
     * absolute rather than relative -- `fromTick` names a tick, not "now" --
     * so applying it early, late or twice all come to the same thing.
     */
    struct PlayerDroppedCommand
    {
        /** The player who has stopped answering. */
        PlayerId player;

        /**
         * The first tick their commands are taken as empty. Everything they
         * sent from here on is discarded and everything missing up to here is
         * filled in, so that every peer cuts their stream in the same place
         * however much of it each happened to receive.
         */
        unsigned int fromTick;
    };

    /**
     * A dropped peer is coming back, and its stream reopens at `fromTick`.
     *
     * The inverse of PlayerDroppedCommand and issued by the same peer, by the
     * same rule -- droppingPlayerFor names whoever would declare that player
     * lost, and the player in question being dropped is exactly the state that
     * rule was written for, so both ends of the round trip are decided by one
     * peer without a message being sent about which.
     *
     * Applied on arrival like the drop, and for the mirror reason: what it
     * says is a tick rather than a moment, so early, late and twice all come
     * to the same thing. Unlike the drop it makes the tick *harder* to reach
     * rather than easier -- from `fromTick` the game needs that player's
     * commands again -- so every peer stalls at `fromTick` until the returning
     * one has caught up and started sending. That stall is the design and not
     * an accident: it is what makes the rejoin a lockstep event rather than a
     * race between a catch-up and a running game. See GameScene_rejoin.cpp.
     */
    struct PlayerRejoinedCommand
    {
        /** The player coming back. */
        PlayerId player;

        /**
         * The first tick their commands are taken from them again. Everything
         * below it stays empty, exactly as the rest of the game simulated it
         * while they were gone.
         */
        unsigned int fromTick;
    };

    /**
     * One resource-sharing action by the issuing player: a toggle, a reserve,
     * or a gift (TOTALA-EXE-COMMANDS.md section 124).
     *
     * One alternative with a kind rather than five, because every alternative
     * added to PlayerCommand costs GameSimulation.cpp hundreds of COFF
     * sections. The chat bar's `+ShareMetal`, `+ShareEnergy`, `+SetShareMetal`,
     * `+SetShareEnergy` and `+Give` each become exactly one of these, and
     * `+ShareAll` becomes two.
     *
     * The issuer is the player the command arrives under, as for every other
     * command, never a field of the command: a peer can only give away its
     * own stock. It is applied by applyPlayerShareCommand (sim/ResourceSharing.h)
     * so that a live game, a replay and the headless arena agree.
     */
    struct PlayerShareCommand
    {
        enum class Kind
        {
            /** Flip the player's metal-sharing flag. */
            ToggleMetal,
            /** Flip the player's energy-sharing flag. */
            ToggleEnergy,
            /** Set the metal reserve to min(storage, amount). Negative amounts are accepted. */
            SetMetalReserve,
            /** Set the energy reserve to min(storage, amount). Negative amounts are accepted. */
            SetEnergyReserve,
            /** Hand `amount` of metal (giveMetal) or energy to `target`. */
            Give,
            /** Assign the target's metal stock outright (`+NoMetal`). */
            SetMetalStock,
            /** Assign the target's energy stock outright (`+NoEnergy`). */
            SetEnergyStock,
            /** +1000 metal and energy to `target` (`+ATM`). */
            AtmGrant,
        };

        Kind kind{Kind::ToggleMetal};

        /** The reserve to set, or the amount to give. Unused by the toggles. */
        float amount{0.0f};

        /** Give only: the recipient. */
        std::optional<PlayerId> target;

        /** Give only: metal if true, energy if false. */
        bool giveMetal{true};

        /** SetMetalReserve/SetEnergyReserve: `amount` is a percentage of storage, not an absolute value. */
        bool percent{false};

        // NoMetal/NoEnergy/ATM reuse `target` and `amount`: the stock being
        // assigned (or the grant's recipient) and the value.
    };

    using PlayerCommand = std::variant<PlayerUnitCommand, PlayerPauseGameCommand, PlayerUnpauseGameCommand, PlayerSetGameSpeedCommand, PlayerDroppedCommand, PlayerRejoinedCommand, PlayerShareCommand>;
}
