/**
 * tad_puppet -- play every demo in a corpus through the puppet driver,
 * headlessly, and report how far each puppet drifted from the position its
 * owner recorded.
 *
 * A demo is a stream of state and effects rather than orders, so it cannot be
 * fed to the simulation as commands. The driver spawns the units the stream
 * names, steers them along the replicated paths with RWE's own movement code,
 * and measures the error at each full-state record. That error is the
 * regression guard: it says how good the dead reckoning is before a live TA
 * has to depend on it.
 *
 *   tad_puppet --file <demo.tad> | --dir <dir> --data-path <p> [--data-path <p>...]
 *              [--json <out>]
 *
 * Exits non-zero if any demo fails to play, and refuses cleanly when the map
 * the header names is absent or the demo's unit table does not match the data
 * set loaded.
 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <rwe/ColorPalette.h>
#include <rwe/MeshService.h>
#include <rwe/PathMapping.h>
#include <rwe/game/GameParameters.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/puppet/TaLiveReceiver.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/sim/SimRandom.h>
#include <rwe/util.h>
#include <rwe/util/OpaqueArgs.h>
#include <rwe/util/SpanStream.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    rwe::PathMapping defaultPathMapping()
    {
        rwe::PathMapping m;
        m.ai = "ai";
        m.anims = "anims";
        m.bitmaps = "bitmaps";
        m.camps = "camps";
        m.downloads = "download";
        m.features = "features";
        m.fonts = "fonts";
        m.gamedata = "gamedata";
        m.guis = "guis";
        m.maps = "maps";
        m.objects3d = "objects3d";
        m.palettes = "palettes";
        m.scripts = "scripts";
        m.sounds = "sounds";
        m.textures = "textures";
        m.unitpics = "unitpics";
        m.units = "units";
        m.weapons = "weapons";
        return m;
    }

    bool isDemo(const fs::path& path)
    {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return extension == ".tad" || extension == ".ted";
    }

    /** What the first pass over a demo reads: the header, players and unit table. */
    struct DemoMeta
    {
        std::optional<rwe::TadHeader> header;
        std::vector<rwe::TadPlayer> players;
        std::optional<rwe::TadUnitTable> unitTable;

        /**
         * Every distinct packet sender, in first-seen order. The sender is a
         * stable per-player key but its numbering is not: RWE numbers senders
         * from zero and TA's own recorder from one, and the player table's
         * `number` field matches neither reliably, so the driver is handed a
         * seat per sender in the order they appear rather than guessing.
         */
        std::vector<uint8_t> senders;
    };

    struct MetaHandler : rwe::TadHandler
    {
        DemoMeta meta;

        void onHeader(const rwe::TadHeader& h) override { meta.header = h; }
        void onPlayer(const rwe::TadPlayer& p, unsigned int, unsigned int) override { meta.players.push_back(p); }
        void onUnitData(const rwe::TadBytes& d) override { meta.unitTable = rwe::tadDecodeUnitTable(d); }

        void onPacket(const rwe::TadPacket& packet, const std::vector<rwe::TadBytes>&, const rwe::TadWalkStats&) override
        {
            if (std::find(meta.senders.begin(), meta.senders.end(), packet.sender) == meta.senders.end())
            {
                meta.senders.push_back(packet.sender);
            }
        }
    };

    struct PlayHandler : rwe::TadHandler
    {
        rwe::TadPuppetDriver& driver;

        explicit PlayHandler(rwe::TadPuppetDriver& driver) : driver(driver) {}

        void onPacket(const rwe::TadPacket& packet, const std::vector<rwe::TadBytes>& subPackets, const rwe::TadWalkStats&) override
        {
            driver.onPacket(packet, subPackets);
        }
    };

    /**
     * The network `--live-sim` stands in for.
     *
     * A packet becomes available between 0 and `jitterTicks` ticks *before* the
     * tick its serial names, which is what a receive buffer is: our tick runs
     * that far behind the peer's clock, so a packet that crosses early is still
     * in hand when the tick that names it arrives. Reordering falls out of two
     * packets taking different leads, and nothing arrives after the tick it
     * names -- which is why a run at 0% loss has to reproduce the plain figures
     * exactly, and why the receiver's own lateness counter stays at zero here.
     * The lateness path is covered by the unit tests instead, since a live
     * clock running ahead of the peer's is the only way to reach it.
     */
    struct LiveSim
    {
        uint32_t jitterTicks{0};
        uint32_t lossPercent{0};
        uint32_t seed{0};

        uint64_t packetsScheduled{0};
        uint64_t packetsLost{0};
        uint32_t localTicks{0};

        rwe::TaLiveReceiverStats receiver;

        static std::optional<LiveSim> parse(const std::string& text);
    };

    struct DriftSummary
    {
        unsigned int count{0};
        double median{0.0};
        double p90{0.0};
        double max{0.0};
        double within32{0.0};
        double within128{0.0};
    };

    DriftSummary summarise(const rwe::TadPuppetDrift& drift)
    {
        DriftSummary s;
        s.count = static_cast<unsigned int>(drift.distances.size());
        if (drift.distances.empty())
        {
            return s;
        }

        auto sorted = drift.distances;
        std::sort(sorted.begin(), sorted.end());
        s.median = sorted[sorted.size() / 2];
        s.p90 = sorted[std::min(sorted.size() - 1, (sorted.size() * 9) / 10)];
        s.max = sorted.back();

        std::size_t within32 = 0;
        std::size_t within128 = 0;
        for (auto d : sorted)
        {
            if (d <= 32.0)
            {
                ++within32;
            }
            if (d <= 128.0)
            {
                ++within128;
            }
        }
        s.within32 = static_cast<double>(within32) / static_cast<double>(sorted.size());
        s.within128 = static_cast<double>(within128) / static_cast<double>(sorted.size());
        return s;
    }

    json driftJson(const DriftSummary& s)
    {
        return json{
            {"count", s.count},
            {"median", s.median},
            {"p90", s.p90},
            {"max", s.max},
            {"within32", s.within32},
            {"within128", s.within128}};
    }

    json statsJson(const rwe::TadPuppetStats& stats)
    {
        auto ground = summarise(stats.groundDrift);
        auto air = summarise(stats.airDrift);
        return json{
            {"packets", stats.packets},
            {"ticksPlayed", stats.ticksPlayed},
            {"unitsSpawned", stats.unitsSpawned},
            {"unitsFinished", stats.unitsFinished},
            {"unitsKilled", stats.unitsKilled},
            {"wrecksLeft", stats.wrecksLeft},
            {"unplacedUnits", stats.unplacedUnits},
            {"recordsDropped", {
                 {"badId", stats.recordsDroppedBadId},
                 {"badType", stats.recordsDroppedBadType},
                 {"badBlock", stats.recordsDroppedBadBlock},
                 {"unknownUnit", stats.recordsDroppedUnknownUnit},
                 {"spawnRefused", stats.spawnsRefused},
             }},
            {"shots", {{"spawned", stats.shotsSpawned}, {"dropped", stats.shotsDropped}}},
            {"scripts", {{"run", stats.scriptCallsRun}, {"dropped", stats.scriptCallsDropped}}},
            {"chat", {{"lines", stats.chatLines}, {"allyLines", stats.allyChatLines}}},
            {"speedChanges", stats.speedChanges},
            {"packetsWithoutClock", stats.packetsWithoutClock},
            {"drift", {{"ground", driftJson(ground)}, {"air", driftJson(air)}}}};
    }

    void printDrift(const std::string& label, const DriftSummary& s)
    {
        std::cout << "  drift " << label << ": n=" << s.count
                  << " median=" << s.median
                  << " p90=" << s.p90
                  << " max=" << s.max
                  << " within32=" << (s.within32 * 100.0) << "%"
                  << " within128=" << (s.within128 * 100.0) << "%\n";
    }

    void printStats(const rwe::TadPuppetStats& stats)
    {
        std::cout << "  packets " << stats.packets
                  << ", ticks " << stats.ticksPlayed
                  << ", spawned " << stats.unitsSpawned
                  << ", finished " << stats.unitsFinished
                  << ", killed " << stats.unitsKilled
                  << ", wrecks " << stats.wrecksLeft
                  << ", unplaced " << stats.unplacedUnits << "\n";
        std::cout << "  dropped: bad id " << stats.recordsDroppedBadId
                  << ", bad type " << stats.recordsDroppedBadType
                  << ", bad block " << stats.recordsDroppedBadBlock
                  << ", unknown unit " << stats.recordsDroppedUnknownUnit
                  << ", refused spawn " << stats.spawnsRefused
                  << ", no clock " << stats.packetsWithoutClock << "\n";
        std::cout << "  shots " << stats.shotsSpawned << " spawned, " << stats.shotsDropped << " dropped"
                  << "; scripts " << stats.scriptCallsRun << " run, " << stats.scriptCallsDropped << " dropped"
                  << "; chat " << stats.chatLines << " lines, " << stats.allyChatLines << " ally"
                  << "; speed " << stats.speedChanges << "\n";
        printDrift("ground", summarise(stats.groundDrift));
        printDrift("air", summarise(stats.airDrift));
    }

    json liveJson(const LiveSim& sim)
    {
        auto& r = sim.receiver;
        return json{
            {"jitterTicks", sim.jitterTicks},
            {"lossPercent", sim.lossPercent},
            {"seed", sim.seed},
            {"packetsScheduled", sim.packetsScheduled},
            {"packetsLost", sim.packetsLost},
            {"localTicks", sim.localTicks},
            {"received", r.packetsReceived},
            {"applied", r.packetsApplied},
            {"late", r.packetsLate},
            {"duplicate", r.packetsDuplicate},
            {"outOfOrder", r.packetsOutOfOrder},
            {"withoutSerial", r.packetsWithoutSerial},
            {"droppedOutOfRange", r.packetsDroppedOutOfRange},
            {"droppedBufferFull", r.packetsDroppedBufferFull},
            {"held", r.held},
            {"clock",
             {{"samples", r.clock.samples},
              {"originSerial", r.clock.originSerial},
              {"originCorrections", r.clock.originCorrections},
              {"minOffsetTicks", r.clock.minOffsetTicks},
              {"maxOffsetTicks", r.clock.maxOffsetTicks},
              {"meanOffsetTicks", r.clock.meanOffsetTicks},
              {"firstOffsetTicks", r.clock.firstOffsetTicks},
              {"lastOffsetTicks", r.clock.lastOffsetTicks},
              {"driftTicks", r.clock.driftTicks},
              {"driftPer1000Ticks", r.clock.driftPer1000Ticks}}}};
    }

    void printLive(const LiveSim& sim, uint32_t localTicks)
    {
        auto& r = sim.receiver;
        std::cout << "  live: " << sim.packetsScheduled << " scheduled, " << sim.packetsLost << " lost"
                  << " (receive buffer " << sim.jitterTicks << " ticks, " << sim.lossPercent << "% loss, seed " << sim.seed
                  << "), " << localTicks << " local ticks\n";
        std::cout << "  received " << r.packetsReceived << ", applied " << r.packetsApplied
                  << ", late " << r.packetsLate
                  << ", duplicate " << r.packetsDuplicate
                  << ", out of order " << r.packetsOutOfOrder
                  << ", no serial " << r.packetsWithoutSerial
                  << ", refused " << (r.packetsDroppedOutOfRange + r.packetsDroppedBufferFull)
                  << ", held " << r.held << "\n";
        std::cout << "  clock: anchor " << r.clock.originSerial.value_or(0)
                  << " (" << r.clock.originCorrections << " corrected)"
                  << ", offset min " << r.clock.minOffsetTicks
                  << " mean " << r.clock.meanOffsetTicks
                  << " max " << r.clock.maxOffsetTicks
                  << ", drift " << r.clock.driftTicks << " ticks"
                  << " (" << r.clock.driftPer1000Ticks << " per 1000)\n";
    }

    /** The loaded services, built once and reused for every demo. */
    struct DataSet
    {
        rwe::CompositeVirtualFileSystem vfs;
        rwe::PathMapping pathMapping;
        std::optional<rwe::ColorPalette> palette;
        std::optional<rwe::ColorPalette> guiPalette;
        rwe::MeshService meshService{&vfs, nullptr, {}, {}, {}};

        rwe::GameLoadServices services()
        {
            return rwe::GameLoadServices{
                &vfs,
                &pathMapping,
                palette ? &*palette : nullptr,
                guiPalette ? &*guiPalette : nullptr,
                nullptr,
                &meshService,
                nullptr,
                nullptr};
        }
    };

    /** Aggregates counts and drift across demos. */
    template <typename T>
    void addCounts(T& into, const T& from)
    {
        into += from;
    }

    struct Totals
    {
        rwe::TadPuppetStats stats;
        std::optional<LiveSim> live;
        unsigned int demos{0};
        unsigned int failed{0};
    };

    /** Sums a live run's own counters; the clock figures are per-demo only. */
    void addLive(LiveSim& into, const LiveSim& from)
    {
        addCounts(into.packetsScheduled, from.packetsScheduled);
        addCounts(into.packetsLost, from.packetsLost);
        addCounts(into.localTicks, from.localTicks);
        addCounts(into.receiver.packetsReceived, from.receiver.packetsReceived);
        addCounts(into.receiver.packetsApplied, from.receiver.packetsApplied);
        addCounts(into.receiver.packetsLate, from.receiver.packetsLate);
        addCounts(into.receiver.packetsDuplicate, from.receiver.packetsDuplicate);
        addCounts(into.receiver.packetsOutOfOrder, from.receiver.packetsOutOfOrder);
        addCounts(into.receiver.packetsWithoutSerial, from.receiver.packetsWithoutSerial);
        addCounts(into.receiver.packetsDroppedOutOfRange, from.receiver.packetsDroppedOutOfRange);
        addCounts(into.receiver.packetsDroppedBufferFull, from.receiver.packetsDroppedBufferFull);
        into.receiver.held = from.receiver.held;
    }

    void addStats(rwe::TadPuppetStats& into, const rwe::TadPuppetStats& from)
    {
        addCounts(into.packets, from.packets);
        addCounts(into.ticksPlayed, from.ticksPlayed);
        addCounts(into.unitsSpawned, from.unitsSpawned);
        addCounts(into.unitsFinished, from.unitsFinished);
        addCounts(into.unitsKilled, from.unitsKilled);
        addCounts(into.wrecksLeft, from.wrecksLeft);
        addCounts(into.unplacedUnits, from.unplacedUnits);
        addCounts(into.recordsDroppedBadId, from.recordsDroppedBadId);
        addCounts(into.recordsDroppedBadType, from.recordsDroppedBadType);
        addCounts(into.recordsDroppedBadBlock, from.recordsDroppedBadBlock);
        addCounts(into.recordsDroppedUnknownUnit, from.recordsDroppedUnknownUnit);
        addCounts(into.spawnsRefused, from.spawnsRefused);
        addCounts(into.packetsWithoutClock, from.packetsWithoutClock);
        addCounts(into.shotsSpawned, from.shotsSpawned);
        addCounts(into.shotsDropped, from.shotsDropped);
        addCounts(into.scriptCallsRun, from.scriptCallsRun);
        addCounts(into.scriptCallsDropped, from.scriptCallsDropped);
        addCounts(into.chatLines, from.chatLines);
        addCounts(into.allyChatLines, from.allyChatLines);
        addCounts(into.speedChanges, from.speedChanges);
        into.groundDrift.distances.insert(into.groundDrift.distances.end(), from.groundDrift.distances.begin(), from.groundDrift.distances.end());
        into.airDrift.distances.insert(into.airDrift.distances.end(), from.airDrift.distances.begin(), from.airDrift.distances.end());
        into.groundDrift.samples += from.groundDrift.samples;
        into.airDrift.samples += from.airDrift.samples;
    }

    std::optional<rwe::GameParameters> parametersFor(const rwe::TadHeader& header, const std::vector<rwe::TadPlayer>& players)
    {
        if (header.maxUnits == 0)
        {
            std::cerr << "  ERROR: the header says maxUnits 0\n";
            return std::nullopt;
        }

        rwe::GameParameters params(header.mapName, 0u);
        rwe::Index slot = 0;
        for (const auto& player : players)
        {
            if (player.isWatcher())
            {
                continue;
            }
            if (slot >= params.players.size())
            {
                std::cerr << "  ERROR: the demo names more players than the engine holds\n";
                return std::nullopt;
            }

            // One Human so the loader has a point of view; the rest Network,
            // which is neither a local player nor a computer one, so no AI is
            // built for a seat the driver is about to take over. Every seat is
            // marked Remote either way, before a packet is read.
            rwe::PlayerControllerType controller = slot == 0
                ? rwe::PlayerControllerType(rwe::PlayerControllerTypeHuman{})
                : rwe::PlayerControllerType(rwe::PlayerControllerTypeNetwork{});

            std::string side = static_cast<rwe::TadSide>(player.side) == rwe::TadSide::Core ? "CORE" : "ARM";
            params.players[slot] = rwe::PlayerInfo{
                std::optional<std::string>(player.name),
                controller,
                side,
                rwe::PlayerColorIndex(player.color),
                rwe::Metal(1000.0f),
                rwe::Energy(1000.0f)};
            ++slot;
        }

        return params;
    }

    /**
     * Runs `body` over a simulation loaded for the demo, with a driver over it
     * and a seat for every sender. The reason is on stderr and `body` is not
     * run if the map is missing or the data set does not match the demo.
     *
     * The loaded game is a local, built straight from what the loader returns.
     * A driver holds a reference into it, so it cannot be moved out of here and
     * passed back: the reference would be left pointing at the old one.
     */
    template <typename Body>
    void withPlayback(DataSet& data, const DemoMeta& meta, bool externalClock, Body&& body)
    {
        if (!meta.header)
        {
            std::cerr << "  ERROR: no header\n";
            return;
        }
        const auto& header = *meta.header;

        auto params = parametersFor(header, meta.players);
        if (!params)
        {
            return;
        }

        auto otaRaw = data.vfs.readFile("maps/" + header.mapName + ".ota");
        if (!otaRaw)
        {
            std::cerr << "  ERROR: map \"" << header.mapName << "\" is not in the data\n";
            return;
        }
        std::string otaStr(otaRaw->begin(), otaRaw->end());
        auto ota = rwe::parseOta(rwe::parseTdfFromString(otaStr));

        auto tntBytes = data.vfs.readFile("maps/" + header.mapName + ".tnt");
        if (!tntBytes)
        {
            std::cerr << "  ERROR: map terrain for \"" << header.mapName << "\" is not in the data\n";
            return;
        }
        rwe::SpanStream tntStream(tntBytes->data(), tntBytes->size());
        rwe::TntArchive tnt(&tntStream);
        auto mapData = rwe::readMapData(tnt, ota, params->schemaIndex);

        rwe::LoadedGame game = rwe::loadGameSimulation(data.services(), *params, std::move(mapData), ota);

        if (meta.unitTable && meta.unitTable->restricted.size() != game.dataMaps.unitLoadOrder.size())
        {
            std::cerr << "  ERROR: the demo's unit table has " << meta.unitTable->restricted.size()
                      << " types, the data set loads " << game.dataMaps.unitLoadOrder.size() << "\n";
            return;
        }

        rwe::TadPuppetDriver driver(game.simulation, header.maxUnits, game.dataMaps.unitLoadOrder);

        rwe::Index slot = 0;
        for (uint8_t sender : meta.senders)
        {
            if (slot >= game.simulation.players.size())
            {
                break;
            }
            driver.addPlayer(sender, rwe::PlayerId(static_cast<unsigned int>(slot)));
            ++slot;
        }

        driver.setExternalClock(externalClock);
        body(game.simulation, driver);
    }

    /** Plays a demo straight from the file, the way the tool has always done. */
    std::optional<rwe::TadPuppetStats> playDemo(const fs::path& path, DataSet& data, const DemoMeta& meta)
    {
        std::optional<rwe::TadPuppetStats> result;
        withPlayback(data, meta, false, [&](rwe::GameSimulation&, rwe::TadPuppetDriver& driver) {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                std::cerr << "  ERROR: cannot open\n";
                return;
            }

            PlayHandler handler(driver);
            rwe::readTad(stream, handler);
            result = driver.stats();
        });
        return result;
    }

    /** The figures a live run reports on top of the driver's own. */
    struct LiveResult
    {
        rwe::TadPuppetStats driver;
        LiveSim sim;
    };

    /**
     * Plays a demo as a live stream would arrive: every packet is delivered at
     * the tick the injected network says, through a TaLiveReceiver, with the
     * driver on its external clock and the simulation ticked once per local
     * tick. The local tick is the clock, as it is in a game.
     */
    std::optional<LiveSim> LiveSim::parse(const std::string& text)
    {
        std::vector<std::string> parts;
        std::size_t at = 0;
        while (parts.size() < 3)
        {
            auto colon = text.find(':', at);
            if (colon == std::string::npos)
            {
                if (!parts.empty())
                {
                    parts.push_back(text.substr(at));
                }
                at = std::string::npos;
                break;
            }
            parts.push_back(text.substr(at, colon - at));
            at = colon + 1;
        }
        if (at != std::string::npos || parts.size() != 3)
        {
            return std::nullopt;
        }

        LiveSim sim;
        try
        {
            sim.jitterTicks = static_cast<uint32_t>(std::stoul(parts[0]));
            sim.lossPercent = static_cast<uint32_t>(std::stoul(parts[1]));
            sim.seed = static_cast<uint32_t>(std::stoul(parts[2]));
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
        if (sim.lossPercent > 100)
        {
            return std::nullopt;
        }
        return sim;
    }

    /** One packet of a demo, and the tick of the stream by the time it was sent. */
    struct ScheduledPacket
    {
        uint8_t sender{0};
        std::vector<rwe::TadBytes> subPackets;
        uint32_t streamTick{0};
        bool hasSerial{false};
    };

    /** Reads a whole demo into memory, in file order. */
    struct CollectHandler : rwe::TadHandler
    {
        std::vector<ScheduledPacket> packets;

        void onPacket(const rwe::TadPacket& packet, const std::vector<rwe::TadBytes>& subPackets, const rwe::TadWalkStats&) override
        {
            ScheduledPacket scheduled;
            scheduled.sender = packet.sender;
            scheduled.subPackets = subPackets;
            for (const auto& subPacket : subPackets)
            {
                if (rwe::tadSerialOfUnitState(subPacket))
                {
                    scheduled.hasSerial = true;
                    break;
                }
            }
            packets.push_back(std::move(scheduled));
        }
    };

    std::optional<LiveResult> playDemoLive(const fs::path& path, DataSet& data, const DemoMeta& meta, LiveSim live)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            std::cerr << "  ERROR: cannot open\n";
            return std::nullopt;
        }

        CollectHandler collected;
        rwe::readTad(stream, collected);
        auto& packets = collected.packets;

        // The first serial names tick 0, exactly as the receiver anchors it.
        std::optional<uint32_t> origin;
        for (const auto& packet : packets)
        {
            if (packet.hasSerial)
            {
                if (auto serial = rwe::tadSerialOfUnitState(packet.subPackets.front()))
                {
                    origin = *serial;
                }
                break;
            }
        }
        if (!origin)
        {
            std::cerr << "  ERROR: no packet carries a 0x2c to clock the run by\n";
            return std::nullopt;
        }

        // A packet is sent at the tick its 0x2c names, but a recording is in
        // arrival order, and one peer's tick 0 can be written after another's
        // tick 1. So each packet is given the tick the stream has got to, which
        // is the tick a straight playback applies it at, and the receiver is
        // left to work out the tick it names.
        uint32_t furthest = 0;
        for (auto& packet : packets)
        {
            if (packet.hasSerial)
            {
                for (const auto& subPacket : packet.subPackets)
                {
                    if (auto serial = rwe::tadSerialOfUnitState(subPacket))
                    {
                        furthest = std::max(furthest, *serial >= *origin ? *serial - *origin : 0u);
                        break;
                    }
                }
            }
            packet.streamTick = furthest;
        }

        std::optional<LiveResult> result;
        withPlayback(data, meta, true, [&](rwe::GameSimulation& sim, rwe::TadPuppetDriver& driver) {
        rwe::TaLiveReceiver receiver(driver, rwe::TaLiveReceiverOptions{live.jitterTicks});

        // The generator is the tool's own: nothing here may touch the
        // simulation's, or the same seed would give two different games.
        std::minstd_rand rng(live.seed);
        std::vector<std::vector<std::size_t>> arriving(furthest + 1);
        for (std::size_t i = 0; i < packets.size(); ++i)
        {
            ++live.packetsScheduled;
            if (live.lossPercent > 0 && rwe::randomBelow(rng, 100) < live.lossPercent)
            {
                ++live.packetsLost;
                continue;
            }

            // Only a packet with a 0x2c is on the jittered clock. One without
            // has no tick of its own: the receiver passes it straight through
            // and the driver resolves it from the sender's last serial, so
            // there is nothing to lead it by.
            auto lead = 0u;
            if (packets[i].hasSerial)
            {
                lead = rwe::randomBetween(rng, 0, static_cast<int>(live.jitterTicks));
            }
            auto streamTick = static_cast<uint64_t>(packets[i].streamTick);
            arriving[static_cast<std::size_t>(streamTick - std::min<uint64_t>(lead, streamTick))].push_back(i);
        }

        for (std::size_t t = 0; t <= furthest; ++t)
        {
            for (auto index : arriving[t])
            {
                auto& packet = packets[index];
                receiver.onPacket(rwe::TadPacket{0, packet.sender}, packet.subPackets, static_cast<uint32_t>(t));
            }
            receiver.onTick(static_cast<uint32_t>(t));
            driver.applyTick(static_cast<uint32_t>(t));
            if (t < furthest)
            {
                sim.tick();
            }
        }

        live.receiver = receiver.stats();
        live.localTicks = static_cast<uint32_t>(furthest);
        result = LiveResult{driver.stats(), live};
        });
        return result;
    }
}

