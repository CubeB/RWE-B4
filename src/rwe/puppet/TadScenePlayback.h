#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    struct TadPuppetStats;
    struct TadChatLine;

    /**
     * A TA demo playing into a scene, one tick at a time.
     *
     * The driver owns no clock here: the file is read once into the driver's
     * per-tick queue, and the scene asks for a tick's records before it runs
     * that tick. That keeps playback on the scene's own clock, which the
     * speed control and pause act on, instead of letting the driver bury a
     * burst of ticks inside one packet. The demo's unit table is checked
     * against the loaded data set before a record is queued, and a mismatch
     * throws rather than placing units a type index does not name.
     */
    class TadScenePlayback
    {
    public:
        TadScenePlayback(GameSimulation& sim, const std::string& path, std::vector<std::string> unitLoadOrder);
        ~TadScenePlayback();

        TadScenePlayback(const TadScenePlayback&) = delete;
        TadScenePlayback& operator=(const TadScenePlayback&) = delete;

        /** Applies the records queued for this tick, then advances moving air goals. */
        void applyTick(uint32_t tick);

        std::optional<uint32_t> lastTick() const;

        const TadPuppetStats& stats() const;

        std::vector<TadChatLine> takeChat();

        /** The latest recorded 0x19 value, or nothing; see TadPuppetDriver. */
        std::optional<uint16_t> takeSpeedChange();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
