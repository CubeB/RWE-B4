#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <rwe/game/SceneTime.h>
#include <rwe/sim/GameHash.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <utility>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * What a peer knows about a desync at the moment it notices one.
     *
     * The tick named here is the FIRST one the peers disagreed on, not the one
     * the disagreement was noticed on. Those are different ticks: a peer's sync
     * hashes arrive a round trip late, so by the time the two streams can be
     * compared at tick T the local simulation is already some way past it. The
     * streams are compared a tick at a time in tick order, and the comparison
     * stops at the first round that disagrees, which is what makes the tick the
     * first divergent one rather than merely the current one.
     *
     * Every peer compares the same ordered streams and so names the same tick.
     * That is the property that makes two peers' dumps worth putting side by
     * side: they are not the same tick of simulation -- each peer dumps where it
     * had got to -- but they agree on which tick went wrong, and on what every
     * peer thought the state was there.
     */
    struct DesyncReport
    {
        /** The first tick the peers' sync hashes disagreed on. */
        SceneTime tick;

        /** Every peer's sync hash for that tick, in player order. */
        std::vector<std::pair<PlayerId, GameHash>> hashes;
    };

    /**
     * The report as a person reads it: the log line, and the text of the error
     * box. `detectedAt` is the local scene time the mismatch surfaced at, which
     * says how far behind the divergence the dump was taken.
     */
    std::string describeDesync(
        const DesyncReport& report,
        PlayerId localPlayer,
        SceneTime detectedAt,
        const std::optional<std::filesystem::path>& dumpPath);

    /**
     * Where this peer's dump belongs: one file per player per divergent tick,
     * named so that the files collected from every peer sort together and say
     * which is which without being opened.
     */
    std::filesystem::path desyncDumpPath(
        const std::filesystem::path& directory,
        const DesyncReport& report,
        PlayerId localPlayer);

    /**
     * The dump a bug report carries: what disagreed, then the hashed simulation
     * state as `dump_util` writes it.
     *
     * The state is deliberately the hash-shaped dump rather than the save, so
     * that every field in it is one the sync hash read -- a diff between two
     * peers' dumps is then a list of candidates for the mismatch and nothing
     * else. The save is the wider net and the better lead when the divergence
     * is older than the hash says (CLAUDE.md, "Determinism"), and `--state-log`
     * and RWE_STATE_DUMP are how to cast it.
     */
    nlohmann::json desyncDumpJson(
        const DesyncReport& report,
        PlayerId localPlayer,
        SceneTime detectedAt,
        const GameSimulation& simulation);

    /**
     * Writes the dump beside the log, in the local data path, and says where it
     * went. Returns nothing if there was nowhere to write it or the write
     * failed: a desync that cannot be dumped is still a desync worth reporting,
     * so this never throws.
     */
    std::optional<std::filesystem::path> writeDesyncDump(
        const DesyncReport& report,
        PlayerId localPlayer,
        SceneTime detectedAt,
        const GameSimulation& simulation);
}