int main(int argc, char* argv[])
{
    rwe::OpaqueArgs args;
    try
    {
        args.parse(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n";
        std::cerr << "usage: tad_puppet --file <path> | --dir <path> [--data-path <dir>...] [--json <out>]\n";
        return 1;
    }

    if (args.isHelpRequested() || (!args.contains("file") && !args.contains("dir")))
    {
        std::cout << "usage: tad_puppet --file <path> [--file <path>...] [--dir <path>] [--data-path <dir>...] [--json <out>]\n"
                  << "\n"
                  << "  --file       a .tad or .ted demo to play; may be repeated\n"
                  << "  --dir        a directory of demos to play; recurses\n"
                  << "  --data-path  game data search path; may be repeated, first wins\n"
                  << "  --json       write the per-demo and aggregate report to this file\n"
                  << "  --live-sim   <jitter-ticks>:<loss-percent>:<seed>, to arrive the packets over a\n"
                  << "               simulated network through TaLiveReceiver rather than reading them in order\n"
                  << "\n"
                  << "Exits non-zero if any demo fails to play.\n";
        return args.isHelpRequested() ? 0 : 1;
    }

    std::optional<LiveSim> liveSim;
    if (args.contains("live-sim"))
    {
        liveSim = LiveSim::parse(args.getString("live-sim"));
        if (!liveSim)
        {
            std::cerr << "--live-sim wants <jitter-ticks>:<loss-percent>:<seed>, loss 0 to 100\n";
            return 1;
        }
    }

    std::vector<fs::path> paths;
    for (const auto& file : args.getMulti("file"))
    {
        paths.emplace_back(file);
    }
    for (const auto& dir : args.getMulti("dir"))
    {
        std::error_code error;
        fs::recursive_directory_iterator it(dir, error);
        if (error)
        {
            std::cerr << "cannot read directory " << dir << ": " << error.message() << "\n";
            return 1;
        }
        for (const auto& entry : it)
        {
            if (entry.is_regular_file() && isDemo(entry.path()))
            {
                paths.push_back(entry.path());
            }
        }
    }
    std::sort(paths.begin(), paths.end());
    if (paths.empty())
    {
        std::cerr << "no demos found\n";
        return 1;
    }

    std::vector<fs::path> dataPaths;
    for (const auto& path : args.getMulti("data-path"))
    {
        dataPaths.emplace_back(path);
    }
    if (dataPaths.empty())
    {
        auto localDataPath = rwe::getLocalDataPath();
        if (!localDataPath)
        {
            std::cerr << "Failed to determine local data path\n";
            return 1;
        }
        dataPaths.emplace_back(*localDataPath / "Data");
    }

    DataSet data;
    data.pathMapping = defaultPathMapping();
    for (const auto& path : dataPaths)
    {
        rwe::addToVfs(data.vfs, path.string());
    }

    if (auto paletteBytes = data.vfs.readFile("palettes/PALETTE.PAL"))
    {
        data.palette = rwe::readPalette(*paletteBytes);
    }
    if (auto guiPaletteBytes = data.vfs.readFile("palettes/GUIPAL.PAL"))
    {
        data.guiPalette = rwe::readPalette(*guiPaletteBytes);
    }

    Totals totals;
    json report;
    report["demos"] = json::array();

    for (const auto& path : paths)
    {
        ++totals.demos;
        std::cout << path.filename().string() << "\n";

        json demoJson;
        demoJson["file"] = path.filename().string();

        DemoMeta meta;
        try
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                std::cerr << "  ERROR: cannot open\n";
                ++totals.failed;
                demoJson["error"] = "cannot open";
                report["demos"].push_back(std::move(demoJson));
                continue;
            }
            MetaHandler handler;
            rwe::readTad(stream, handler);
            meta = std::move(handler.meta);
        }
        catch (const std::exception& e)
        {
            std::cerr << "  ERROR: " << e.what() << "\n";
            ++totals.failed;
            demoJson["error"] = e.what();
            report["demos"].push_back(std::move(demoJson));
            continue;
        }

        if (meta.header)
        {
            demoJson["map"] = meta.header->mapName;
            demoJson["players"] = meta.header->numPlayers;
        }

        try
        {
            if (liveSim)
            {
                auto result = playDemoLive(path, data, meta, *liveSim);
                if (!result)
                {
                    ++totals.failed;
                    demoJson["error"] = "failed to play";
                    report["demos"].push_back(std::move(demoJson));
                    continue;
                }

                printLive(result->sim, result->sim.localTicks);
                printStats(result->driver);
                demoJson.update(statsJson(result->driver));
                demoJson["live"] = liveJson(result->sim);
                report["demos"].push_back(std::move(demoJson));
                addStats(totals.stats, result->driver);
                if (!totals.live)
                {
                    totals.live = *liveSim;
                }
                addLive(*totals.live, result->sim);
            }
            else
            {
                auto stats = playDemo(path, data, meta);
                if (!stats)
                {
                    ++totals.failed;
                    demoJson["error"] = "failed to play";
                    report["demos"].push_back(std::move(demoJson));
                    continue;
                }

                printStats(*stats);
                demoJson.update(statsJson(*stats));
                report["demos"].push_back(std::move(demoJson));
                addStats(totals.stats, *stats);
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "  ERROR: " << e.what() << "\n";
            ++totals.failed;
            demoJson["error"] = e.what();
            report["demos"].push_back(std::move(demoJson));
        }
    }

    std::cout << "\n"
              << totals.demos << " demos, " << totals.failed << " failed\n";
    std::cout << "aggregate:\n";
    if (totals.live)
    {
        printLive(*totals.live, totals.live->localTicks);
    }
    printStats(totals.stats);

    report["aggregate"] = statsJson(totals.stats);
    report["aggregate"]["demos"] = totals.demos;
    report["aggregate"]["failed"] = totals.failed;
    if (totals.live)
    {
        report["aggregate"]["live"] = liveJson(*totals.live);
    }

    if (args.contains("json"))
    {
        std::ofstream out(args.getString("json"));
        if (!out)
        {
            std::cerr << "cannot write " << args.getString("json") << "\n";
            return 1;
        }
        out << report.dump(2) << "\n";
    }

    return totals.failed > 0 ? 1 : 0;
}
