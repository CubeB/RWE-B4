#pragma once

#include <memory>
#include <optional>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/ai/AiController.h>
#include <rwe/ai/AiEngine.h>
#include <rwe/ai/AiPersonality.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/MapIntel.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    class AbstractVirtualFileSystem;

    /**
     * Which engine plays a seat, in the order ADR-0005 gives: its
     * personality's engine when the personality was found, else the engine a
     * saved game recorded for it, else the game's default. The three are
     * separate arguments rather than one resolved answer so that the order is
     * the one place it can be got wrong, and is tested as such.
     */
    AiEngine chooseAiEngine(const std::optional<AiPersonality>& personality, std::optional<AiEngine> savedEngine, AiEngine defaultEngine);

    /** What readClassicAiProfile found, and where. */
    struct ClassicAiProfileFile
    {
        /** The profile's text, or empty when neither file exists. */
        std::string text;
        /** The path it was read from, or empty when nothing was. */
        std::string path;
    };

    /**
     * The profile text the original's computer player reads: the mission's
     * `aiprofile` as `ai/<name>.txt`, otherwise `ai/default.txt`. Names are
     * the virtual file system's to compare, which it does case-insensitively.
     * Empty when neither exists, which the original plays with every
     * multiplier at 100 and no limits.
     */
    ClassicAiProfileFile readClassicAiProfile(const AbstractVirtualFileSystem& vfs, const std::string& profileName);

    /** Everything one computer seat's controller is built from. */
    struct AiSeatRequest
    {
        PlayerId playerId{0u};
        /** The player's faction, for the RWE engine's faction defaults. */
        std::string side;
        /** The personality chosen for the seat, if it named one that exists. */
        std::optional<AiPersonality> personality;
        /** What a saved game recorded for the seat. */
        std::optional<AiEngine> savedEngine;
        /** GameParameters::aiEngine: for a seat with neither of the above. */
        AiEngine defaultEngine{AiEngine::Classic};
        /** The game's difficulty, used unless the personality names one. */
        AiDifficulty gameDifficulty{AiDifficulty::Standard};
        /** --ai-tune entries, each "<player>:<knob>=<value>"; the ones for other players are skipped. */
        std::vector<std::string> tuning;
        /** A replay without shadow AI: construct, draw the seed, and do nothing. */
        bool idle{false};
        /** Where a classic seat's profile is read from; may be null, which reads as no file. */
        const AbstractVirtualFileSystem* vfs{nullptr};
        /** The mission's `aiprofile`, "DEFAULT" for a skirmish map. */
        std::string aiProfileName{"DEFAULT"};
    };

    /**
     * Builds the controller for one computer seat and draws exactly one value
     * from `simulation.rng` for it, whichever engine the seat plays. That
     * draw is not the engine's business: the start positions are dealt from
     * the same stream a few lines after the seats are built, so a seat that
     * drew nothing, or twice, would put every commander somewhere else --
     * and a replay, which idles its computer players, depends on it.
     *
     * Throws std::runtime_error for a malformed --ai-tune entry, for one that
     * does not name a knob, and for one aimed at a seat the classic engine
     * plays, which has no knobs to set.
     */
    std::unique_ptr<AiController> createAiController(
        GameSimulation& simulation,
        const AiSeatRequest& request,
        const MapIntel& mapIntel,
        const AiBuildTree& buildTree);
}
