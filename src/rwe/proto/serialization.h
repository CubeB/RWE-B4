#pragma once

#include <network.pb.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/UnitFireOrders.h>
#include <vector>

namespace rwe
{
    void serializeVector(const SimVector& v, proto::SimVector& out);

    proto::PlayerUnitCommand::IssueOrder::IssueKind serializeIssueKind(const PlayerUnitCommand::IssueOrder::IssueKind& kind);

    proto::PlayerUnitCommand::SetFireOrders::FireOrders serializeFireOrders(const UnitFireOrders& orders);

    void serializePlayerCommand(const PlayerCommand& command, proto::PlayerCommand& out);

    std::vector<PlayerCommand> deserializeCommandSet(const proto::GameUpdateMessage_PlayerCommandSet& set);

    /**
     * How many commands from the front of `commands` make a set no bigger
     * than `byteBudget` on the wire, never fewer than one while there are
     * any. A set is a tick's worth and cannot be split across packets, so a
     * tick with more than fit is sent over several: an order to a hundred
     * units is three kilobytes, twice what a datagram carries. Issue #75.
     */
    std::size_t commandsFittingOneSet(const std::vector<PlayerCommand>& commands, std::size_t byteBudget);

    PlayerCommand deserializeCommand(const proto::PlayerCommand& cmd);

    PlayerUnitCommand deserializeUnitCommand(const proto::PlayerUnitCommand& cmd);

    PlayerUnitCommand::IssueOrder deserializeIssueOrder(const proto::PlayerUnitCommand::IssueOrder& cmd);

    PlayerUnitCommand::IssueOrder::IssueKind deserializeIssueKind(const proto::PlayerUnitCommand::IssueOrder::IssueKind& kind);

    UnitFireOrders deserializeFireOrders(const proto::PlayerUnitCommand::SetFireOrders::FireOrders& orders);

    UnitOrder deserializeUnitOrder(const proto::PlayerUnitCommand::IssueOrder& cmd);

    SimVector deserializeVector(const proto::SimVector& v);
}
