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
//   --emit-shots  write every 0x0d, 0x0b and 0x0c as JSON Lines, for the
//                 weapon-event pairing work; see writeShotJson
//   --emit-cpp    write the storage episodes as a C++ header for rwe_test
//   --emit-build-cpp  write the build-timing episodes as a C++ header
//   --cells       print the (builder, product) build-timing cells, which is
//                 what tools/tad-buildtime.py scores
//   --weapon-slots  check a 0x0d's trailing byte against the slots the
//                 shooter's own FBI fills; the evidence that it is a weapon index
//   --max-types   distinct unit types an episode may carry (default 6)
//   --max-per-player  episodes to keep per player (default 3)
//   --max-cells   build-timing episodes to check in (default 25)
//   --min-builds  builds a cell needs before its mode is used (default 5)
//   --all         emit rejected episodes too, each with its reasons

#include <algorithm>
#include <array>
#include <bit>
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
#include <rwe/io/weapontdf/WeaponTdf.h>
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

            /**
             * One 0x0d, with the sender's tick, for --weapon-slots and
             * --emit-shots.
             *
             * WHOSE TICK, and why the sender is kept beside it. Each peer emits
             * the events for its own units against its own 0x2c serial, so the
             * sender is what says which clock a tick is on. That mattered when
             * this was written because a shot and its damage looked like they
             * might be stamped by two different peers; they are not (see
             * DamageRecord), and the sender stays because it is the evidence for
             * that rather than a field anything now has to correct for.
             */
            struct ShotRecord
            {
                uint8_t sender;
                uint32_t tick;
                TadShot shot;
            };

            std::vector<ShotRecord> shots;

            /**
             * One 0x0b, with the sender's tick.
             *
             * THE SENDER IS THE ATTACKER'S OWNER, not the victim's, which is the
             * single most useful thing the dump established: 797,783 of the
             * corpus's damage records are sent by the peer that owns the
             * attacker and NOT ONE by the peer that owns the victim. So a shot
             * and the damage it caused are stamped by the same peer's 0x2c
             * clock, and a flight time is a difference within one clock rather
             * than across two. The residual 27,061 are records with no
             * attributable attacker at all -- 21,013 of them carry attacker id
             * 0, which is terrain, decay or self-damage -- rather than
             * counter-examples.
             */
            struct DamageRecord
            {
                uint8_t sender;
                uint32_t tick;
                TadDamage damage;
            };

            std::vector<DamageRecord> damageRecords;

            /** One 0x0c, with the sender's tick. */
            struct DeathRecord
            {
                uint8_t sender;
                uint32_t tick;
                TadDeath death;
            };

            std::vector<DeathRecord> deathRecords;

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
                            onDeath(packet.sender, s);
                            break;

                        case TadSubPacketCode::PlayerResourceInfo:
                            onResources(packet.sender, s);
                            break;

                        case TadSubPacketCode::WeaponFired:
                            if (auto shot = tadDecodeShot(s))
                            {
                                shots.push_back(ShotRecord{packet.sender, tick[packet.sender], *shot});
                            }
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
                damageRecords.push_back(DamageRecord{sender, tick[sender], *e});
            }

            void onDeath(uint8_t sender, const TadBytes& s)
            {
                auto e = tadDecodeDeath(s);
                if (!e)
                {
                    return;
                }

                inProgress.erase(e->unitId);
                deathRecords.push_back(DeathRecord{sender, tick[sender], *e});
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
     *
     * The storage pair explains a 0x28 capacity; buildTime and workerTime
     * explain a nanoframe's duration, and maxVelocity and canFly decide which
     * of the three scoring classes a builder falls into. One reader, because a
     * second one could disagree with this about what a field means.
     */
    struct UnitFacts
    {
        float metalStorage;
        float energyStorage;
        unsigned int buildTime;
        unsigned int workerTime;
        float maxVelocity;
        bool canFly;

        /**
         * Which of Weapon1/Weapon2/Weapon3 the FBI actually fills, as bits 0-2.
         * A mask and not a count: the standard TA convention puts a unit's
         * anti-air weapon in slot 3 and leaves slot 2 empty, so counting weapons
         * makes a two-weapon unit look as though it may only fire slots 0 and 1
         * when the slots it really has are 0 and 2.
         */
        unsigned int weaponSlotMask;

        /**
         * What the FBI puts in Weapon1/Weapon2/Weapon3, empty where the slot is
         * empty. The 0x0d's trailing byte indexes this array directly, which is
         * what turns a shot into a *weapon* rather than merely a shooter.
         */
        std::array<std::string, 3> weaponNames;
    };

    /**
     * One weapon block, out of the data set's own weapon TDFs.
     *
     * A SECOND READER OVER A DIFFERENT DIRECTORY, which is the one place the
     * "widen UnitFacts rather than adding a reader" rule does not apply: weapons
     * are genuinely a second file format in a second place, and an FBI names a
     * weapon without describing it. It goes through the engine's own
     * parseWeaponTdf, exactly as readUnitFacts goes through parseUnitFbi, so a
     * checked-in fixture cannot disagree with the loader about what a field
     * means.
     */
    struct WeaponFacts
    {
        /** TDF weaponvelocity, in world units a SECOND; divide by 30 for a tick. */
        unsigned int velocity;

        /** Zero in the TDF means "off the rail at full speed", not "stationary". */
        unsigned int startVelocity;
        unsigned int acceleration;
        unsigned int range;

        /** Rounds per trigger pull. Anything above 1 is why a shot cannot be isolated. */
        unsigned int burst;

        bool ballistic;
        bool vLaunch;
        bool waterWeapon;
        bool lineOfSight;

        /** The [DAMAGE] block's `default`, which is what a paired 0x0b should carry. */
        unsigned int defaultDamage;
    };

    /**
     * Every weapon TDF under dir.
     *
     * The VFS path is `weapons\*.tdf`, but a mod's extracted tree renames the
     * directory -- Escalation's is `weaponE` -- so the match is on the directory
     * name CONTAINING "weapon" rather than equalling "weapons". Widening it to
     * every .tdf under the tree would sweep in sound, GUI and feature
     * definitions, which parse as weapons with every field defaulted and would
     * be indistinguishable from a real one with no velocity.
     */
    std::map<std::string, WeaponFacts> readWeaponFacts(const std::filesystem::path& dir, std::error_code& error)
    {
        std::map<std::string, WeaponFacts> facts;
        std::filesystem::recursive_directory_iterator it(dir, error);
        if (error)
        {
            return facts;
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
            if (extension != ".tdf")
            {
                continue;
            }

            auto parent = entry.path().parent_path().filename().string();
            for (auto& c : parent)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (parent.find("weapon") == std::string::npos)
            {
                continue;
            }

            std::ifstream stream(entry.path(), std::ios::binary);
            if (!stream)
            {
                continue;
            }
            std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

            try
            {
                for (const auto& [name, weapon] : parseWeaponTdf(parseTdfFromString(contents)))
                {
                    auto key = name;
                    for (auto& c : key)
                    {
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    }

                    unsigned int defaultDamage = 0;
                    for (const auto& [category, value] : weapon.damage)
                    {
                        if (category == "DEFAULT" || category == "default")
                        {
                            defaultDamage = value;
                        }
                    }

                    facts[key] = WeaponFacts{
                        weapon.weaponVelocity,
                        weapon.startVelocity,
                        weapon.weaponAcceleration,
                        weapon.range,
                        weapon.burst,
                        weapon.ballistic,
                        weapon.vLaunch,
                        weapon.waterWeapon,
                        weapon.lineOfSight,
                        defaultDamage};
                }
            }
            catch (const std::exception&)
            {
                // Same as readUnitFacts: a file the parser will not take cannot
                // contribute a number, and a cell missing one is dropped rather
                // than emitted with a hole in it.
            }
        }

        return facts;
    }

    std::map<std::string, UnitFacts> readUnitFacts(const std::vector<std::filesystem::path>& files)
    {
        std::map<std::string, UnitFacts> facts;
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
                facts[name] = UnitFacts{
                    static_cast<float>(fbi.metalStorage),
                    static_cast<float>(fbi.energyStorage),
                    fbi.buildTime,
                    fbi.workerTime,
                    fbi.maxVelocity,
                    fbi.canFly,
                    static_cast<unsigned int>(
                        (fbi.weapon1.empty() ? 0u : 1u) | (fbi.weapon2.empty() ? 0u : 2u)
                        | (fbi.weapon3.empty() ? 0u : 4u)),
                    std::array<std::string, 3>{fbi.weapon1, fbi.weapon2, fbi.weapon3}};
            }
            catch (const std::exception&)
            {
                // A file the parser will not take cannot contribute a number to
                // an episode, and an episode missing one is dropped below rather
                // than emitted with a hole in it.
            }
        }
        return facts;
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
        const std::map<std::string, UnitFacts>& unitFacts)
    {
        std::map<std::string, unsigned int> counts;
        for (const auto& name : names)
        {
            ++counts[name];
        }

        std::vector<CompositionEntry> out;
        for (const auto& [name, count] : counts)
        {
            auto storage = unitFacts.find(name);
            if (storage == unitFacts.end())
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
        const std::map<std::string, UnitFacts>& unitFacts,
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

                    auto storage = unitFacts.find(*name);
                    if (storage == unitFacts.end())
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
                    auto finished = aggregate(finishedNames, unitFacts);
                    auto building = aggregate(buildingNames, unitFacts);

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
    bool writeGeneratedHeader(const std::filesystem::path& path, const std::string& generated)
    {
        std::string existing;
        {
            std::ifstream stream(path, std::ios::binary);
            if (stream)
            {
                existing.assign((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            }
        }

        if (existing == generated)
        {
            std::cout << "unchanged: " << path.string() << "\n";
            return true;
        }

        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            return false;
        }
        out << generated;
        std::cout << (existing.empty() ? "wrote " : "CHANGED ") << path.string() << "\n";
        return static_cast<bool>(out);
    }

    // --- --emit-build-cpp: build-timing episodes -----------------------------
    //
    // A build-timing episode is one (builder type, product type) cell of the
    // corpus, consumed as the MODE of its durations. The model being pinned is
    // TA's completion arithmetic: a builder contributes p = WorkerTime / 30
    // build units a tick, the first increment lands on the 0x09's own tick, and
    // the job finishes when a single-precision fraction counted up by
    // p / BuildTime passes 1.0f. tools/tad-buildtime.py is the same arithmetic
    // and is the re-runnable check; this is the port that feeds the fixture.
    //
    // This is a corpus-wide pass, unlike the storage miner, because a cell pools
    // across demos: a pair that appears six times in each of four games is one
    // observation with 24 builds behind it.

    /** One (builder, product) pair of the corpus, with its modal duration. */
    struct BuildCell
    {
        std::string builder;
        std::string product;
        unsigned int buildTime;
        unsigned int workerTime;

        /** The builder's contribution a tick, WorkerTime / 30, integer division. */
        unsigned int p;

        /** How many builds survived the outlier cap, and how many hit the mode. */
        unsigned int builds;
        unsigned int buildsAtMode;

        /** The modal duration in ticks, which is the number to consume. */
        uint32_t mode;

        /** What TA's float32 fraction predicts, as a duration. */
        unsigned int floatModel;

        /** What RWE's integer accumulator predicts, as a duration. */
        unsigned int integerModel;

        /** immobile, airborne or ground: the three classes that score apart. */
        std::string kind;

        /** One build that landed on the mode, for the episode's provenance. */
        std::string demo;
        uint32_t startTick;
        uint32_t finishTick;
    };

    /**
     * How many increments TA needs, by replaying its accumulator.
     *
     * Every operation is a float32 one and has to stay that way: doing the same
     * sum in double precision gets 6 of the 15 divisible pairs right where this
     * gets all 15, which is what says the arithmetic is genuinely 32-bit.
     */
    unsigned int tadTicksToBuild(unsigned int buildTime, unsigned int p)
    {
        auto x = static_cast<float>(p) / static_cast<float>(buildTime);
        auto frac = 0.0f;
        unsigned int n = 0;
        while (frac <= 1.0f && n < 400000)
        {
            frac = frac + x;
            ++n;
        }
        return n;
    }

    /**
     * How many increments RWE needs. UnitState::addBuildProgress clamps the
     * contribution to what is left and finishes on equality, so this is a plain
     * ceiling -- and where BuildTime divides exactly by p, that is a tick fewer
     * than TA takes. docs/TOTALA-EXE.md section 88.
     */
    unsigned int rweTicksToBuild(unsigned int buildTime, unsigned int p)
    {
        return (buildTime + p - 1) / p;
    }

    /** Which of the three scoring classes a builder belongs to. */
    std::string builderClass(const UnitFacts& facts)
    {
        if (facts.maxVelocity == 0.0f)
        {
            return "immobile";
        }
        return facts.canFly ? "airborne" : "ground";
    }

    /**
     * The build a unit id was carrying at a tick -- the last one to finish at or
     * before it -- as (finish tick, name).
     *
     * Both places that name a unit from its id go through here, because the two
     * disagreeing is exactly the fault this exists to prevent: the build cells
     * kept the FIRST name an id ever held while --weapon-slots scoped, and the
     * gap between the two predicates was 453 failures against 14. The finish
     * tick comes back with the name because how stale a name is is what
     * separates a recycled id from a genuine reading.
     */
    std::optional<std::pair<uint32_t, std::string>> buildAtTick(
        const std::vector<std::pair<uint32_t, std::string>>& builds,
        uint32_t at)
    {
        std::optional<std::pair<uint32_t, std::string>> best;
        for (const auto& build : builds)
        {
            if (build.first <= at)
            {
                best = build;
            }
        }
        return best;
    }

    /**
     * (builder type, product type) cells over the whole corpus.
     *
     * A builder's own type is known only where the builder was itself built
     * during the recording, which is what makes builder-keyed rows possible at
     * all: pair a 0x12's builderId back to the unitId of an earlier episode,
     * scoped to the build that id was carrying when this one started. That is
     * also what limits how many cells there are.
     */
    std::vector<BuildCell> mineBuildCells(
        const std::vector<Episode>& episodes,
        const std::vector<std::string>& loadOrder,
        const std::map<std::string, UnitFacts>& unitFacts,
        const std::set<std::string>& wrongDataSet,
        unsigned int minBuilds)
    {
        auto nameOf = [&](const Episode& e) -> std::optional<std::string> {
            return tadUnitNameForTypeIndex(loadOrder, e.typeIndex);
        };

        // A cell pools across demos, so unlike the storage miner this one can be
        // poisoned by a single demo recorded on another data set: its type
        // indices would name the wrong units and merge into somebody else's
        // cells. The demo's own 0x1a table says how many types it had, so a
        // disagreement with --units is grounds to drop it rather than to print a
        // warning and hope. That is what keeps --dir over a mixed corpus honest.

        // Each id's builds, oldest first, so a builder can be named as of a tick
        // rather than for all time. Scoping matters because TA recycles unit ids
        // heavily -- in 14725, 2,622 of 4,093 distinct ids are reused by a later
        // nanoframe and 2,550 of those by a different type -- so keeping the
        // FIRST name an id ever held looks most builders up under a stale name.
        // The reference script scopes the same way, and scoping moved no scored
        // cell's mode: a stale name either carries a different WorkerTime, which
        // puts the duration outside the outlier cap and drops the build, or the
        // same one (every stock factory is p = 4), which lands it in the wrong
        // cell with the right duration, since only p and the product's BuildTime
        // enter the arithmetic. What it buys is evidence -- builds roughly
        // double and four more pairs clear --min-builds.
        std::map<std::pair<std::string, uint16_t>, std::vector<std::pair<uint32_t, std::string>>> lives;
        for (const auto& e : episodes)
        {
            if (wrongDataSet.count(e.demo) != 0)
            {
                continue;
            }
            if (auto name = nameOf(e))
            {
                lives[std::make_pair(e.demo, e.unitId)].emplace_back(e.finishTick, *name);
            }
        }
        for (auto& [id, list] : lives)
        {
            std::sort(list.begin(), list.end());
        }

        auto nameAt = [&](const std::string& demo, uint16_t id, uint32_t at) -> std::optional<std::string> {
            auto it = lives.find(std::make_pair(demo, id));
            if (it == lives.end())
            {
                return std::nullopt;
            }
            if (auto build = buildAtTick(it->second, at))
            {
                return build->second;
            }
            return std::nullopt;
        };

        // Grouped in first-appearance order, because the mode's tie-break is
        // first-encountered and the port has to agree with the script's on the
        // cells where two durations are equally common.
        std::map<std::pair<std::string, std::string>, std::vector<const Episode*>> grouped;
        for (const auto& e : episodes)
        {
            if (wrongDataSet.count(e.demo) != 0)
            {
                continue;
            }

            auto builder = nameAt(e.demo, e.builderId, e.startTick);
            auto product = nameOf(e);
            if (!builder || !product)
            {
                continue;
            }
            if (unitFacts.count(*builder) == 0 || unitFacts.count(*product) == 0)
            {
                continue;
            }
            grouped[std::make_pair(*builder, *product)].push_back(&e);
        }

        std::vector<BuildCell> out;
        for (const auto& [pair, builds] : grouped)
        {
            const auto& builderFacts = unitFacts.at(pair.first);
            const auto& productFacts = unitFacts.at(pair.second);
            auto p = builderFacts.workerTime / 30;
            if (p == 0 || productFacts.buildTime == 0)
            {
                continue;
            }

            auto floatModel = tadTicksToBuild(productFacts.buildTime, p) - 1;

            // Assisted builds are the bulk of a competitive game and only ever
            // shorten; a cap either side keeps them and the badly stalled ones
            // from dragging the mode off the unassisted build.
            std::vector<const Episode*> kept;
            for (const auto* e : builds)
            {
                auto delta = static_cast<long long>(e->durationTicks()) - static_cast<long long>(floatModel);
                if (delta >= -20 && delta <= 120)
                {
                    kept.push_back(e);
                }
            }
            if (kept.size() < minBuilds)
            {
                continue;
            }

            std::map<uint32_t, unsigned int> histogram;
            uint32_t mode = 0;
            unsigned int atMode = 0;
            for (const auto* e : kept)
            {
                auto count = ++histogram[e->durationTicks()];
                if (count > atMode)
                {
                    mode = e->durationTicks();
                    atMode = count;
                }
            }

            // The representative build is the earliest one that landed on the
            // mode, so the provenance is stable under regeneration.
            const Episode* representative = nullptr;
            for (const auto* e : kept)
            {
                if (e->durationTicks() != mode)
                {
                    continue;
                }
                if (representative == nullptr
                    || std::tie(e->demo, e->startTick, e->unitId)
                        < std::tie(representative->demo, representative->startTick, representative->unitId))
                {
                    representative = e;
                }
            }

            out.push_back(BuildCell{
                pair.first,
                pair.second,
                productFacts.buildTime,
                builderFacts.workerTime,
                p,
                static_cast<unsigned int>(kept.size()),
                atMode,
                mode,
                floatModel,
                rweTicksToBuild(productFacts.buildTime, p) - 1,
                builderClass(builderFacts),
                representative->demo,
                representative->startTick,
                representative->finishTick});
        }

        return out;
    }

    /**
     * Picks the cells worth checking in, and writes them.
     *
     * The storage miner's selection rule -- one episode per distinct set of
     * storage-granting types -- does not transfer, because a cell already IS an
     * aggregate: every one of them asserts something different by construction.
     * So the rule here is one episode per scored cell, capped, and the cap
     * prefers the cells where the two models part company: all of the ones whose
     * BuildTime divides exactly by p, because those carry the ten deltas, and
     * then the most-observed of the rest, because a mode over 249 builds is a
     * stronger observation than one over 5.
     *
     * A SEPARATE HEADER from tad_economy_episodes.h, deliberately. The two share
     * no struct, are mined from different passes -- storage per demo, build
     * timing corpus-wide, because a cell pools across games -- and are
     * regenerated from different corpora, so one regeneration should never churn
     * the other's diff.
     */
    void writeBuildEpisodes(std::ostream& out, std::vector<BuildCell> cells, std::size_t maxCells)
    {
        // A cell the float32 model does not predict is an unexplained
        // observation, and checking one in would write a mystery into
        // expectedDurationDelta as though it were a licensed divergence -- the
        // one thing that field exists to prevent. There are none over the
        // Escalation corpus; if one ever appears, tools/tad-buildtime.py exits
        // non-zero at the same moment and that is where to start.
        std::vector<BuildCell> scored;
        unsigned int unexplained = 0;
        for (auto& cell : cells)
        {
            if (cell.kind != "immobile")
            {
                continue;
            }
            if (cell.mode != cell.floatModel)
            {
                std::cout << "  skipping " << cell.builder << " -> " << cell.product
                          << ": corpus says " << cell.mode << ", the model says " << cell.floatModel << "\n";
                ++unexplained;
                continue;
            }
            scored.push_back(std::move(cell));
        }

        std::stable_sort(scored.begin(), scored.end(), [](const BuildCell& a, const BuildCell& b) {
            auto rank = [](const BuildCell& c) { return c.buildTime % c.p == 0 ? 0 : 1; };
            return std::make_tuple(rank(a), b.builds, a.builder, a.product)
                < std::make_tuple(rank(b), a.builds, b.builder, b.product);
        });
        if (scored.size() > maxCells)
        {
            scored.resize(maxCells);
        }

        // Emitted in name order, so a regeneration that adds a cell inserts one
        // row rather than reshuffling the table.
        std::sort(scored.begin(), scored.end(), [](const BuildCell& a, const BuildCell& b) {
            return std::tie(a.builder, a.product) < std::tie(b.builder, b.product);
        });

        unsigned int divergent = 0;
        for (const auto& cell : scored)
        {
            divergent += cell.integerModel == cell.mode ? 0 : 1;
        }
        std::cout << scored.size() << " build episode(s), " << divergent
                  << " of them where RWE is expected to differ";
        if (unexplained != 0)
        {
            std::cout << ", " << unexplained << " cell(s) skipped as unexplained";
        }
        std::cout << "\n";

        out << R"(#pragma once

// GENERATED FILE -- do not edit by hand. Regenerate with tad_episodes
// --emit-build-cpp; the command, and the corpus it needs, are in
// docs/TA-DEMOS.md.
//
// Episodes mined from real Total Annihilation games, for the conformance tests
// in buildtime.test.cpp. Its sibling, tad_economy_episodes.h, holds the storage
// ones; they share no struct and are mined by different passes, so they are kept
// apart and regenerate independently.
//
// WHY THIS IS A HEADER OF STRUCTS AND NOT A DATA FILE. rwe_test is hermetic --
// it reads no files, mounts no VFS and opens no archive -- and it stays that
// way. Demos and mod files never enter the repository either. So the numbers
// travel as source: each unit's own FBI values are transcribed inline beside the
// observation they explain, and a test can be read without either.
//
// WHAT AN EPISODE IS. One (builder type, product type) cell of the corpus: every
// build of that product by that builder, pooled across the games it appeared in,
// consumed as the MODE of its durations. The mode and not the mean, because a
// build runs at full rate unless something interferes -- an assist shortens it
// and a missed micro-stall lengthens it -- so the modal duration is the
// unassisted, unimpeded one and the spread either side is the interference.
//
// WHICH BUILDS MAY BE HERE. Only builds by an IMMOBILE builder, which is to say
// a factory: there is nothing for it to walk to and nothing for it to deploy, so
// the duration is the nanolathe and nothing else. A ground mobile builder pays
// its own COB deploy sequence before INBUILDSTANCE, which is the mod's data
// rather than the engine's behaviour. An airborne one finishes consistently one
// tick early against the model, for a reason nobody has yet read out of the
// binary (docs/TOTALA-EXE.md section 91), and writing that into
// expectedDurationDelta would launder an open question into a licensed
// divergence, which is the one thing that field exists to prevent. See
// docs/TA-DEMOS.md.
//
// THE ARITHMETIC BEING PINNED. A builder contributes p = WorkerTime / 30 build
// units a tick -- integer division, in the engine and in the demo tooling alike.
// The first increment lands on the tick the nanoframe appears, so an episode's
// duration is one less than the number of increments. The original finishes when
// a single-precision fraction counted up by p / BuildTime passes 1.0f; RWE adds
// p to an unsigned counter and finishes at BuildTime. Those two agree except
// where BuildTime divides exactly by p, and expectedDurationDelta is where they
// do not.

namespace rwe
{
    /** One (builder, product) cell of the corpus, consumed as its modal duration. */
    struct TadBuildEpisode
    {
        /**
         * Provenance. builds is how many builds of this pair the corpus held
         * after the outlier cap, buildsAtMode how many of them landed on the
         * mode, and the tick pair is one representative build that did -- out of
         * the named demo, which is not necessarily the only game the cell pooled
         * over.
         */
        const char* demo;
        const char* builderName;
        const char* productName;
        unsigned int startTick;
        unsigned int finishTick;
        unsigned int builds;
        unsigned int buildsAtMode;

        /** The builder's own WorkerTime and the product's own BuildTime, from the FBI. */
        unsigned int workerTime;
        unsigned int buildTime;

        /** Observed: finishTick - startTick, at the mode. */
        unsigned int modeDurationTicks;

        /**
         * What RWE is expected to differ by, and why.
         *
         * A conformance test that asserts equality gets disabled the first time
         * it is right to fail, so an episode asserts the observation plus a
         * known delta instead. expectedDifference names the docs/TOTALA-EXE.md
         * section 88 entry that licences a non-zero one, and is null where there
         * is nothing to excuse.
         *
         * Unlike the storage episodes' deltas, these are not hand-written: the
         * two completion models are both small enough to replay, so the emitter
         * computes the difference between them. That is not circular. A test
         * asserting mode + delta still fails if the engine stops matching its
         * own model, and it fails on exactly these rows -- and no others -- if
         * the engine is changed to the original's float, which is what says the
         * deltas are the divergence section 88 describes rather than a fudge
         * that happens to fit.
         */
        int expectedDurationDelta;
        const char* expectedDifference;
    };

    // clang-format off
    inline constexpr TadBuildEpisode tadBuildEpisodes[] = {
)";

        for (const auto& cell : scored)
        {
            auto delta = static_cast<int>(cell.integerModel) - static_cast<int>(cell.mode);
            out << "        {\"" << cell.demo << "\", \"" << cell.builder << "\", \"" << cell.product << "\",\n"
                << "            " << cell.startTick << ", " << cell.finishTick << ", "
                << cell.builds << ", " << cell.buildsAtMode << ",\n"
                << "            " << cell.workerTime << ", " << cell.buildTime << ", "
                << cell.mode << ",\n"
                << "            " << delta << ", "
                << (delta == 0
                           ? std::string("nullptr")
                           : "\"TOTALA-EXE.md 88: build progress is an integer accumulator, not the original's float\"")
                << "},\n";
        }

        out << "    };\n"
            << "    // clang-format on\n"
            << "}\n";
    }

    // --- --weapon-slots: what the last byte of a 0x0d indexes -----------------
    //
    // The byte is a 0-based index into the shooter's own Weapon1/Weapon2/Weapon3,
    // and this mode is the evidence. For every unit type the corpus caught
    // firing, it collects the set of slots that type was seen using and checks
    // each against the slots that type's FBI actually fills.
    //
    // NAMING A SHOOTER IS THE WHOLE DIFFICULTY. TA recycles unit ids heavily --
    // in 14725, 2,622 of 4,093 distinct ids are reused by a later nanoframe and
    // 2,550 of those by a different type -- so a map that keeps the FIRST name
    // an id ever held names most shooters wrongly, and the wrong name is what
    // makes a metal extractor appear to open fire. Scoping the name to the id's
    // most recent build BEFORE the shot fixes it, and takes the occupancy test
    // from 453 failures to 14. mineBuildCells now scopes the same way, and
    // unscopedViolating below is what is left of the contrast: the naive tally
    // kept for the argument, not a second implementation in use anywhere.

    /** What one unit type was seen doing, against what its FBI allows. */
    struct WeaponSlotTally
    {
        unsigned int consistent = 0;
        unsigned int violating = 0;

        /**
         * The same tally under the naive predicate `slot < number of weapons`.
         * Carried purely for the contrast: the gap between the two is the
         * Weapon1-and-Weapon3 convention, and it is the argument.
         */
        unsigned int naiveViolating = 0;

        /**
         * The same tally again under the occupancy predicate but with shooters
         * named by the FIRST name an id ever held. Carried for the second
         * contrast: the gap between this and `violating` is what unit-id
         * recycling costs, and it is the argument that made the build cells
         * scope too.
         */
        unsigned int unscopedViolating = 0;
        unsigned long shots = 0;
        unsigned long named = 0;
        unsigned long aimedAtUnit = 0;
        std::map<unsigned int, unsigned long> slotCounts;

        /**
         * How many shots the violating observations account for, against the
         * conforming ones, and how stale the name behind each shot was.
         *
         * These two were meant to separate the competing explanations for a
         * violation -- a handful of very stale shots would be recycled unit ids
         * and nothing more; a large share, or shots landing promptly after their
         * own build, would mean 0x0d covers something besides weapons. Over the
         * corpus the answer came out in between and neither story covers the
         * fourteen: 7,180 shots, median staleness 10,008 ticks against a
         * conforming 7,083. See docs/TA-DEMOS.md, the 0x0d section.
         */
        unsigned long violatingShots = 0;
        unsigned long conformingShots = 0;
        std::vector<uint32_t> violatingAges;
        std::vector<uint32_t> conformingAges;
    };

    /**
     * Each unit id's builds, newest last, so a name can be asked for as of a
     * tick rather than for all time.
     */
    std::map<uint16_t, std::vector<std::pair<uint32_t, std::string>>> unitLives(
        const EpisodeHandler& handler,
        const std::vector<std::string>& loadOrder)
    {
        std::map<uint16_t, std::vector<std::pair<uint32_t, std::string>>> lives;
        for (const auto& e : handler.episodes)
        {
            if (auto name = tadUnitNameForTypeIndex(loadOrder, e.typeIndex))
            {
                lives[e.unitId].emplace_back(e.finishTick, *name);
            }
        }
        for (auto& [id, list] : lives)
        {
            std::sort(list.begin(), list.end());
        }
        return lives;
    }

    // --- --emit-shots: the raw weapon events, for arguing pairing in Python ---
    //
    // This is a dump and not a miner, deliberately. A weapon oracle needs a shot
    // paired with the damage it caused, and NOTHING IN THE STREAM LINKS THEM:
    // there is no shot id, no sequence number, and no tick on a 0x0b beyond the
    // 0x2c serial of the packet carrying it, against 631,578 shots and 824,844
    // damage events over the corpus. A unit firing a burst has several shots in
    // flight and several damage events arriving, and nothing says which came
    // from which. So time-of-flight is a filtered statistic, the filters are the
    // job, and the filters get argued over the JSON before a line of C++ miner
    // is written -- which is the order that caught every mistake in the
    // build-timing pass.
    //
    // Three things the dump has to preserve because a summary would destroy
    // them:
    //
    //  * THE SENDER of every record. Each peer emits the events for its own
    //    units against its own tick clock, so a shot is stamped by the shooter's
    //    owner and the damage it causes by the victim's owner. A flight time
    //    measured across the two is a difference between two clocks, and whether
    //    those clocks agree is the first thing to check rather than the thing to
    //    assume.
    //  * THE GEOMETRY. Origin and aim point are both in the 0x0d, and the
    //    distance between them is what any flight time has to be explained
    //    against.
    //  * DEATHS as well as damage. 0x0b is not a complete ledger -- 17,526 of
    //    the corpus's script-driven deaths carry no recorded damage on the
    //    victim at all -- so a killing blow may arrive only as a 0x0c. Absence
    //    is unknown, never zero.
    //
    // JSON LINES, not a JSON array, because the corpus is about 1.5 million
    // records and a pretty-printed array of them is neither writable in one pass
    // nor loadable in one go. One object per line, streamed per demo:
    //
    //     import json
    //     rows = [json.loads(line) for line in open(path)]
    //
    // Names are scoped through buildAtTick, the same way --weapon-slots names a
    // shooter, so a consumer never has to redo the load-order lookup and can
    // never do it differently.

    /** Writes one demo's shots, damage and deaths as JSON Lines. */
    void writeShotJson(
        std::ostream& out,
        const EpisodeHandler& handler,
        const std::vector<std::string>& loadOrder)
    {
        auto lives = unitLives(handler, loadOrder);
        auto nameOf = [&](uint16_t id, uint32_t at) -> std::string {
            auto it = lives.find(id);
            if (it == lives.end())
            {
                return "";
            }
            auto build = buildAtTick(it->second, at);
            return build ? build->second : std::string();
        };

        auto position = [](std::ostream& o, const char* prefix, const TadPosition& p) {
            o << ",\"" << prefix << "x\":" << tadFixedToDouble(p.x)
              << ",\"" << prefix << "y\":" << tadFixedToDouble(p.y)
              << ",\"" << prefix << "z\":" << tadFixedToDouble(p.z);
        };

        out << std::fixed << std::setprecision(3);

        for (const auto& record : handler.shots)
        {
            out << "{\"kind\":\"shot\",\"demo\":\"" << handler.demo
                << "\",\"sender\":" << static_cast<unsigned int>(record.sender)
                << ",\"tick\":" << record.tick
                << ",\"shooter\":" << record.shot.shooterId
                << ",\"shooterName\":\"" << nameOf(record.shot.shooterId, record.tick)
                << "\",\"slot\":" << static_cast<unsigned int>(record.shot.weaponSlot)
                << ",\"target\":" << record.shot.targetId
                << ",\"targetName\":\"" << nameOf(record.shot.targetId, record.tick) << "\"";
            position(out, "o", record.shot.origin);
            position(out, "t", record.shot.target);
            out << "}\n";
        }

        for (const auto& record : handler.damageRecords)
        {
            out << "{\"kind\":\"damage\",\"demo\":\"" << handler.demo
                << "\",\"sender\":" << static_cast<unsigned int>(record.sender)
                << ",\"tick\":" << record.tick
                << ",\"victim\":" << record.damage.victimId
                << ",\"victimName\":\"" << nameOf(record.damage.victimId, record.tick)
                << "\",\"attacker\":" << record.damage.attackerId
                << ",\"attackerName\":\"" << nameOf(record.damage.attackerId, record.tick)
                << "\",\"damage\":" << record.damage.damage
                // The trailing u16 rides along unnamed. It is not remaining
                // health and it is not identified (tad_events.h says what it
                // is not), and this dump is the first thing to carry it beside
                // a named shooter and a named weapon slot, which is what would
                // narrow it.
                << ",\"unknown\":" << record.damage.unknown << "}\n";
        }

        for (const auto& record : handler.deathRecords)
        {
            out << "{\"kind\":\"death\",\"demo\":\"" << handler.demo
                << "\",\"sender\":" << static_cast<unsigned int>(record.sender)
                << ",\"tick\":" << record.tick
                << ",\"unit\":" << record.death.unitId
                << ",\"unitName\":\"" << nameOf(record.death.unitId, record.tick)
                << "\",\"killer\":" << record.death.killerId
                << ",\"killerName\":\"" << nameOf(record.death.killerId, record.tick)
                << "\",\"severity\":" << static_cast<unsigned int>(record.death.severity)
                << ",\"cause\":" << static_cast<unsigned int>(record.death.cause())
                << ",\"corpseLevel\":" << static_cast<unsigned int>(record.death.corpseLevel()) << "}\n";
        }
    }

    void reportWeaponSlots(
        const EpisodeHandler& handler,
        const std::vector<std::string>& loadOrder,
        const std::map<std::string, UnitFacts>& unitFacts,
        WeaponSlotTally& total)
    {
        auto lives = unitLives(handler, loadOrder);
        // The name AND the build that supplied it, because how long before the
        // shot that build finished is what separates a recycled id from a
        // genuine reading: a shot 30,000 ticks after "the wind generator was
        // built" is a later unit wearing the wind generator's id.
        auto nameAt = [&](uint16_t id, uint32_t at) -> std::optional<std::pair<uint32_t, std::string>> {
            auto it = lives.find(id);
            if (it == lives.end())
            {
                return std::nullopt;
            }
            return buildAtTick(it->second, at);
        };

        /** What one unit type was seen doing, and how stale its name was. */
        struct TypeObservation
        {
            std::set<unsigned int> slots;

            /** Ticks between the build that named the shooter and the shot. */
            std::vector<uint32_t> ages;
        };

        std::map<std::string, TypeObservation> slotsPerType;
        std::map<std::string, std::set<unsigned int>> slotsPerTypeUnscoped;
        std::map<uint16_t, std::string> firstName;
        for (const auto& [id, list] : lives)
        {
            firstName.emplace(id, list.front().second);
        }
        unsigned long named = 0;
        unsigned long aimedAtUnit = 0;
        std::map<unsigned int, unsigned long> slotCounts;
        for (const auto& record : handler.shots)
        {
            ++slotCounts[record.shot.weaponSlot];
            if (record.shot.targetId != 0)
            {
                ++aimedAtUnit;
            }
            if (auto name = nameAt(record.shot.shooterId, record.tick))
            {
                ++named;
                auto& observation = slotsPerType[name->second];
                observation.slots.insert(record.shot.weaponSlot);
                observation.ages.push_back(record.tick - name->first);
            }
            if (auto first = firstName.find(record.shot.shooterId); first != firstName.end())
            {
                slotsPerTypeUnscoped[first->second].insert(record.shot.weaponSlot);
            }
        }

        auto countViolations = [&](const std::map<std::string, std::set<unsigned int>>& seen) {
            unsigned int bad = 0;
            for (const auto& [type, slots] : seen)
            {
                auto facts = unitFacts.find(type);
                if (facts == unitFacts.end())
                {
                    continue;
                }
                for (auto slot : slots)
                {
                    if (slot > 2 || (facts->second.weaponSlotMask & (1u << slot)) == 0)
                    {
                        ++bad;
                        break;
                    }
                }
            }
            return bad;
        };
        auto unscopedViolating = countViolations(slotsPerTypeUnscoped);

        auto median = [](std::vector<uint32_t> values) -> uint32_t {
            if (values.empty())
            {
                return 0;
            }
            std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
            return values[values.size() / 2];
        };

        unsigned int consistent = 0;
        unsigned int violating = 0;
        unsigned int naiveViolating = 0;
        unsigned long violatingShots = 0;
        unsigned long conformingShots = 0;
        std::vector<uint32_t> violatingAges;
        std::vector<uint32_t> conformingAges;
        std::ostringstream violations;
        for (const auto& [type, observation] : slotsPerType)
        {
            auto facts = unitFacts.find(type);
            if (facts == unitFacts.end())
            {
                continue;
            }

            auto ok = true;
            auto naiveOk = true;
            auto weaponCount = static_cast<unsigned int>(
                std::popcount(facts->second.weaponSlotMask));
            for (auto slot : observation.slots)
            {
                if (slot > 2 || (facts->second.weaponSlotMask & (1u << slot)) == 0)
                {
                    ok = false;
                }
                if (slot >= weaponCount)
                {
                    naiveOk = false;
                }
            }
            naiveViolating += naiveOk ? 0 : 1;

            if (ok)
            {
                ++consistent;
                conformingShots += observation.ages.size();
                conformingAges.insert(
                    conformingAges.end(), observation.ages.begin(), observation.ages.end());
                continue;
            }

            ++violating;
            violatingShots += observation.ages.size();
            violatingAges.insert(
                violatingAges.end(), observation.ages.begin(), observation.ages.end());

            violations << "    " << type << " fills slots";
            for (unsigned int slot = 0; slot < 3; ++slot)
            {
                if ((facts->second.weaponSlotMask & (1u << slot)) != 0)
                {
                    violations << " " << slot;
                }
            }
            if (facts->second.weaponSlotMask == 0)
            {
                violations << " none";
            }
            violations << ", fired";
            for (auto slot : observation.slots)
            {
                violations << " " << slot;
            }
            violations << " on " << observation.ages.size() << " shot(s), median "
                       << median(observation.ages) << " ticks after the build that named it\n";
        }

        std::cout << "  " << handler.shots.size() << " shots, " << named
                  << " with a named shooter, " << aimedAtUnit << " aimed at a unit; slots";
        for (const auto& [slot, count] : slotCounts)
        {
            std::cout << " " << slot << "x" << count;
        }
        std::cout << "; " << consistent << " type(s) consistent, " << violating << " not\n"
                  << violations.str();

        total.consistent += consistent;
        total.violating += violating;
        total.naiveViolating += naiveViolating;
        total.unscopedViolating += unscopedViolating;
        total.violatingShots += violatingShots;
        total.conformingShots += conformingShots;
        total.violatingAges.insert(
            total.violatingAges.end(), violatingAges.begin(), violatingAges.end());
        total.conformingAges.insert(
            total.conformingAges.end(), conformingAges.begin(), conformingAges.end());
        total.shots += handler.shots.size();
        total.named += named;
        total.aimedAtUnit += aimedAtUnit;
        for (const auto& [slot, count] : slotCounts)
        {
            total.slotCounts[slot] += count;
        }
    }
    // --- the weapon-flight cells ---------------------------------------------
    //
    // A port of tools/tad-weapontime.py, which is the reference and which has
    // the whole argument in its docstring. The short version: nothing links a
    // 0x0d to the 0x0b it caused, so a flight time is a filtered statistic.
    // Keyed on (attacker, victim), a shot survives only where it is the only
    // shot from that shooter at that victim within +-window ticks and exactly
    // one damage record from that shooter to that victim lands in the window
    // after it. The survivors are scored against
    //
    //     flight = ceil(distance / (weaponvelocity / 30)) - 1
    //
    // where the -1 is a projectile taking its first step on the tick it is
    // fired -- the same off-by-one as the build accumulator's first increment
    // landing on the 0x09's own tick.
    //
    // THIS AND THE SCRIPT MUST KEEP AGREEING, cell for cell, and the script is
    // the reference. --weapon-cells prints the same table the script prints.

    /** One (shooter type, weapon slot) cell of the corpus. */
    struct WeaponCell
    {
        std::string shooter;
        unsigned int slot;
        std::string weapon;

        /** The weapon's own TDF values, for transcribing into the fixture. */
        unsigned int velocity;
        unsigned int damage;

        unsigned int pairings;
        unsigned int pairingsAtMode;

        /** Modal `flight - (ceil(d/v) - 1)`, which the model says is zero. */
        int modeDelta;

        /** The modal damage value, which should be the weapon's own. */
        unsigned int modalDamage;

        /** One representative pairing that landed on the mode. */
        std::string demo;
        uint32_t shotTick;
        uint32_t damageTick;
        TadPosition origin;
        TadPosition target;
    };

    /** One surviving (shot, damage) pairing. */
    struct Pairing
    {
        std::string demo;
        std::string shooter;
        unsigned int slot;
        uint32_t shotTick;
        uint32_t damageTick;
        double distance;
        unsigned int damage;
        TadPosition origin;
        TadPosition target;
    };

    double tadDistance(const TadPosition& a, const TadPosition& b)
    {
        auto dx = tadFixedToDouble(a.x) - tadFixedToDouble(b.x);
        auto dy = tadFixedToDouble(a.y) - tadFixedToDouble(b.y);
        auto dz = tadFixedToDouble(a.z) - tadFixedToDouble(b.z);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    /** The filters, per demo. */
    std::vector<Pairing> pairShots(
        const EpisodeHandler& handler,
        const std::vector<std::string>& loadOrder,
        unsigned int window)
    {
        auto lives = unitLives(handler, loadOrder);
        auto nameOf = [&](uint16_t id, uint32_t at) -> std::string {
            auto it = lives.find(id);
            if (it == lives.end())
            {
                return "";
            }
            auto build = buildAtTick(it->second, at);
            return build ? build->second : std::string();
        };

        struct Fired
        {
            uint32_t tick;
            std::string shooter;
            unsigned int slot;
            double distance;
            TadPosition origin;
            TadPosition target;
        };

        std::map<std::pair<uint16_t, uint16_t>, std::vector<Fired>> fired;
        for (const auto& record : handler.shots)
        {
            if (record.shot.targetId == 0)
            {
                continue;
            }
            auto shooter = nameOf(record.shot.shooterId, record.tick);
            if (shooter.empty())
            {
                continue;
            }
            fired[std::make_pair(record.shot.shooterId, record.shot.targetId)].push_back(
                Fired{record.tick, shooter, record.shot.weaponSlot, tadDistance(record.shot.origin, record.shot.target), record.shot.origin, record.shot.target});
        }

        std::map<std::pair<uint16_t, uint16_t>, std::vector<std::pair<uint32_t, unsigned int>>> landed;
        for (const auto& record : handler.damageRecords)
        {
            landed[std::make_pair(record.damage.attackerId, record.damage.victimId)]
                .emplace_back(record.tick, record.damage.damage);
        }

        std::vector<Pairing> out;
        for (auto& [key, shots] : fired)
        {
            std::stable_sort(shots.begin(), shots.end(), [](const Fired& a, const Fired& b) {
                return a.tick < b.tick;
            });

            std::vector<std::pair<uint32_t, unsigned int>> hits;
            if (auto it = landed.find(key); it != landed.end())
            {
                hits = it->second;
                std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
                    return a.first < b.first;
                });
            }

            for (std::size_t i = 0; i < shots.size(); ++i)
            {
                const auto& shot = shots[i];
                if (i > 0 && shot.tick - shots[i - 1].tick < window)
                {
                    continue;
                }
                if (i + 1 < shots.size() && shots[i + 1].tick - shot.tick < window)
                {
                    continue;
                }

                const std::pair<uint32_t, unsigned int>* only = nullptr;
                unsigned int inside = 0;
                for (const auto& hit : hits)
                {
                    if (hit.first >= shot.tick && hit.first <= shot.tick + window)
                    {
                        ++inside;
                        only = &hit;
                    }
                }
                if (inside != 1)
                {
                    continue;
                }

                out.push_back(Pairing{
                    handler.demo, shot.shooter, shot.slot, shot.tick, only->first, shot.distance, only->second, shot.origin, shot.target});
            }
        }
        return out;
    }

    /** What the model predicts, and what excludes a weapon from being scored. */
    int tadFlightModel(double distance, unsigned int velocity)
    {
        auto perTick = static_cast<double>(velocity) / 30.0;
        return static_cast<int>(std::ceil(distance / perTick)) - 1;
    }

    /**
     * Which model describes this weapon, or why none of them does. The strings
     * match tools/tad-weapontime.py's weapon_class so the two tables can be
     * compared row for row.
     */
    std::string weaponClass(const WeaponFacts& facts)
    {
        if (facts.vLaunch)
        {
            return "vlaunch";
        }
        if (facts.waterWeapon)
        {
            return "waterweapon";
        }
        if (facts.ballistic)
        {
            return "ballistic";
        }
        if (facts.burst > 1)
        {
            return "burst";
        }
        if (facts.velocity == 0)
        {
            return "no velocity";
        }
        // The TDF's startvelocity is zero where the weapon leaves the rail at
        // full speed, which is the constant-speed case and not a stationary one.
        if (facts.startVelocity != 0 && facts.startVelocity != facts.velocity)
        {
            return "accelerating";
        }
        return "constant speed";
    }

    std::vector<WeaponCell> mineWeaponCells(
        const std::vector<Pairing>& pairings,
        const std::map<std::string, UnitFacts>& unitFacts,
        const std::map<std::string, WeaponFacts>& weaponFacts,
        unsigned int minPairings)
    {
        std::map<std::pair<std::string, unsigned int>, std::vector<const Pairing*>> grouped;
        for (const auto& p : pairings)
        {
            grouped[std::make_pair(p.shooter, p.slot)].push_back(&p);
        }

        std::vector<WeaponCell> out;
        for (const auto& [key, group] : grouped)
        {
            if (group.size() < minPairings)
            {
                continue;
            }

            auto unit = unitFacts.find(key.first);
            if (unit == unitFacts.end() || key.second > 2)
            {
                continue;
            }
            auto weaponName = unit->second.weaponNames[key.second];
            for (auto& c : weaponName)
            {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            auto weapon = weaponFacts.find(weaponName);
            if (weaponName.empty() || weapon == weaponFacts.end() || weapon->second.velocity == 0)
            {
                continue;
            }

            std::map<int, unsigned int> deltas;
            std::map<unsigned int, unsigned int> damages;
            int mode = 0;
            unsigned int atMode = 0;
            for (const auto* p : group)
            {
                auto delta = static_cast<int>(p->damageTick - p->shotTick)
                    - tadFlightModel(p->distance, weapon->second.velocity);
                auto count = ++deltas[delta];
                if (count > atMode)
                {
                    mode = delta;
                    atMode = count;
                }
                ++damages[p->damage];
            }

            unsigned int modalDamage = 0;
            unsigned int atModalDamage = 0;
            for (const auto& [value, count] : damages)
            {
                if (count > atModalDamage)
                {
                    modalDamage = value;
                    atModalDamage = count;
                }
            }

            // The representative is the earliest pairing that landed on the
            // mode, so the provenance is stable under regeneration.
            const Pairing* representative = nullptr;
            for (const auto* p : group)
            {
                auto delta = static_cast<int>(p->damageTick - p->shotTick)
                    - tadFlightModel(p->distance, weapon->second.velocity);
                if (delta != mode)
                {
                    continue;
                }
                if (representative == nullptr
                    || std::tie(p->demo, p->shotTick) < std::tie(representative->demo, representative->shotTick))
                {
                    representative = p;
                }
            }

            out.push_back(WeaponCell{
                key.first, key.second, weaponName, weapon->second.velocity, weapon->second.defaultDamage, static_cast<unsigned int>(group.size()), atMode, mode, modalDamage, representative->demo, representative->shotTick, representative->damageTick, representative->origin, representative->target});
        }

        std::sort(out.begin(), out.end(), [](const WeaponCell& a, const WeaponCell& b) {
            return std::tie(a.shooter, a.slot) < std::tie(b.shooter, b.slot);
        });
        return out;
    }


    /**
     * Writes the weapon-flight episodes.
     *
     * One episode per scored cell, and only the cells the model predicts. The
     * two that do not -- ARMAMPH firing GAUSS_MAV and CORGEO firing RIOT_ALL,
     * both one tick low and both near-ties with the bucket the model names --
     * are SKIPPED rather than checked in with their offset written into
     * expectedFlightDelta, because that field is for a divergence somebody
     * decided on and not for an observation nobody has explained. It is the same
     * rule that keeps airborne builders out of the build fixture.
     *
     * A THIRD HEADER, beside tad_economy_episodes.h and tad_build_episodes.h.
     * They share no struct, are mined by different passes over different
     * corpora, and regenerate independently, so one regeneration never churns
     * another's diff.
     */
    void writeWeaponEpisodes(
        std::ostream& out,
        const std::vector<WeaponCell>& cells,
        const std::map<std::string, WeaponFacts>& weaponFacts)
    {
        std::vector<const WeaponCell*> scored;
        unsigned int unexplained = 0;
        for (const auto& cell : cells)
        {
            auto weapon = weaponFacts.find(cell.weapon);
            if (weapon == weaponFacts.end() || weaponClass(weapon->second) != "constant speed")
            {
                continue;
            }
            if (cell.modeDelta != 0)
            {
                std::cout << "  skipping " << cell.shooter << " slot " << cell.slot
                          << " (" << cell.weapon << "): corpus is " << std::showpos << cell.modeDelta
                          << std::noshowpos << " off the model over " << cell.pairings << " pairings\n";
                ++unexplained;
                continue;
            }
            scored.push_back(&cell);
        }

        std::cout << scored.size() << " weapon episode(s)";
        if (unexplained != 0)
        {
            std::cout << ", " << unexplained << " cell(s) skipped as unexplained";
        }
        std::cout << "\n";

        out << R"(#pragma once

#include <cstdint>

// GENERATED FILE -- do not edit by hand. Regenerate with tad_episodes
// --emit-weapon-cpp; the command, and the corpus it needs, are in
// docs/TA-DEMOS.md.
//
// Episodes mined from real Total Annihilation games, for the conformance tests
// in weaponflight.test.cpp. Its siblings are tad_economy_episodes.h and
// tad_build_episodes.h; the three share no struct and are mined by different
// passes, so they are kept apart and regenerate independently.
//
// WHY THIS IS A HEADER OF STRUCTS AND NOT A DATA FILE. rwe_test is hermetic --
// it reads no files, mounts no VFS and opens no archive -- and it stays that
// way. Demos and mod files never enter the repository either. So the numbers
// travel as source: each weapon's own TDF values are transcribed inline beside
// the observation they explain, and a test can be read without either.
//
// WHAT AN EPISODE IS. One shot out of a real game, with the damage it caused.
// NOTHING IN A DEMO LINKS THE TWO -- no shot id, no sequence number, and no tick
// on a damage record beyond the packet serial carrying it, against 631,578 shots
// and 824,844 damage events -- so the pairing is a filter, not a lookup: a shot
// is kept only where it is the only shot from that shooter at that victim within
// 300 ticks either side and exactly one damage record from that shooter to that
// victim lands in the 300 after it. That keeps 35,535 shots. The representative
// below is the earliest surviving pairing of its cell that landed on the cell's
// modal flight time, and `pairings` and `pairingsAtMode` say how much company it
// had.
//
// The pairing is confirmed by a number no filter looks at: every cell's modal
// damage is its weapon's own [DAMAGE] default, which is `weaponDamage` here.
//
// WHICH SHOTS MAY BE HERE. Only weapons that fly at a constant speed, which is
// to say the ones this arithmetic describes. An accelerating missile leaves the
// rail at `startvelocity` and works up, a ballistic round travels an arc longer
// than the straight line, a vlaunch rocket goes up before it goes anywhere, a
// torpedo travels through water, and a burst weapon fires several rounds from
// one trigger so the isolation filter cannot mean what it means elsewhere. Those
// five are five more oracles, not discrepancies. See docs/TA-DEMOS.md.
//
// THE ARITHMETIC BEING PINNED. A projectile covers weaponVelocity / 30 world
// units a tick -- the same conversion LoadingScene_util.cpp does -- and takes
// its FIRST step on the tick it is fired, so it has covered the distance after
// ceil(d / v) steps and the gap between the shot and the damage is one less.
// RWE steps projectiles after the behaviour pass that spawns them, so it takes
// that first step on the firing tick too, and expectedFlightDelta is zero
// everywhere below. The field is kept because the fixture's whole purpose is to
// survive a deliberate divergence; a non-zero value here would have to name the
// docs/TOTALA-EXE.md section that licensed it, exactly as the build fixture's
// does.

namespace rwe
{
    struct TadWeaponEpisode
    {
        const char* demo;
        const char* shooterName;
        unsigned int weaponSlot;
        const char* weaponName;

        /** The tick the shot was fired on, and the tick its damage arrived. */
        uint32_t shotTick;
        uint32_t damageTick;

        /** How many pairings the cell held, and how many shared this flight time. */
        unsigned int pairings;
        unsigned int pairingsAtMode;

        /** TDF weaponvelocity, in world units a SECOND. Divide by 30 for a tick. */
        unsigned int weaponVelocity;

        /** The weapon's [DAMAGE] default, which the cell's modal damage matches. */
        unsigned int weaponDamage;

        /**
         * Where the shot came from and where it was aimed, as TA puts them on
         * the wire: 16.16 fixed point in world units, y up. Kept as the raw
         * integers because 16.16 carries up to 32 significant bits and a float
         * has 24, so converting here would lose the low end of a large
         * coordinate and could move a ceil across a boundary.
         */
        int32_t originX, originY, originZ;
        int32_t targetX, targetY, targetZ;

        /** damageTick - shotTick, which is what the test has to reproduce. */
        unsigned int flightTicks;

        /** Ticks RWE is expected to differ by, and what licenses it. */
        int expectedFlightDelta;
        const char* expectedDifference;
    };

    // clang-format off
    inline constexpr TadWeaponEpisode tadWeaponEpisodes[] = {
)";

        for (const auto* cell : scored)
        {
            out << "        {\"" << cell->demo << "\", \"" << cell->shooter << "\", "
                << cell->slot << ", \"" << cell->weapon << "\",\n"
                << "            " << cell->shotTick << ", " << cell->damageTick << ", "
                << cell->pairings << ", " << cell->pairingsAtMode << ",\n"
                << "            " << cell->velocity << ", " << cell->damage << ",\n"
                << "            " << cell->origin.x << ", " << cell->origin.y << ", " << cell->origin.z << ",\n"
                << "            " << cell->target.x << ", " << cell->target.y << ", " << cell->target.z << ",\n"
                << "            " << (cell->damageTick - cell->shotTick) << ", 0, nullptr},\n";
        }

        out << "    };\n"
            << "    // clang-format on\n"
            << "}\n";
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
                  << "  --emit-shots  write every 0x0d, 0x0b and 0x0c as JSON Lines (needs --units)\n"
                  << "  --emit-cpp    write the storage episodes as a C++ header (needs --units)\n"
                  << "  --emit-build-cpp  write the build-timing episodes as a C++ header (needs --units)\n"
                  << "  --cells       print the (builder, product) build-timing cells\n"
                  << "  --weapon-cells  print the (shooter, weapon slot) flight-time cells\n"
                  << "  --emit-weapon-cpp  write the weapon-flight episodes as a C++ header\n"
                  << "  --window      isolation window for the weapon pass, ticks (default 300)\n"
                  << "  --min-pairings  pairings a weapon cell needs (default 30)\n"
                  << "  --weapon-slots  check the 0x0d trailing byte against each shooter's FBI\n"
                  << "  --max-types   distinct unit types an episode may carry (default 6)\n"
                  << "  --max-per-player  episodes to keep per player (default 3)\n"
                  << "  --max-cells   build-timing episodes to check in (default 25)\n"
                  << "  --min-builds  builds a cell needs before its mode is used (default 5)\n"
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
    std::map<std::string, UnitFacts> unitFacts;
    std::map<std::string, WeaponFacts> weaponFacts;
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
        unitFacts = readUnitFacts(files);
        std::cout << "unit load order: " << loadOrder.size() << " types from " << dir << "\n";

        // The weapon TDFs sit beside the FBIs in the same tree but are a
        // different format in a different directory, so they are a second read.
        // Their absence is not an error: only the weapon pass needs them.
        std::error_code weaponError;
        weaponFacts = readWeaponFacts(dir, weaponError);
        if (!weaponFacts.empty())
        {
            std::cout << "weapon definitions: " << weaponFacts.size() << " blocks\n";
        }
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
    std::size_t maxCells = args.contains("max-cells") ? std::stoul(args.getString("max-cells")) : 25;

    // How many builds a (builder, product) cell needs before its mode is worth
    // consuming. The same default, and the same meaning, as
    // tools/tad-buildtime.py's.
    unsigned int minBuilds = args.contains("min-builds") ? std::stoul(args.getString("min-builds")) : 5;

    // The isolation window and the cell floor for the weapon pass. Both are
    // tools/tad-weapontime.py's defaults and mean the same things, because the
    // two have to keep agreeing cell for cell.
    unsigned int window = args.contains("window") ? std::stoul(args.getString("window")) : 300;
    unsigned int minPairings = args.contains("min-pairings") ? std::stoul(args.getString("min-pairings")) : 30;

    if (args.contains("emit-cpp") && loadOrder.empty())
    {
        std::cerr << "--emit-cpp needs --units: an episode carries its unit types' own FBI values\n";
        return 1;
    }

    if (args.contains("emit-build-cpp") && loadOrder.empty())
    {
        std::cerr << "--emit-build-cpp needs --units: a cell is keyed on the builder's and the"
                  << " product's own types, and carries their FBI values\n";
        return 1;
    }

    // Opened before the walk and written per demo, because the corpus is about
    // 1.5 million records and holding them all to serialise at the end would be
    // a needless peak. --emit-shots does not need --units, but without it every
    // name in the dump is empty, which makes the pairing work impossible: say so
    // rather than writing a file that silently cannot be used.
    std::ofstream shotFile;
    unsigned long shotLines = 0;
    if (args.contains("emit-shots"))
    {
        if (loadOrder.empty())
        {
            std::cerr << "--emit-shots needs --units: without it no shooter, victim or"
                      << " target in the dump has a name\n";
            return 1;
        }
        auto out = args.getString("emit-shots");
        shotFile.open(out);
        if (!shotFile)
        {
            std::cerr << "cannot write " << out << "\n";
            return 1;
        }
    }

    if ((args.contains("weapon-cells") || args.contains("emit-weapon-cpp")) && loadOrder.empty())
    {
        std::cerr << "--weapon-cells and --emit-weapon-cpp need --units: a cell is keyed on the"
                  << " shooter's type and carries its weapon's own TDF values\n";
        return 1;
    }

    nlohmann::json resourceJson = nlohmann::json::array();
    std::vector<StorageEpisode> storageEpisodes;

    std::vector<rwe::Episode> all;

    /** Demos whose own type count disagrees with --units; see mineBuildCells. */
    std::set<std::string> wrongDataSet;

    WeaponSlotTally weaponSlots;

    // A weapon cell pools across demos for the same reason a build cell does,
    // so the pairings accumulate here and are grouped once the walk is over.
    std::vector<Pairing> pairings;
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
            wrongDataSet.insert(path.filename().string());
        }

        if (args.contains("weapon-slots"))
        {
            reportWeaponSlots(handler, loadOrder, unitFacts, weaponSlots);
        }


        if (args.contains("weapon-cells") || args.contains("emit-weapon-cpp"))
        {
            auto mined = pairShots(handler, loadOrder, window);
            std::cout << "  " << mined.size() << " shot(s) paired\n";
            pairings.insert(pairings.end(), mined.begin(), mined.end());
        }

        if (shotFile.is_open())
        {
            writeShotJson(shotFile, handler, loadOrder);
            shotLines += handler.shots.size() + handler.damageRecords.size()
                + handler.deathRecords.size();
            std::cout << "  " << handler.shots.size() << " shots, "
                      << handler.damageRecords.size() << " damage, "
                      << handler.deathRecords.size() << " deaths\n";
        }

        if (args.contains("emit-cpp"))
        {
            auto mined = mineStorageEpisodes(handler, loadOrder, unitFacts, maxTypes, maxPerPlayer);
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

    if (args.contains("weapon-slots"))
    {
        std::cout << "\n"
                  << weaponSlots.shots << " shots, " << weaponSlots.named
                  << " with a named shooter, " << weaponSlots.aimedAtUnit << " aimed at a unit\n"
                  << "trailing byte:";
        for (const auto& [slot, count] : weaponSlots.slotCounts)
        {
            std::cout << " " << slot << "x" << count;
        }
        std::cout << "\n"
                  << weaponSlots.consistent << " of "
                  << (weaponSlots.consistent + weaponSlots.violating)
                  << " (demo, type) observations fired only slots their FBI fills;"
                  << " the naive `slot < weapon count` test fails "
                  << weaponSlots.naiveViolating << " of them, and naming a shooter"
                  << " by its id's FIRST build rather than its most recent fails "
                  << weaponSlots.unscopedViolating << "\n"
                  << "the first gap is the Weapon1-and-Weapon3 convention, which is what says"
                  << " the byte is a slot;\nthe second is what unit-id recycling costs\n";

        auto median = [](std::vector<uint32_t> values) -> uint32_t {
            if (values.empty())
            {
                return 0;
            }
            std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
            return values[values.size() / 2];
        };

        std::cout << "the " << weaponSlots.violating << " that fail account for "
                  << weaponSlots.violatingShots << " shot(s) against "
                  << weaponSlots.conformingShots << " conforming;\n"
                  << "median ticks between the naming build and the shot: "
                  << median(weaponSlots.violatingAges) << " for a failing shot, "
                  << median(weaponSlots.conformingAges) << " for a conforming one\n";
    }

    // The build-timing cells, corpus-wide. Printed on --cells so the port can be
    // diffed against tools/tad-buildtime.py over the same episodes, which is the
    // check that says this is the script's arithmetic and not a second opinion.
    std::vector<BuildCell> buildCells;
    if (!loadOrder.empty() && (args.contains("cells") || args.contains("emit-build-cpp")))
    {
        if (!wrongDataSet.empty())
        {
            std::cout << "\nexcluding " << wrongDataSet.size()
                      << " demo(s) from the build-timing cells: recorded on another data set\n";
        }
        buildCells = mineBuildCells(all, loadOrder, unitFacts, wrongDataSet, minBuilds);
    }

    if (args.contains("cells"))
    {
        std::vector<const BuildCell*> scored;
        for (const auto& cell : buildCells)
        {
            if (cell.kind == "immobile")
            {
                scored.push_back(&cell);
            }
        }

        std::map<std::string, unsigned int> byClass;
        for (const auto& cell : buildCells)
        {
            ++byClass[cell.kind];
        }

        std::cout << "\n"
                  << buildCells.size() << " pairs with " << minBuilds << "+ builds:";
        for (const auto& [kind, count] : byClass)
        {
            std::cout << " " << count << " " << kind;
        }

        // Only the immobile class may become an episode. A ground mobile
        // builder pays its own COB deploy before INBUILDSTANCE, which is mod
        // data; an airborne one finishes a tick early for a reason nobody has
        // read out of the binary yet (TOTALA-EXE.md section 91), and writing
        // that into expectedDifference would launder an open question into a
        // licensed divergence. docs/TA-DEMOS.md.
        std::cout << "\nscoring the " << scored.size() << " whose builder is immobile\n\n"
                  << "  builder    product      BuildTime   p   BT/p  extra   mode     n  share  delta\n";

        std::sort(scored.begin(), scored.end(), [](const BuildCell* a, const BuildCell* b) {
            return std::tie(b->builds, a->builder, a->product) < std::tie(a->builds, b->builder, b->product);
        });

        unsigned int misses = 0;
        for (const auto* cell : scored)
        {
            auto floorTicks = cell->buildTime / cell->p;
            auto extra = static_cast<int>(cell->floatModel + 1) - static_cast<int>(floorTicks);
            auto delta = static_cast<int>(cell->integerModel) - static_cast<int>(cell->mode);
            auto agrees = cell->mode == cell->floatModel;
            misses += agrees ? 0 : 1;
            std::cout << "  " << std::left << std::setw(10) << cell->builder << " "
                      << std::setw(12) << cell->product << std::right
                      << std::setw(10) << cell->buildTime
                      << std::setw(4) << cell->p
                      << std::setw(7) << floorTicks
                      << std::setw(6) << std::showpos << extra << std::noshowpos
                      << std::setw(7) << cell->mode
                      << std::setw(6) << cell->builds
                      << std::setw(6) << (100 * cell->buildsAtMode / cell->builds) << "%"
                      << std::setw(6) << std::showpos << delta << std::noshowpos
                      << (agrees ? "" : "   <-- disagrees with the float32 model") << "\n";
        }
        std::cout << "\n"
                  << misses << " disagreement(s) with the float32 model\n";
    }

    if (args.contains("emit-cpp"))
    {
        auto out = args.getString("emit-cpp");
        std::ostringstream generated;
        writeEpisodes(generated, std::move(storageEpisodes));
        if (!writeGeneratedHeader(out, generated.str()))
        {
            std::cerr << "cannot write " << out << "\n";
            return 1;
        }
    }

    if (args.contains("emit-build-cpp"))
    {
        auto out = args.getString("emit-build-cpp");
        std::ostringstream generated;
        writeBuildEpisodes(generated, std::move(buildCells), maxCells);
        if (!writeGeneratedHeader(out, generated.str()))
        {
            std::cerr << "cannot write " << out << "\n";
            return 1;
        }
    }

    std::vector<WeaponCell> weaponCells;
    if (!pairings.empty())
    {
        weaponCells = mineWeaponCells(pairings, unitFacts, weaponFacts, minPairings);
    }

    if (args.contains("weapon-cells"))
    {
        // The same table tools/tad-weapontime.py prints, in the same order, so
        // the two can be diffed cell for cell. The script is the reference.
        std::map<std::string, std::vector<const WeaponCell*>> byClass;
        for (const auto& cell : weaponCells)
        {
            auto weapon = weaponFacts.find(cell.weapon);
            byClass[weapon == weaponFacts.end() ? "no weapon block" : weaponClass(weapon->second)]
                .push_back(&cell);
        }

        const auto& scored = byClass["constant speed"];
        std::cout << "\n"
                  << pairings.size() << " shots paired, " << weaponCells.size()
                  << " cells with " << minPairings << "+ pairings\n"
                  << "scoring the " << scored.size() << " whose weapon flies at a constant speed\n\n";
        std::cout << "  " << std::left << std::setw(13) << "shooter" << " " << std::right << std::setw(2) << "sl"
                  << " " << std::left << std::setw(22) << "weapon" << std::right
                  << std::setw(7) << "v/30" << std::setw(6) << "n" << std::setw(7) << "delta"
                  << std::setw(7) << "share" << std::setw(8) << "damage" << std::setw(7) << "decl" << "\n";

        unsigned int agreeing = 0;
        unsigned int damageAgreeing = 0;
        std::vector<const WeaponCell*> sorted(scored.begin(), scored.end());
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
            return a->pairings > b->pairings;
        });
        for (const auto* cell : sorted)
        {
            agreeing += cell->modeDelta == 0 ? 1 : 0;
            damageAgreeing += cell->modalDamage == cell->damage ? 1 : 0;
            std::cout << "  " << std::left << std::setw(13) << cell->shooter << " "
                      << std::right << std::setw(2) << cell->slot << " "
                      << std::left << std::setw(22) << cell->weapon << std::right
                      << std::setw(7) << std::fixed << std::setprecision(1) << (cell->velocity / 30.0)
                      << std::setw(6) << cell->pairings
                      << std::setw(6) << std::showpos << cell->modeDelta << std::noshowpos
                      << std::setw(6) << static_cast<int>(std::lround(100.0 * cell->pairingsAtMode / cell->pairings)) << "%"
                      << std::setw(8) << cell->modalDamage << std::setw(7) << cell->damage
                      << (cell->modeDelta == 0 ? "" : "   <-- disagrees") << "\n";
        }

        std::cout << "\n"
                  << damageAgreeing << " of " << sorted.size()
                  << " scored cells carry their weapon's own [DAMAGE] default as the modal damage\n";
        std::cout << "\nthe classes this model does not describe, listed and never scored:\n";
        for (const auto& [kind, group] : byClass)
        {
            if (kind == "constant speed")
            {
                continue;
            }
            unsigned int pooled = 0;
            for (const auto* cell : group)
            {
                pooled += cell->pairings;
            }
            std::cout << "  " << kind << ": " << group.size() << " cells, " << pooled << " pairings\n";
        }
        std::cout << "\n"
                  << (sorted.size() - agreeing) << " disagreement(s) with the model\n";
    }


    if (args.contains("emit-weapon-cpp"))
    {
        auto out = args.getString("emit-weapon-cpp");
        std::ostringstream generated;
        writeWeaponEpisodes(generated, weaponCells, weaponFacts);
        if (!writeGeneratedHeader(out, generated.str()))
        {
            std::cerr << "cannot write " << out << "\n";
            return 1;
        }
    }
    if (shotFile.is_open())
    {
        shotFile.close();
        std::cout << "wrote " << args.getString("emit-shots") << ", " << shotLines
                  << " JSON Lines record(s)\n";
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
