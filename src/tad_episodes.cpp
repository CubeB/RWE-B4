// Mines Total Annihilation demo recordings for short bounded episodes with real
// numbers in them -- the conformance corpus item 2 of docs/TA-DEMOS.md asks for.
//
// This is deliberately NOT a tad_probe mode. tad_probe's contract is "exits
// non-zero if anything walked out of step" and that check should stay sharp; an
// extractor exits non-zero for entirely different reasons, and mixing the two
// would blunt the one number the walker exists to produce.
//
// What it extracts today is build timing: a 0x09 nanoframe appearing, the 0x12
// that finishes it, and the tick count between them, which lands directly on
// UnitState::getBuildCostInfo / addBuildProgress and on workerTime. An episode
// mined from a competitive game is not a controlled experiment, so most of the
// work here is the filters -- docs/TA-DEMOS.md, "The filters, which are the real
// work" -- and every rejection is counted and reported rather than silently
// dropped, because the rejection rate is itself worth looking at.
//
// NAMING A TYPE. A 0x09 carries its unit type as a load-order index, not a
// name: TA numbers every units\*.FBI in the merged VFS from one, in sorted
// order, and that number is what the packet holds. Point --units at the data
// set the demo was recorded on and every episode gets a name; without it the
// type stays an anonymous index, which is still fine for the timing histogram.
// The rule, and the evidence for it, is on tadUnitLoadOrder in tad_events.h.
//
// The mod files never enter the repository -- only extracted numbers do -- so
// --units takes a path rather than shipping a table.
//
// Truly offline: no SDL, no GL, no VFS, just files.
//
// Usage: tad_episodes --file <path> [--file <path>...] [--dir <path>]
//                     [--units <dir>] [--emit-json <path>]
//                     [--emit-resources <path>] [--all]
//   --file        a .tad or .ted demo to mine; may be repeated
//   --dir         a directory of demos to mine; recurses
//   --units       a directory of the data set's unit files, recursively
//                 scanned for *.FBI, used to name each type index
//   --emit-json   write the episodes to a file as JSON
//   --emit-resources  write every 0x28 resource record to a file as JSON
//   --emit-cpp    write the storage episodes as a C++ header for rwe_test
//   --max-types   distinct unit types an episode may carry (default 6)
//   --max-per-player  episodes to keep per player (default 3)
//   --all         emit rejected episodes too, each with its reasons

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/util/OpaqueArgs.h>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * One nanoframe, from the 0x09 that created it to the 0x12 that finished
         * it, with everything that happened in between that might disqualify it.
         */
        struct Episode
        {
            std::string demo;
            uint8_t sender;
            unsigned int ownerBlock;

            uint16_t typeIndex;
            uint16_t unitId;
            uint16_t builderId;

            uint32_t startTick;
            uint32_t finishTick;

            /**
             * Where the nanoframe was laid down. Carried through so that an
             * overhead can be tested against distance -- a builder that has to
             * walk to its next site looks nothing like one paying a fixed cost,
             * and the 0x09 is the only place either position is recorded.
             */
            TadPosition position;

            /** Why this is not a clean measurement of the nanolathe. */
            std::vector<std::string> rejections;

            uint32_t durationTicks() const { return finishTick - startTick; }
            bool clean() const { return rejections.empty(); }
        };

        /** A nanoframe that has started but not yet finished. */
        struct InProgress
        {
            uint16_t typeIndex;
            uint32_t startTick;
            TadPosition position;
            uint8_t sender;
            unsigned int ownerBlock;
            std::set<std::string> rejections;
        };

        struct EpisodeHandler : TadHandler
        {
            std::string demo;
            TadHeader header;
            std::optional<TadUnitTable> unitTable;

            /** The tick clock, per sender: the 0x2c serial, never Packet::time. */
            std::map<uint8_t, uint32_t> tick;

            std::map<uint16_t, InProgress> inProgress;
            std::vector<Episode> episodes;

            /** Owner blocks whose resource record showed an empty pool. */
            std::map<unsigned int, uint32_t> stalledSince;

            /**
             * One 0x28 burst, collapsed. A sender emits `numPlayers - 1` copies
             * of one record on one tick -- one unicast per other peer, which the
             * recorder sees all of -- so the copies carry no extra information
             * and only the first is kept. `copies` keeps the burst length,
             * which is the evidence for that reading.
             */
            struct ResourceRecord
            {
                uint8_t sender;
                uint32_t tick;
                unsigned int copies;
                TadResourceStats stats;
            };

            std::vector<ResourceRecord> resourceRecords;

            /** Where each sender's current burst sits, and what it has shown. */
            struct OpenBurst
            {
                std::size_t index;
                bool statsDiffered = false;
                bool prefixDiffered = false;
            };

            std::map<uint8_t, OpenBurst> openBurst;

            /**
             * Bursts whose copies disagreed on one of the ten floats. Expected
             * to be zero, and reported rather than asserted: this is the number
             * that would overturn the fan-out reading if a demo produced one.
             */
            unsigned int inconsistentBursts = 0;

            /**
             * Bursts whose copies agreed on the floats but not on the 17
             * unresolved bytes. Not a counter-example: these all sit in a game's
             * closing seconds, where 0x28 comes every five ticks or so instead
             * of every 120 and two successive samples land on one tick.
             */
            unsigned int variantPrefixBursts = 0;

            unsigned int speedChanges = 0;
            unsigned int orphanedFinishes = 0;

            explicit EpisodeHandler(std::string demo) : demo(std::move(demo)) {}

            void onHeader(const TadHeader& h) override { header = h; }

            std::vector<TadPlayer> players;

            void onPlayer(const TadPlayer& p, unsigned int, unsigned int) override
            {
                players.push_back(p);
            }

            void onUnitData(const TadBytes& record) override
            {
                unitTable = tadDecodeUnitTable(record);
            }

            /** Marks every build in progress, everywhere, as spoilt. */
            void spoilAll(const std::string& reason)
            {
                for (auto& [id, build] : inProgress)
                {
                    build.rejections.insert(reason);
                }
            }

            void spoilOwner(unsigned int block, const std::string& reason)
            {
                for (auto& [id, build] : inProgress)
                {
                    if (build.ownerBlock == block)
                    {
                        build.rejections.insert(reason);
                    }
                }
            }

            void onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets, const TadWalkStats&) override
            {
                for (const auto& s : subPackets)
                {
                    if (s.empty())
                    {
                        continue;
                    }

                    switch (static_cast<TadSubPacketCode>(s[0]))
                    {
                        case TadSubPacketCode::UnitStatAndMove:
                            if (s.size() >= 7)
                            {
                                tick[packet.sender] = static_cast<uint32_t>(s[3])
                                    | (static_cast<uint32_t>(s[4]) << 8)
                                    | (static_cast<uint32_t>(s[5]) << 16)
                                    | (static_cast<uint32_t>(s[6]) << 24);
                            }
                            break;

                        case TadSubPacketCode::Speed:
                            // A speed change or a pause anywhere invalidates
                            // every window it falls inside, because the tick
                            // clock stops meaning wall time.
                            ++speedChanges;
                            spoilAll("speed change");
                            break;

                        case TadSubPacketCode::UnitBuildStarted:
                            onBuildStarted(packet.sender, s);
                            break;

                        case TadSubPacketCode::UnitBuildFinished:
                            onBuildFinished(packet.sender, s);
                            break;

                        case TadSubPacketCode::UnitTakeDamage:
                            onDamage(packet.sender, s);
                            break;

                        case TadSubPacketCode::UnitKilled:
                            onDeath(s);
                            break;

                        case TadSubPacketCode::PlayerResourceInfo:
                            onResources(packet.sender, s);
                            break;

                        default:
                            break;
                    }
                }
            }

            void onBuildStarted(uint8_t sender, const TadBytes& s)
            {
                auto e = tadDecodeBuildStarted(s);
                if (!e)
                {
                    return;
                }

                auto block = tadOwnerBlockOfUnitId(e->unitId, header.maxUnits);
                if (!block)
                {
                    return;
                }

                // The same 0x09 can arrive twice -- the corpus has duplicates at
                // tick 0. Restarting the clock on the second copy would measure
                // nothing, so the first one wins.
                if (inProgress.count(e->unitId) != 0)
                {
                    return;
                }

                senderBlock[sender] = *block;

                InProgress build{e->typeIndex, tick[sender], e->position, sender, *block, {}};

                // A player already in stall when the frame is laid down is
                // measuring the economy, not the nanolathe.
                if (stalledSince.count(*block) != 0)
                {
                    build.rejections.insert("owner stalled");
                }

                inProgress.emplace(e->unitId, build);
            }

            void onBuildFinished(uint8_t sender, const TadBytes& s)
            {
                auto e = tadDecodeBuildFinished(s);
                if (!e)
                {
                    return;
                }

                auto it = inProgress.find(e->unitId);
                if (it == inProgress.end())
                {
                    // Finished something whose start was before the recording
                    // began, or which the walker never saw.
                    ++orphanedFinishes;
                    return;
                }

                const auto& build = it->second;
                Episode episode{
                    demo,
                    build.sender,
                    build.ownerBlock,
                    build.typeIndex,
                    e->unitId,
                    e->builderId,
                    build.startTick,
                    tick[sender],
                    build.position,
                    {build.rejections.begin(), build.rejections.end()}};

                if (episode.finishTick <= episode.startTick)
                {
                    episode.rejections.emplace_back("no elapsed ticks");
                }

                if (tadOwnerBlockOfUnitId(e->builderId, header.maxUnits) != build.ownerBlock)
                {
                    episode.rejections.emplace_back("builder in another owner block");
                }

                auto hit = lastDamageTick.find(e->builderId);
                if (hit != lastDamageTick.end() && hit->second >= episode.startTick)
                {
                    episode.rejections.emplace_back("builder took damage");
                }

                episodes.push_back(std::move(episode));
                inProgress.erase(it);
            }

            void onDamage(uint8_t sender, const TadBytes& s)
            {
                auto e = tadDecodeDamage(s);
                if (!e)
                {
                    return;
                }

                // Damage to the frame itself spoils its window immediately. The
                // builder cannot be handled here, because a 0x09 does not name
                // one -- see tad_events.h -- so the last tick each unit was hit
                // is kept and consulted when the 0x12 finally names it.
                auto it = inProgress.find(e->victimId);
                if (it != inProgress.end())
                {
                    it->second.rejections.insert("frame took damage");
                }

                lastDamageTick[e->victimId] = tick[sender];
            }

            void onDeath(const TadBytes& s)
            {
                auto e = tadDecodeDeath(s);
                if (!e)
                {
                    return;
                }

                inProgress.erase(e->unitId);
            }

            void onResources(uint8_t sender, const TadBytes& s)
            {
                auto e = tadDecodeResourceStats(s);
                if (!e)
                {
                    return;
                }

                // Collapse the burst. Everything after the first copy is the
                // same record addressed to another peer, so it is counted and
                // dropped; a copy that does not match is counted separately,
                // because that is what a positional reading would look like.
                auto open = openBurst.find(sender);
                if (open != openBurst.end() && resourceRecords[open->second.index].tick == tick[sender])
                {
                    auto& first = resourceRecords[open->second.index];
                    ++first.copies;

                    auto statsDiffer = std::memcmp(&first.stats, &*e, offsetof(TadResourceStats, prefix)) != 0;
                    auto prefixDiffers = std::memcmp(first.stats.prefix, e->prefix, sizeof(e->prefix)) != 0;

                    if (statsDiffer && !open->second.statsDiffered)
                    {
                        open->second.statsDiffered = true;
                        ++inconsistentBursts;
                    }
                    else if (prefixDiffers && !open->second.statsDiffered && !open->second.prefixDiffered)
                    {
                        open->second.prefixDiffered = true;
                        ++variantPrefixBursts;
                    }
                    return;
                }

                openBurst[sender] = OpenBurst{resourceRecords.size()};
                resourceRecords.push_back(ResourceRecord{sender, tick[sender], 1, *e});

                // A watcher's record is all zeros but for two slots and would
                // otherwise read as a permanent stall. Filter it on the shape
                // that identifies it rather than on the player table, which does
                // not survive between recordings of the same game.
                auto watcher = e->metalStorage == 0.0f && e->energyStorage == 0.0f;
                if (watcher)
                {
                    return;
                }

                // The record is the SENDER'S OWN state -- settled over the
                // corpus, see docs/TA-DEMOS.md -- and it carries no id, so the
                // owner block comes from the sender's own units.
                auto block = senderBlock.find(sender);
                if (block == senderBlock.end())
                {
                    return;
                }

                auto empty = e->metalStored == 0.0f || e->energyStored == 0.0f;
                if (empty)
                {
                    stalledSince[block->second] = tick[sender];
                    spoilOwner(block->second, "owner stalled");
                }
                else
                {
                    stalledSince.erase(block->second);
                }
            }

            /**
             * Which owner block each sender's own units live in. Learned from
             * the senders' own build events rather than from the player table,
             * whose numbering is not consistent between recordings of one game,
             * and which in any case does not agree with the block index.
             */
            std::map<uint8_t, unsigned int> senderBlock;

            /** The last tick each unit was hit, for the builder test above. */
            std::map<uint16_t, uint32_t> lastDamageTick;
        };
    }
}

