#include "PlayerCommandService.h"
#include <algorithm>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    std::optional<std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>>> PlayerCommandService::tryPopCommands()
    {
        std::scoped_lock<std::mutex> lock(mutex);

        for (const auto& p : commandBuffers)
        {
            if (p.second.empty() && !isDroppedLocked(p.first))
            {
                return std::nullopt;
            }
        }

        std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>> out;
        for (auto& p : commandBuffers)
        {
            if (p.second.empty())
            {
                // A dropped player, past the tick their stream was cut at.
                // They issue nothing for the rest of the game, and their units
                // are left standing where they are with the orders they had.
                out.emplace_back(p.first, std::vector<PlayerCommand>());
                continue;
            }

            out.emplace_back(p.first, p.second.front());
            p.second.pop_front();
        }

        ++poppedRounds;
        return out;
    }

    void PlayerCommandService::pushCommands(PlayerId player, const std::vector<PlayerCommand>& commands)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        if (isDroppedLocked(player))
        {
            // Their stream is closed. Packets can still arrive from a peer
            // that was declared lost -- a slow one that came back, or one
            // still in flight when the drop was agreed -- and taking them
            // would put commands after the cut on this peer and nowhere else.
            return;
        }

        commandBuffers.at(player).push_back(commands);
        ++pushedCount[player];

        // A drop is applied the moment it arrives rather than when it is
        // popped, because what it unblocks is the pop itself: the tick cannot
        // advance while the lost peer's buffer is empty, and a command waiting
        // to be popped is waiting behind that same wall. Applying it early is
        // safe because the cut it describes is a tick and not a moment.
        for (const auto& command : commands)
        {
            const auto* dropped = std::get_if<PlayerDroppedCommand>(&command);
            if (dropped == nullptr)
            {
                continue;
            }

            if (droppingPlayerForLocked(dropped->player) != player)
            {
                LOG_WARN << "Ignoring a drop of player " << dropped->player.value
                         << " issued by player " << player.value << ", who is not the one to issue it";
                continue;
            }

            dropPlayerLocked(dropped->player, dropped->fromTick);
        }

        for (const auto& command : commands)
        {
            const auto* rejoined = std::get_if<PlayerRejoinedCommand>(&command);
            if (rejoined == nullptr)
            {
                continue;
            }

            if (droppingPlayerForLocked(rejoined->player) != player)
            {
                LOG_WARN << "Ignoring a rejoin of player " << rejoined->player.value
                         << " issued by player " << player.value << ", who is not the one to issue it";
                continue;
            }

            rejoinPlayerLocked(rejoined->player, rejoined->fromTick);
        }
    }

    void PlayerCommandService::pushHash(PlayerId player, const GameHash& gameHash)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        // A dropped peer stops being a hash source, so its buffer is gone
        // rather than empty. Late hashes from it are simply not compared.
        auto it = gameTimeBuffers.find(player);
        if (it == gameTimeBuffers.end())
        {
            return;
        }

        it->second.push_back(gameHash);
    }

    void PlayerCommandService::registerPlayer(PlayerId playerId)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        auto result = commandBuffers.emplace(playerId, std::deque<std::vector<PlayerCommand>>());
        if (!result.second)
        {
            throw std::logic_error("Player already registered");
        }
    }

    void PlayerCommandService::registerHashSource(PlayerId playerId, SceneTime fromTick)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        auto result = gameTimeBuffers.emplace(playerId, std::deque<GameHash>());
        if (!result.second)
        {
            throw std::logic_error("Hash source already registered");
        }

        hashSourceFromTick[playerId] = fromTick;
    }

    unsigned int PlayerCommandService::bufferedCommandCount(PlayerId player) const
    {
        std::scoped_lock<std::mutex> lock(mutex);

        return commandBuffers.at(player).size();
    }

    std::optional<DesyncReport> PlayerCommandService::checkHashes()
    {
        std::scoped_lock<std::mutex> lock(mutex);

        if (desync)
        {
            return desync;
        }

        // A source whose first hash belongs to a later tick than the one being
        // compared has not started yet: it is skipped rather than waited for,
        // or a rejoin agreed for tick 9000 would stall the comparison from now
        // until then.
        auto participating = [&](PlayerId playerId) {
            auto it = hashSourceFromTick.find(playerId);
            return it == hashSourceFromTick.end() || it->second <= nextHashTick;
        };

        while (!std::any_of(gameTimeBuffers.begin(), gameTimeBuffers.end(), [&](const auto& p) { return participating(p.first) && p.second.empty(); }))
        {
            std::vector<std::pair<PlayerId, GameHash>> round;
            round.reserve(gameTimeBuffers.size());
            for (auto& p : gameTimeBuffers)
            {
                if (!participating(p.first))
                {
                    continue;
                }

                round.emplace_back(p.first, p.second.front());
                p.second.pop_front();
            }

            if (round.empty())
            {
                // Nobody reports for this tick, for one of two reasons.
                //
                // Every source may have been dropped, and then there is
                // nothing to compare and nothing to advance towards. Or this
                // peer may have joined a game in progress, where every source
                // including its own starts at the rejoin tick while this
                // counter still stands at 1 -- and since it is only ever
                // advanced by a round being compared, it would stand there for
                // the rest of the game and no hash would ever be looked at.
                // Move it up to the first tick anybody reports; the ticks
                // skipped are ticks no source has, and were compared by the
                // peers that were there for them.
                std::optional<SceneTime> earliest;
                for (const auto& p : gameTimeBuffers)
                {
                    auto it = hashSourceFromTick.find(p.first);
                    if (it != hashSourceFromTick.end() && (!earliest || it->second < *earliest))
                    {
                        earliest = it->second;
                    }
                }

                if (earliest && *earliest > nextHashTick)
                {
                    nextHashTick = *earliest;
                    continue;
                }

                return std::nullopt;
            }

            // The buffers live in a hash table, so a round comes out in
            // whatever order the table happens to hold. A report is read by a
            // person and set beside another peer's, so put the players in
            // their own order before either happens.
            std::sort(round.begin(), round.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            auto matching = std::all_of(round.begin(), round.end(), [&](const auto& p) { return p.second == round.front().second; });

            if (!matching)
            {
                desync = DesyncReport{nextHashTick, std::move(round)};
                return desync;
            }

            nextHashTick += SceneTime(1);
        }

        return std::nullopt;
    }

    bool PlayerCommandService::needsCommandsForTick(PlayerId player, unsigned int tick) const
    {
        std::scoped_lock<std::mutex> lock(mutex);

        if (isDroppedLocked(player))
        {
            return false;
        }

        auto it = pushedCount.find(player);
        return (it == pushedCount.end() ? 0u : it->second) < tick;
    }

    void PlayerCommandService::rewindTo(unsigned int tick)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        for (auto& p : commandBuffers)
        {
            p.second.clear();
            pushedCount[p.first] = tick;
        }

        poppedRounds = tick;

        for (auto it = droppedFromTick.begin(); it != droppedFromTick.end();)
        {
            it = it->second >= tick ? droppedFromTick.erase(it) : std::next(it);
        }
    }

    std::vector<PlayerId> PlayerCommandService::playersNotReady() const
    {
        std::scoped_lock<std::mutex> lock(mutex);

        std::vector<PlayerId> out;
        for (const auto& p : commandBuffers)
        {
            if (p.second.empty() && !isDroppedLocked(p.first))
            {
                out.push_back(p.first);
            }
        }

        std::sort(out.begin(), out.end());
        return out;
    }

    void PlayerCommandService::dropPlayer(PlayerId player, unsigned int fromTick)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        dropPlayerLocked(player, fromTick);
    }

    void PlayerCommandService::rejoinPlayer(PlayerId player, unsigned int fromTick)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        rejoinPlayerLocked(player, fromTick);
    }

    bool PlayerCommandService::isDropped(PlayerId player) const
    {
        std::scoped_lock<std::mutex> lock(mutex);
        return isDroppedLocked(player);
    }

    std::optional<PlayerId> PlayerCommandService::droppingPlayerFor(PlayerId dropped) const
    {
        std::scoped_lock<std::mutex> lock(mutex);
        return droppingPlayerForLocked(dropped);
    }

    bool PlayerCommandService::isDroppedLocked(PlayerId player) const
    {
        return droppedFromTick.find(player) != droppedFromTick.end();
    }

    std::optional<PlayerId> PlayerCommandService::droppingPlayerForLocked(PlayerId dropped) const
    {
        // Over the hash sources rather than every player: a peer is exactly
        // something that runs its own simulation and reports a hash for it,
        // and a computer player -- which is every peer's own copy of the same
        // AI -- has no machine of its own to notice anything from.
        std::optional<PlayerId> best;
        for (const auto& p : gameTimeBuffers)
        {
            if (p.first == dropped || isDroppedLocked(p.first))
            {
                continue;
            }

            if (!best || p.first < *best)
            {
                best = p.first;
            }
        }

        return best;
    }

    void PlayerCommandService::rejoinPlayerLocked(PlayerId player, unsigned int fromTick)
    {
        if (!isDroppedLocked(player))
        {
            // Idempotent for the same reason the drop is: this arrives once
            // over the wire and again when the command carrying it is popped.
            // It is also what a rejoin naming somebody who never left does.
            return;
        }

        auto buffer = commandBuffers.find(player);
        if (buffer == commandBuffers.end())
        {
            LOG_ERROR << "Rejoin names player " << player.value << ", who is not in this game";
            return;
        }

        auto droppedAt = droppedFromTick.at(player);
        if (fromTick <= droppedAt)
        {
            // The stream would reopen at or before the tick it was cut at, so
            // there would be ticks the rest of the game ran as empty and this
            // player is about to fill in. Refuse rather than desync.
            LOG_ERROR << "Rejoin of player " << player.value << " at tick " << fromTick
                      << " is not after the tick their stream was cut at (" << droppedAt << ")";
            return;
        }

        if (fromTick <= poppedRounds)
        {
            // A tick the simulation has already run. Whoever issued this chose
            // it too low -- the same fault the drop's margin exists to prevent,
            // and the same refusal, because filling a tick that is in the past
            // is a desync rather than a recovery.
            LOG_ERROR << "Rejoin of player " << player.value << " at tick " << fromTick
                      << " is not in the future; the game has consumed " << poppedRounds << " ticks";
            return;
        }

        // Ticks up to fromTick-1 were empty for everyone and stay empty, and
        // the buffer holds only the ones still ahead of the game.
        //
        // Which is why the padding starts at whichever of the two counters is
        // further on. Below the cut the buffer still holds this player's real
        // sets and its size is pushed minus popped, as anyone's is; past the
        // cut it is empty and the game has gone on consuming ticks without
        // popping anything from it, so popped has overtaken pushed and the
        // difference is ticks that were never in the buffer at all. Padding
        // from pushed would then hand the ticks they missed to the ticks still
        // to come, and padding from popped would drop real commands they sent
        // before they went quiet.
        auto padFrom = std::max(pushedCount[player], poppedRounds);
        for (auto tick = padFrom + 1; tick < fromTick; ++tick)
        {
            buffer->second.push_back(std::vector<PlayerCommand>());
        }

        pushedCount[player] = fromTick - 1;

        droppedFromTick.erase(player);

        // A source again, from the tick they reopened at rather than from tick
        // 1: they have no hashes for the ticks they missed, and the peers that
        // stayed compared and discarded theirs long ago.
        gameTimeBuffers.emplace(player, std::deque<GameHash>());
        hashSourceFromTick[player] = SceneTime(fromTick);

        LOG_INFO << "Player " << player.value << " rejoined; their commands are needed again from tick " << fromTick;
    }

    void PlayerCommandService::dropPlayerLocked(PlayerId player, unsigned int fromTick)
    {
        if (isDroppedLocked(player))
        {
            // Idempotent on purpose: the same drop arrives once over the wire
            // and again when the command carrying it is popped.
            return;
        }

        auto buffer = commandBuffers.find(player);
        if (buffer == commandBuffers.end())
        {
            LOG_ERROR << "Drop names player " << player.value << ", who is not in this game";
            return;
        }

        droppedFromTick.emplace(player, fromTick);
        gameTimeBuffers.erase(player);
        hashSourceFromTick.erase(player);

        // Sets 1 to fromTick-1 are theirs; everything from fromTick on is
        // empty. Two peers can hold different amounts of a lost peer's stream,
        // so both ends have to be forced: discard what is past the cut, fill in
        // what is short of it.
        auto realSets = fromTick > 0 ? fromTick - 1 : 0;
        auto& pushed = pushedCount[player];

        while (pushed > realSets && !buffer->second.empty())
        {
            buffer->second.pop_back();
            --pushed;
        }

        if (pushed > realSets)
        {
            // Sets past the cut were not merely received but already run, so
            // this peer has simulated ticks that the others will not. The tick
            // was chosen too low, which the margin the issuer adds exists to
            // prevent; say so, because the desync is already in the past.
            LOG_ERROR << "Drop of player " << player.value << " at tick " << fromTick
                      << " comes after " << pushed << " of their sets have already been consumed";
        }

        while (pushed < realSets)
        {
            buffer->second.push_back(std::vector<PlayerCommand>());
            ++pushed;
        }

        LOG_INFO << "Player " << player.value << " dropped; their commands are empty from tick " << fromTick;
    }
}
