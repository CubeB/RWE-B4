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
//                     [--units <dir>] [--emit-json <path>] [--all]
//   --file        a .tad or .ted demo to mine; may be repeated
//   --dir         a directory of demos to mine; recurses
//   --units       a directory of the data set's unit files, recursively
//                 scanned for *.FBI, used to name each type index
//   --emit-json   write the episodes to a file as JSON
//   --all         emit rejected episodes too, each with its reasons

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/util/OpaqueArgs.h>
#include <set>
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

            unsigned int speedChanges = 0;
            unsigned int orphanedFinishes = 0;

            explicit EpisodeHandler(std::string demo) : demo(std::move(demo)) {}

            void onHeader(const TadHeader& h) override { header = h; }

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

                // A watcher's record is all zeros but for two slots and would
                // otherwise read as a permanent stall. Filter it on the shape
                // that identifies it rather than on the player table, which does
                // not survive between recordings of the same game.
                auto watcher = e->metalStorage == 0.0f && e->energyStorage == 0.0f;
                if (watcher)
                {
                    return;
                }

                // Every owner block whose builds this record could spoil. The
                // sender's own block is the one it reports on; there is no id in
                // the record to say so, so attribute it by sender's own units.
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
    std::vector<std::string> readUnitStems(const std::filesystem::path& dir, std::error_code& error)
    {
        std::vector<std::string> stems;
        std::filesystem::recursive_directory_iterator it(dir, error);
        if (error)
        {
            return stems;
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
                stems.push_back(entry.path().stem().string());
            }
        }

        return stems;
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
    if (args.contains("units"))
    {
        auto dir = args.getString("units");
        std::error_code error;
        auto stems = readUnitStems(dir, error);
        if (error)
        {
            std::cerr << "cannot read unit directory " << dir << ": " << error.message() << "\n";
            return 1;
        }
        if (stems.empty())
        {
            std::cerr << "no *.FBI files under " << dir << "\n";
            return 1;
        }

        loadOrder = tadUnitLoadOrder(std::move(stems));
        std::cout << "unit load order: " << loadOrder.size() << " types from " << dir << "\n";
    }

    std::sort(paths.begin(), paths.end());

    if (paths.empty())
    {
        std::cerr << "no demos found\n";
        return 1;
    }

    auto emitAll = args.getBool("all");

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