namespace
{
    using namespace rwe;

    /**
     * Every *.FBI stem under dir, which is what TA's units\\*.FBI enumeration
     * sees once the VFS has merged the archives. Extracted mods keep one
     * directory per archive, so this recurses.
     */
    std::vector<std::filesystem::path> readUnitFiles(const std::filesystem::path& dir, std::error_code& error)
    {
        std::vector<std::filesystem::path> files;
        std::filesystem::recursive_directory_iterator it(dir, error);
        if (error)
        {
            return files;
        }

        for (const auto& entry : it)
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            auto extension = entry.path().extension().string();
            for (auto& c : extension)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            if (extension == ".fbi")
            {
                files.push_back(entry.path());
            }
        }

        return files;
    }

    /**
     * What an episode has to carry inline about a unit type, read out of the
     * data set's own FBI with the engine's own parser.
     *
     * Only --emit-cpp needs this. Naming a type needs the file names alone,
     * which is why the load order is built from stems and this is separate.
     */
    struct UnitStorage
    {
        float metalStorage;
        float energyStorage;
    };

    std::map<std::string, UnitStorage> readUnitStorage(const std::vector<std::filesystem::path>& files)
    {
        std::map<std::string, UnitStorage> storage;
        for (const auto& file : files)
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                continue;
            }

            std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

            try
            {
                auto fbi = parseUnitFbi(parseTdfFromString(contents));
                auto name = file.stem().string();
                for (auto& c : name)
                {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                storage[name] = UnitStorage{
                    static_cast<float>(fbi.metalStorage),
                    static_cast<float>(fbi.energyStorage)};
            }
            catch (const std::exception&)
            {
                // A file the parser will not take cannot contribute a number to
                // an episode, and an episode missing one is dropped below rather
                // than emitted with a hole in it.
            }
        }
        return storage;
    }

    /** The type index as a name if we have the data set, and as a number if not. */
    std::string typeLabel(const std::vector<std::string>& loadOrder, uint16_t typeIndex)
    {
        if (auto name = tadUnitNameForTypeIndex(loadOrder, typeIndex))
        {
            return *name;
        }

        return std::to_string(typeIndex);
    }

    nlohmann::json toJson(const Episode& e, const std::vector<std::string>& loadOrder)
    {
        nlohmann::json j;
        j["demo"] = e.demo;
        j["ownerBlock"] = e.ownerBlock;
        j["typeIndex"] = e.typeIndex;
        if (auto name = tadUnitNameForTypeIndex(loadOrder, e.typeIndex))
        {
            j["unitName"] = *name;
        }
        j["unitId"] = e.unitId;
        j["builderId"] = e.builderId;
        j["startTick"] = e.startTick;
        j["finishTick"] = e.finishTick;
        j["durationTicks"] = e.durationTicks();
        j["x"] = tadFixedToDouble(e.position.x);
        j["y"] = tadFixedToDouble(e.position.y);
        j["z"] = tadFixedToDouble(e.position.z);
        if (!e.rejections.empty())
        {
            j["rejections"] = e.rejections;
        }
        return j;
    }

    /**
     * Every 0x28 of one demo, with the player table beside it.
     *
     * The record carries no player id, so attribution has to be worked out from
     * the burst -- see docs/TA-DEMOS.md. This dump is what that argument is made
     * from: the sender, the tick, the position in the burst and the raw floats,
     * with nothing interpreted on the way out.
     */
    nlohmann::json resourcesToJson(const rwe::EpisodeHandler& handler)
    {
        nlohmann::json j;
        j["demo"] = handler.demo;
        j["numPlayers"] = handler.header.numPlayers;
        j["maxUnits"] = handler.header.maxUnits;
        j["mapName"] = handler.header.mapName;

        j["players"] = nlohmann::json::array();
        for (const auto& p : handler.players)
        {
            nlohmann::json pj;
            pj["color"] = p.color;
            pj["side"] = p.side;
            pj["number"] = p.number;
            pj["name"] = p.name;
            pj["watcher"] = p.isWatcher();
            j["players"].push_back(pj);
        }

        j["senderBlocks"] = nlohmann::json::object();
        for (const auto& [sender, block] : handler.senderBlock)
        {
            j["senderBlocks"][std::to_string(sender)] = block;
        }

        j["inconsistentBursts"] = handler.inconsistentBursts;
        j["variantPrefixBursts"] = handler.variantPrefixBursts;
        j["records"] = nlohmann::json::array();
        for (const auto& r : handler.resourceRecords)
        {
            nlohmann::json rj;
            rj["sender"] = r.sender;
            rj["tick"] = r.tick;
            rj["copies"] = r.copies;
            rj["metalStored"] = r.stats.metalStored;
            rj["energyStored"] = r.stats.energyStored;
            rj["metalStorage"] = r.stats.metalStorage;
            rj["energyStorage"] = r.stats.energyStorage;
            rj["energyCounters"] = {r.stats.energyCounters[0], r.stats.energyCounters[1], r.stats.energyCounters[2]};
            rj["metalCounters"] = {r.stats.metalCounters[0], r.stats.metalCounters[1], r.stats.metalCounters[2]};

            // The 17 unresolved bytes, whole. They are the only place a
            // recipient or subject id could still be hiding, so the dump has to
            // carry them for the attribution argument to be worth anything.
            std::string prefix;
            for (auto b : r.stats.prefix)
            {
                const char* digits = "0123456789abcdef";
                prefix += digits[b >> 4];
                prefix += digits[b & 0x0f];
            }
            rj["prefix"] = prefix;
            j["records"].push_back(rj);
        }

        return j;
    }

    // --- --emit-cpp: storage episodes ---------------------------------------
    //
    // A storage episode is one 0x28 sample plus the composition that explains
    // its two capacity slots. The model being pinned is that TA's capacity is a
    // plain sum over what the player has FINISHED -- base plus each unit's own
    // MetalStorage/EnergyStorage, nanoframes contributing nothing -- and the
    // emitter only keeps a sample where that sum is already known to come out
    // right, because an episode the model cannot explain would be a broken test
    // rather than a finding.

    /** One (type, count) row of an episode's composition. */
    struct CompositionEntry
    {
        std::string unitName;
        unsigned int count;
        float metalStorage;
        float energyStorage;
    };

    struct StorageEpisode
    {
        std::string demo;
        unsigned int ownerBlock;
        uint32_t previousSampleTick;
        uint32_t sampleTick;
        float startingMetal;
        float startingEnergy;
        std::vector<CompositionEntry> finished;
        std::vector<CompositionEntry> building;
        float metalStorage;
        float energyStorage;
        float metalStored;
        float energyStored;
    };

    /** Aggregates units by type, in name order, so regeneration is stable. */
    std::vector<CompositionEntry> aggregate(
        const std::vector<std::string>& names,
        const std::map<std::string, UnitStorage>& unitStorage)
    {
        std::map<std::string, unsigned int> counts;
        for (const auto& name : names)
        {
            ++counts[name];
        }

        std::vector<CompositionEntry> out;
        for (const auto& [name, count] : counts)
        {
            auto storage = unitStorage.find(name);
            if (storage == unitStorage.end())
            {
                return {};
            }
            out.push_back(CompositionEntry{name, count, storage->second.metalStorage, storage->second.energyStorage});
        }
        return out;
    }

    std::vector<StorageEpisode> mineStorageEpisodes(
        const EpisodeHandler& handler,
        const std::vector<std::string>& loadOrder,
        const std::map<std::string, UnitStorage>& unitStorage,
        std::size_t maxTypes,
        std::size_t maxPerPlayer)
    {
        // Every build the demo paired, named and keyed by owner block. Rejected
        // ones count as much as clean ones here: a unit's rejection is about
        // whether its DURATION means anything, and it holds its storage either
        // way.
        std::map<unsigned int, std::vector<const Episode*>> byBlock;
        for (const auto& episode : handler.episodes)
        {
            byBlock[episode.ownerBlock].push_back(&episode);
        }
        for (auto& [block, list] : byBlock)
        {
            std::sort(list.begin(), list.end(), [](const Episode* a, const Episode* b) {
                return std::tie(a->finishTick, a->unitId) < std::tie(b->finishTick, b->unitId);
            });
        }

        std::map<uint8_t, std::vector<const EpisodeHandler::ResourceRecord*>> bySender;
        for (const auto& record : handler.resourceRecords)
        {
            bySender[record.sender].push_back(&record);
        }

        std::vector<StorageEpisode> out;
        for (const auto& [sender, samples] : bySender)
        {
            auto block = handler.senderBlock.find(sender);
            if (block == handler.senderBlock.end() || samples.empty())
            {
                continue;
            }

            // A watcher reports zero capacity for ever; it owns nothing and
            // explains nothing.
            const auto& first = *samples.front();
            if (first.stats.metalStorage == 0.0f && first.stats.energyStorage == 0.0f)
            {
                continue;
            }

            const auto& completions = byBlock[block->second];

            // The base is the lobby's storage setting, and the only way to read
            // it off is a first sample taken before the player finished
            // anything. A recording that joined a game in progress has no base
            // and no episodes.
            auto startedClean = std::none_of(completions.begin(), completions.end(), [&](const Episode* e) {
                return e->finishTick <= first.tick;
            });
            if (!startedClean)
            {
                continue;
            }

            auto startingMetal = first.stats.metalStorage;
            auto startingEnergy = first.stats.energyStorage;

            auto lastMetalStorage = -1.0f;
            auto lastEnergyStorage = -1.0f;
            uint32_t previousTick = 0;
            std::size_t kept = 0;

            for (const auto* sample : samples)
            {
                if (kept >= maxPerPlayer)
                {
                    break;
                }

                std::vector<std::string> finishedNames;
                std::vector<std::string> buildingNames;
                auto predictedMetal = startingMetal;
                auto predictedEnergy = startingEnergy;
                auto unknownType = false;

                for (const auto* e : completions)
                {
                    auto name = tadUnitNameForTypeIndex(loadOrder, e->typeIndex);
                    if (!name)
                    {
                        unknownType = true;
                        break;
                    }

                    auto storage = unitStorage.find(*name);
                    if (storage == unitStorage.end())
                    {
                        unknownType = true;
                        break;
                    }

                    if (e->finishTick <= sample->tick)
                    {
                        finishedNames.push_back(*name);
                        predictedMetal += storage->second.metalStorage;
                        predictedEnergy += storage->second.energyStorage;
                    }
                    else if (e->startTick <= sample->tick)
                    {
                        buildingNames.push_back(*name);
                    }
                }

                if (unknownType)
                {
                    break;
                }

                // The first sample the sum stops explaining is where the
                // player's history stops being complete -- a storage building
                // died, or one was begun before the recording. Everything after
                // it is unexplained, so the walk ends rather than skipping on.
                if (predictedMetal != sample->stats.metalStorage || predictedEnergy != sample->stats.energyStorage)
                {
                    break;
                }

                auto changed = sample->stats.metalStorage != lastMetalStorage
                    || sample->stats.energyStorage != lastEnergyStorage;

                if (changed)
                {
                    auto finished = aggregate(finishedNames, unitStorage);
                    auto building = aggregate(buildingNames, unitStorage);

                    // A composition nobody can read is not worth checking in.
                    if (finished.size() <= maxTypes)
                    {
                        out.push_back(StorageEpisode{
                            handler.demo,
                            block->second,
                            previousTick,
                            sample->tick,
                            startingMetal,
                            startingEnergy,
                            std::move(finished),
                            std::move(building),
                            sample->stats.metalStorage,
                            sample->stats.energyStorage,
                            sample->stats.metalStored,
                            sample->stats.energyStored});
                        ++kept;
                    }

                    lastMetalStorage = sample->stats.metalStorage;
                    lastEnergyStorage = sample->stats.energyStorage;
                }

                previousTick = sample->tick;
            }
        }

        return out;
    }

    /**
     * A float as a C++ literal that reads back bit for bit. Nine significant
     * digits round-trip a float32, and a literal that lost its point would not
     * compile.
     */
    std::string floatLiteral(float value)
    {
        std::ostringstream ss;
        ss << std::setprecision(9) << value;
        auto text = ss.str();
        if (text.find('.') == std::string::npos && text.find('e') == std::string::npos)
        {
            text += ".0";
        }
        return text + "f";
    }

    void emitComposition(std::ostream& out, const std::string& name, const std::vector<CompositionEntry>& entries)
    {
        out << "    inline constexpr TadEpisodeComposition " << name << "[] = {\n";
        for (const auto& entry : entries)
        {
            out << "        {\"" << entry.unitName << "\", " << entry.count << ", "
                << floatLiteral(entry.metalStorage) << ", " << floatLiteral(entry.energyStorage) << "},\n";
        }
        out << "    };\n\n";
    }

    void writeEpisodes(std::ostream& out, std::vector<StorageEpisode> episodes)
    {
        // Deterministic order, and no timestamp or path anywhere in the output:
        // regenerating over an unchanged corpus has to produce a byte-identical
        // file or the check-in is worthless as a diff.
        std::sort(episodes.begin(), episodes.end(), [](const StorageEpisode& a, const StorageEpisode& b) {
            return std::tie(a.demo, a.ownerBlock, a.sampleTick) < std::tie(b.demo, b.ownerBlock, b.sampleTick);
        });

        // One episode per distinct set of storage-granting types, because two
        // episodes with the same set assert the same thing and a corpus fixture
        // earns its place by variety rather than by volume. Everything the
        // survivor owns rides along in its composition, mexes and wind
        // generators included, so the types that grant nothing are still being
        // checked to grant nothing -- they simply stop deciding which episodes
        // get checked in.
        std::set<std::string> seen;
        std::vector<StorageEpisode> distinct;
        for (auto& episode : episodes)
        {
            std::ostringstream key;
            key << floatLiteral(episode.metalStorage) << "/" << floatLiteral(episode.energyStorage);
            for (const auto& entry : episode.finished)
            {
                if (entry.metalStorage > 0.0f || entry.energyStorage > 0.0f)
                {
                    key << " " << entry.unitName << "x" << entry.count;
                }
            }
            for (const auto& entry : episode.building)
            {
                if (entry.metalStorage > 0.0f || entry.energyStorage > 0.0f)
                {
                    key << " +" << entry.unitName << "x" << entry.count;
                }
            }

            if (seen.insert(key.str()).second)
            {
                distinct.push_back(std::move(episode));
            }
        }
        episodes = std::move(distinct);

        std::cout << episodes.size() << " episode(s) after keeping one per storage shape\n";

        out << R"(#pragma once

// GENERATED FILE -- do not edit by hand. Regenerate with tad_episodes --emit-cpp;
// the command, and the corpus it needs, are in docs/TA-DEMOS.md.
//
// Episodes mined from real Total Annihilation games, for the conformance tests
// in economy.test.cpp.
//
// WHY THIS IS A HEADER OF STRUCTS AND NOT A DATA FILE. rwe_test is hermetic --
// it reads no files, mounts no VFS and opens no archive -- and it stays that
// way. Demos and mod files never enter the repository either. So the numbers
// travel as source: each unit's own FBI values are transcribed inline beside
// the observation they explain, and a test can be read without either.
//
// WHAT AN EPISODE IS. One 0x28 resource sample from one player, with everything
// that player had finished, and everything it still had under construction, at
// the tick the sample landed on. A 0x28 is the sender's own state
// (docs/TA-DEMOS.md), so the composition is that sender's own owner block.
// Only slots 2 and 3 of the record -- the two storage capacities -- are what
// these episodes are chosen to explain; slots 0 and 1 come along for the clamp.
//
// The composition does NOT include the commander. TA gives a player its lobby
// storage setting for its commander rather than the commander's own FBI figures
// -- neither data set's commander declares any -- and startingMetal is that
// setting, read off the player's own opening sample, before it had finished
// anything at all.

#include <cstddef>

namespace rwe
{
    /**
     * One unit type in an episode's composition, with the FBI values that make
     * the observation predictable transcribed beside it.
     */
    struct TadEpisodeComposition
    {
        const char* unitName;
        unsigned int count;
        float metalStorage;
        float energyStorage;
    };

    /** One player's resource sample, and what it owned when the sample was taken. */
    struct TadStorageEpisode
    {
        /** Provenance: the demo this came out of, and where in it. */
        const char* demo;
        unsigned int ownerBlock;

        /**
         * The tick the sample landed on, and the one before it. The pair is the
         * window a failure has to be explained inside: anything that finished in
         * between is credited by the later sample and not by the earlier. A zero
         * previous tick means this was the player's first sample.
         */
        unsigned int previousSampleTick;
        unsigned int sampleTick;

        /** What the lobby gave the player for its commander. */
        float startingMetal;
        float startingEnergy;

        /** Finished at sampleTick, aggregated by type, commander excluded. */
        const TadEpisodeComposition* finished;
        std::size_t finishedCount;

        /**
         * Nanoframes standing at sampleTick that had not finished. These
         * contribute nothing, which is the falsifiable half of the episode:
         * crediting them would break 5,972 of the 6,162 corpus samples that have
         * one in flight.
         */
        const TadEpisodeComposition* building;
        std::size_t buildingCount;

        /** Observed, slots 2 and 3 of the record. */
        float metalStorage;
        float energyStorage;

        /** Observed, slots 0 and 1. Never above the capacity, in 61,709 samples. */
        float metalStored;
        float energyStored;

        /**
         * What RWE is expected to differ by, and why.
         *
         * A conformance test that asserts equality gets disabled the first time
         * it is right to fail, so an episode asserts the observation plus a
         * known delta instead. expectedDifference names the docs/TOTALA-EXE.md
         * section 88 entry that licences a non-zero one, and is null where there
         * is nothing to excuse. The emitter cannot know about a deliberate
         * difference, so it writes zero and null; an entry here is written by
         * hand, and survives regeneration because it is written into the
         * emitter's own table. There are none yet -- see section 88 for the
         * differences that exist and why none of them moves a storage capacity.
         */
        float expectedMetalStorageDelta;
        float expectedEnergyStorageDelta;
        const char* expectedDifference;
    };

)";

        for (std::size_t i = 0; i < episodes.size(); ++i)
        {
            const auto& episode = episodes[i];
            out << "    // " << episode.demo << ", owner block " << episode.ownerBlock
                << ", tick " << episode.sampleTick << ": capacity "
                << floatLiteral(episode.metalStorage) << " metal, "
                << floatLiteral(episode.energyStorage) << " energy.\n";

            if (!episode.finished.empty())
            {
                emitComposition(out, "tadStorageEpisode" + std::to_string(i) + "Finished", episode.finished);
            }
            if (!episode.building.empty())
            {
                emitComposition(out, "tadStorageEpisode" + std::to_string(i) + "Building", episode.building);
            }
            if (episode.finished.empty() && episode.building.empty())
            {
                out << "\n";
            }
        }

        // The table is laid out a field group to a line, which clang-format
        // would otherwise collapse into a seventeen-field one-liner nobody can
        // read a diff of. Everything above formats the way the tool wants it.
        out << "    // clang-format off\n"
            << "    inline constexpr TadStorageEpisode tadStorageEpisodes[] = {\n";
        for (std::size_t i = 0; i < episodes.size(); ++i)
        {
            const auto& episode = episodes[i];
            auto reference = [&](const char* suffix, std::size_t count) {
                return count == 0
                    ? std::string("nullptr, 0")
                    : "tadStorageEpisode" + std::to_string(i) + suffix + ", " + std::to_string(count);
            };

            out << "        {\"" << episode.demo << "\", " << episode.ownerBlock << ", "
                << episode.previousSampleTick << ", " << episode.sampleTick << ",\n"
                << "            " << floatLiteral(episode.startingMetal) << ", "
                << floatLiteral(episode.startingEnergy) << ",\n"
                << "            " << reference("Finished", episode.finished.size()) << ",\n"
                << "            " << reference("Building", episode.building.size()) << ",\n"
                << "            " << floatLiteral(episode.metalStorage) << ", "
                << floatLiteral(episode.energyStorage) << ", "
                << floatLiteral(episode.metalStored) << ", "
                << floatLiteral(episode.energyStored) << ",\n"
                << "            0.0f, 0.0f, nullptr},\n";
        }
        out << "    };\n"
            << "    // clang-format on\n"
            << "}\n";
    }

    /**
     * Writes the header, and says whether it moved.
     *
     * Regenerating over an unchanged corpus has to produce a byte-identical
     * file -- the check-in is only worth having if its diffs mean something --
     * so the emitter reads back what was there and reports. rwe_test cannot
     * make this check itself: it never opens a file, and the corpus is not in
     * the repository.
     */
    bool emitCpp(const std::filesystem::path& path, std::vector<StorageEpisode> episodes)
    {
        std::ostringstream generated;
        writeEpisodes(generated, std::move(episodes));

        std::string existing;
        {
            std::ifstream stream(path, std::ios::binary);
            if (stream)
            {
                existing.assign((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            }
        }

        if (existing == generated.str())
        {
            std::cout << "unchanged: " << path.string() << "\n";
            return true;
        }

        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            return false;
        }
        out << generated.str();
        std::cout << (existing.empty() ? "wrote " : "CHANGED ") << path.string() << "\n";
        return static_cast<bool>(out);
    }

    bool isDemo(const std::filesystem::path& path)
    {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return extension == ".tad" || extension == ".ted";
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
        return 1;
    }

    if (args.isHelpRequested() || (!args.contains("file") && !args.contains("dir")))
    {
        std::cout << "usage: tad_episodes --file <path> [--dir <path>] [--units <dir>]\n"
                  << "                   [--emit-json <path>] [--all]\n"
                  << "\n"
                  << "  --file        a .tad or .ted demo to mine; may be repeated\n"
                  << "  --dir         a directory of demos to mine; recurses\n"
                  << "  --units       a directory of the data set's unit files, scanned\n"
                  << "                recursively for *.FBI, used to name each type index\n"
                  << "  --emit-json   write the episodes to a file as JSON\n"
                  << "  --emit-resources  write every 0x28 resource record as JSON\n"
                  << "  --emit-cpp    write the storage episodes as a C++ header (needs --units)\n"
                  << "  --max-types   distinct unit types an episode may carry (default 6)\n"
                  << "  --max-per-player  episodes to keep per player (default 3)\n"
                  << "  --all         emit rejected episodes too, each with its reasons\n"
                  << "\n"
                  << "Without --units, episodes are keyed on an anonymous type index; see the\n"
                  << "note at the top of src/tad_episodes.cpp.\n";
        return args.isHelpRequested() ? 0 : 1;
    }

    std::vector<std::filesystem::path> paths;
    for (const auto& file : args.getMulti("file"))
    {
        paths.emplace_back(file);
    }
    for (const auto& dir : args.getMulti("dir"))
    {
        std::error_code error;
        std::filesystem::recursive_directory_iterator it(dir, error);
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
    std::vector<std::string> loadOrder;
    std::map<std::string, UnitStorage> unitStorage;
    if (args.contains("units"))
    {
        auto dir = args.getString("units");
        std::error_code error;
        auto files = readUnitFiles(dir, error);
        if (error)
        {
            std::cerr << "cannot read unit directory " << dir << ": " << error.message() << "\n";
            return 1;
        }
        if (files.empty())
        {
            std::cerr << "no *.FBI files under " << dir << "\n";
            return 1;
        }

        std::vector<std::string> stems;
        for (const auto& file : files)
        {
            stems.push_back(file.stem().string());
        }
        loadOrder = tadUnitLoadOrder(std::move(stems));
        unitStorage = readUnitStorage(files);
        std::cout << "unit load order: " << loadOrder.size() << " types from " << dir << "\n";
    }

    std::sort(paths.begin(), paths.end());

    if (paths.empty())
    {
        std::cerr << "no demos found\n";
        return 1;
    }

    auto emitAll = args.getBool("all");

    // How big an episode may get before it stops being readable, and how many
    // of one player's are worth keeping. Both are about the checked-in header
    // rather than about the data: a fortieth episode from one game says nothing
    // the first three did not.
    std::size_t maxTypes = args.contains("max-types") ? std::stoul(args.getString("max-types")) : 6;
    std::size_t maxPerPlayer = args.contains("max-per-player") ? std::stoul(args.getString("max-per-player")) : 3;

    if (args.contains("emit-cpp") && loadOrder.empty())
    {
        std::cerr << "--emit-cpp needs --units: an episode carries its unit types' own FBI values\n";
        return 1;
    }

    nlohmann::json resourceJson = nlohmann::json::array();
    std::vector<StorageEpisode> storageEpisodes;

    std::vector<rwe::Episode> all;
    std::map<std::string, unsigned int> rejectionCounts;
    unsigned int cleanCount = 0;

    for (const auto& path : paths)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            std::cerr << "cannot open " << path << "\n";
            return 1;
        }

        rwe::EpisodeHandler handler(path.filename().string());
        try
        {
            rwe::readTad(stream, handler);
        }
        catch (const rwe::TadException& e)
        {
            std::cerr << path.filename().string() << ": " << e.what() << "\n";
            return 1;
        }

        auto known = handler.unitTable ? handler.unitTable->knownDataSet() : std::nullopt;

        unsigned int clean = 0;
        for (const auto& episode : handler.episodes)
        {
            if (episode.clean())
            {
                ++clean;
            }
            for (const auto& reason : episode.rejections)
            {
                ++rejectionCounts[reason];
            }
        }
        cleanCount += clean;

        std::cout << path.filename().string()
                  << ": " << handler.episodes.size() << " builds paired, "
                  << clean << " clean"
                  << ", data set " << (known ? *known : std::string("unrecognised"))
                  << ", " << handler.orphanedFinishes << " finishes with no start"
                  << ", " << handler.inProgress.size() << " still building at the end\n";

        // A name is only as good as the data set it came from. The demo's own
        // 0x1a table carries the type count, so a --units pointed at the wrong
        // mod is caught here rather than quietly renaming every episode.
        if (!loadOrder.empty() && handler.unitTable
            && handler.unitTable->restricted.size() != loadOrder.size())
        {
            std::cerr << "  WARNING: " << path.filename().string() << " declares "
                      << handler.unitTable->restricted.size() << " unit types but --units gave "
                      << loadOrder.size() << "; names for this demo will be wrong\n";
        }

        if (args.contains("emit-cpp"))
        {
            auto mined = mineStorageEpisodes(handler, loadOrder, unitStorage, maxTypes, maxPerPlayer);
            std::cout << "  " << mined.size() << " storage episode(s)\n";
            storageEpisodes.insert(storageEpisodes.end(), mined.begin(), mined.end());
        }

        if (args.contains("emit-resources"))
        {
            resourceJson.push_back(resourcesToJson(handler));

            // A burst is numPlayers - 1 identical copies of one record. Report
            // both halves of that, per demo, so a demo that breaks the reading
            // says so where it is read rather than in a later analysis.
            std::map<unsigned int, unsigned int> burstSizes;
            for (const auto& r : handler.resourceRecords)
            {
                ++burstSizes[r.copies];
            }
            unsigned int modal = 0;
            unsigned int modalCount = 0;
            for (const auto& [size, count] : burstSizes)
            {
                if (count > modalCount)
                {
                    modal = size;
                    modalCount = count;
                }
            }
            std::cout << "  " << handler.resourceRecords.size() << " resource samples"
                      << ", modal burst " << modal << " of an expected "
                      << (handler.header.numPlayers - 1)
                      << ", " << handler.inconsistentBursts << " burst(s) disagreeing on the floats"
                      << " and " << handler.variantPrefixBursts << " on the prefix only\n";
        }

        for (auto& episode : handler.episodes)
        {
            if (emitAll || episode.clean())
            {
                all.push_back(std::move(episode));
            }
        }
    }

    std::cout << "\n"
              << paths.size() << " demos, " << all.size() << " episodes emitted, "
              << cleanCount << " clean\n";

    if (!rejectionCounts.empty())
    {
        std::cout << "rejections:";
        for (const auto& [reason, count] : rejectionCounts)
        {
            std::cout << " " << reason << " x" << count;
        }
        std::cout << "\n";
    }

    // The number an oracle actually consumes is the MODE, not the mean: a build
    // runs at full rate unless something interferes, assists shorten it and
    // missed micro-stalls lengthen it, so the modal duration is the unassisted,
    // unimpeded one and the spread either side is the interference. Over demo
    // 14724 that mode is sharp -- 66 of 87 builds of one type land on exactly
    // 596 ticks -- which is what makes this worth reporting.
    {
        std::map<uint16_t, std::map<uint32_t, unsigned int>> durations;
        for (const auto& episode : all)
        {
            if (episode.clean())
            {
                ++durations[episode.typeIndex][episode.durationTicks()];
            }
        }

        std::cout << (loadOrder.empty()
                ? "\nmodal build times, by anonymous type index:\n"
                : "\nmodal build times, by unit type:\n")
                  << "  type            n    mode   share\n";
        std::vector<std::pair<uint16_t, const std::map<uint32_t, unsigned int>*>> byCount;
        for (const auto& [type, histogram] : durations)
        {
            byCount.emplace_back(type, &histogram);
        }
        std::sort(byCount.begin(), byCount.end(), [](const auto& a, const auto& b) {
            auto total = [](const auto& h) {
                unsigned int n = 0;
                for (const auto& [duration, count] : *h)
                {
                    n += count;
                }
                return n;
            };
            return total(a.second) > total(b.second);
        });

        for (const auto& [type, histogram] : byCount)
        {
            unsigned int total = 0;
            uint32_t mode = 0;
            unsigned int modeCount = 0;
            for (const auto& [duration, count] : *histogram)
            {
                total += count;
                if (count > modeCount)
                {
                    mode = duration;
                    modeCount = count;
                }
            }

            if (total < 5)
            {
                continue;
            }

            std::cout << "  " << std::left << std::setw(12) << typeLabel(loadOrder, type) << std::right
                      << std::setw(5) << total
                      << std::setw(8) << mode
                      << std::setw(7) << (100 * modeCount / total) << "%\n";
        }
    }

    if (args.contains("emit-cpp"))
    {
        auto out = args.getString("emit-cpp");
        if (!emitCpp(out, storageEpisodes))
        {
            std::cerr << "cannot write " << out << "\n";
            return 1;
        }
    }

    if (args.contains("emit-resources"))
    {
        auto out = args.getString("emit-resources");
        std::ofstream file(out);
        if (!file)
        {
            std::cerr << "cannot write " << out << "\n";
            return 1;
        }
        file << resourceJson.dump(2) << "\n";
        std::cout << "wrote " << out << "\n";
    }

    if (args.contains("emit-json"))
    {
        auto out = args.getString("emit-json");
        nlohmann::json j = nlohmann::json::array();
        for (const auto& episode : all)
        {
            j.push_back(toJson(episode, loadOrder));
        }

        std::ofstream file(out);
        if (!file)
        {
            std::cerr << "cannot write " << out << "\n";
            return 1;
        }
        file << j.dump(2) << "\n";
        std::cout << "wrote " << out << "\n";
    }

    return 0;
}
