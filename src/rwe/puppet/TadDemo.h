#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <rwe/game/GameParameters.h>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_events.h>

namespace rwe
{
    /** What a first pass over a demo reads: the header, players, unit table and senders. */
    struct TadDemoMeta
    {
        std::optional<TadHeader> header;
        std::vector<TadPlayer> players;
        std::optional<TadUnitTable> unitTable;

        /**
         * Every distinct packet sender, in first-seen order. The sender is a
         * stable per-player key but its numbering is not: RWE numbers senders
         * from zero and TA's recorder from one, and the player table's
         * `number` field matches neither reliably, so a driver is handed a
         * seat per sender in the order they appear rather than guessing.
         */
        std::vector<uint8_t> senders;
    };

    /** Reads the header, players, unit table and sender list, or nothing on a framing error. */
    std::optional<TadDemoMeta> readTadDemoMeta(const std::string& path);

    /**
     * Game parameters for spectating a demo: the map it names, every player a
     * Network seat and no local one, so nobody on this machine decides
     * anything. Nothing connects to those seats -- a demo has no peers -- and
     * `tadDemoFile` is what says so. Returns nothing when the demo seats more
     * players than the engine holds.
     */
    std::optional<GameParameters> gameParametersForDemo(const TadDemoMeta& meta, const std::string& demoPath);
}
