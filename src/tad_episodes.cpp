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
//   --stall-episodes  print the stall pass, which is what
//                 tools/tad-stalltime.py scores
//   --emit-stall-cpp  write the stall episodes as a C++ header
//   --cells       print the (builder, product) build-timing cells, which is
//                 what tools/tad-buildtime.py scores
//   --unit-state  decode every 0x2c and hold it to the rest of the stream: the
//                 0x09 that placed a unit, its FBI MaxVelocity, the previous
//                 full-state record, and where a 0x0d put it. Needs --units
//   --emit-unit-state  write the decoded full-state records as JSON Lines;
//                 --with-updates adds the per-tick mover updates
//   --weapon-slots  check a 0x0d's trailing byte against the slots the
//                 shooter's own FBI fills; the evidence that it is a weapon index
//   --max-types   distinct unit types an episode may carry (default 6)
//   --max-per-player  episodes to keep per player (default 3)
//   --max-cells   build-timing episodes to check in (default 28)
//   --min-builds  builds a cell needs before its mode is used (default 5)
//   --all         emit rejected episodes too, each with its reasons

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
#include <tuple>
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
         * FootprintX and FootprintZ, in map squares of sixteen world units. What
         * a projectile stops on: it detonates the first tick it stands in a
         * square the victim occupies (0x49B090), and a unit occupies these.
         */
        unsigned int footprintX;
        unsigned int footprintZ;

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

        /**
         * BuildCostMetal and BuildCostEnergy. Only the stall episodes read them:
         * a factory asks for a product's cost a tick at a time, and the size of
         * the ask is what a stalled settle turns into debt.
         */
        unsigned int buildCostMetal;
        unsigned int buildCostEnergy;
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

        /**
         * Zero in the TDF means two different things, and which one depends on
         * whether the weapon has a motor: with no acceleration it is "off the
         * rail at full speed", and with acceleration it is "from a standstill".
         * tadLaunchSpeed asks it the way createProjectileFromWeapon does.
         */
        unsigned int startVelocity;
        unsigned int acceleration;
        unsigned int range;

        /**
         * How long the motor runs. `range / weaponvelocity` ticks normally, and
         * `weapontimer` seconds where the weapon says `noautorange`; running out
         * is not death, the missile coasts on at the speed it reached.
         */
        float weaponTimer;
        bool noAutoRange;

        /** Rounds per trigger pull. Anything above 1 is why a shot cannot be isolated. */
        unsigned int burst;

        bool ballistic;
        bool vLaunch;
        bool waterWeapon;
        bool lineOfSight;

        /**
         * Whether the engine flies this one on the motor path at all, and the
         * two flags it reads only there. TA tests `selfprop` before anything
         * else (0x49B9C2), so a weapon that also carries `lineofsight=1` --
         * which most missiles do -- is flown by the motor; and `cruise` or
         * `twophase` replace the flight with a shape no model here measures.
         */
        bool selfProp;
        bool cruise;
        bool twoPhase;

        /**
         * Detonate on running out of motor instead of coasting on, which is a
         * different arrival and not a different speed. Nothing scored here is
         * both burnblow and longer-lived than its motor, but a regeneration that
         * produced one would fly a round the fixture had not described, so it
         * travels with them.
         */
        bool burnBlow;

        /**
         * How wide the original's own aim jitter is, in sixteen-bit angle units.
         *
         * 0x49D6D7 adds `rand(accuracy) - accuracy/2` to BOTH the heading and
         * the pitch of every turret shot, and `sprayangle` is a second draw on
         * the heading alone. Only the ballistic class reads them: for a round
         * that flies level a pitch error is under a percent of its speed, and
         * for a shell it is a range error several ticks wide. `turret` says
         * whether the handler that draws at all is the one this weapon gets
         * (0x49E010).
         */
        unsigned int accuracy;
        unsigned int sprayAngle;
        bool turret;

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
                        weapon.weaponTimer,
                        weapon.noAutoRange,
                        weapon.burst,
                        weapon.ballistic,
                        weapon.vLaunch,
                        weapon.waterWeapon,
                        weapon.lineOfSight,
                        weapon.selfProp,
                        weapon.cruise,
                        weapon.twoPhase,
                        weapon.burnBlow,
                        weapon.accuracy,
                        weapon.sprayAngle,
                        weapon.turret,
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
                    fbi.footprintX,
                    fbi.footprintZ,
                    static_cast<unsigned int>(
                        (fbi.weapon1.empty() ? 0u : 1u) | (fbi.weapon2.empty() ? 0u : 2u)
                        | (fbi.weapon3.empty() ? 0u : 4u)),
                    std::array<std::string, 3>{fbi.weapon1, fbi.weapon2, fbi.weapon3},
                    fbi.buildCostMetal,
                    fbi.buildCostEnergy};
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

        // How many unit types the demo's own 0x1a table declares, so a consumer
        // can drop a demo recorded on another data set the way the build cells
        // do (wrongDataSet) rather than naming its units out of the wrong load
        // order. tools/tad-stalltime.py reads it.
        if (handler.unitTable)
        {
            j["unitTypes"] = handler.unitTable->restricted.size();
        }

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
    // p / BuildTime passes 1.0f. A construction aircraft gets a SECOND
    // increment on that first tick, because its mission runs the lathe twice
    // before the tick ends (docs/TOTALA-EXE.md section 107), so its duration is
    // two less than its increment count where everything else is one less.
    // tools/tad-buildtime.py is the same arithmetic
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

            // The increments that do not show up as duration: one for the tick
            // the 0x09 is sent, and a second for the construction aircraft's
            // repeat of its own lathe state on that tick (section 107). The
            // reference script subtracts the same number in the same place, and
            // it has to be subtracted BEFORE the outlier cap, or an airborne
            // cell would keep a different set of builds than the script does.
            auto kind = builderClass(builderFacts);
            auto increment = kind == "airborne" ? 2u : 1u;

            auto floatModel = tadTicksToBuild(productFacts.buildTime, p) - increment;

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
                rweTicksToBuild(productFacts.buildTime, p) - increment,
                kind,
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
            // Immobile and airborne builders both have a model. A ground mobile
            // one does not: it pays its own COB deploy before INBUILDSTANCE,
            // which is the mod's data rather than the engine's behaviour.
            if (cell.kind != "immobile" && cell.kind != "airborne")
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
            // Ahead of the cap: every cell whose BuildTime divides exactly by
            // the rate, because those are the ones carrying a delta, and every
            // airborne cell, because there are only three of them and they are
            // the fixture's whole evidence for the second lathe -- including
            // the one that carries no delta, which is what says the class is
            // not uniformly a tick out.
            auto rank = [](const BuildCell& c) { return c.buildTime % c.p == 0 || c.kind == "airborne" ? 0 : 1; };
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
        unsigned int airborne = 0;
        for (const auto& cell : scored)
        {
            divergent += cell.integerModel == cell.mode ? 0 : 1;
            airborne += cell.kind == "airborne" ? 1 : 0;
        }
        std::cout << scored.size() << " build episode(s), " << airborne
                  << " of them airborne, " << divergent
                  << " where RWE is expected to differ";
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
// WHICH BUILDS MAY BE HERE. Builds by an IMMOBILE builder, which is to say a
// factory: there is nothing for it to walk to and nothing for it to deploy, so
// the duration is the nanolathe and nothing else. And builds by an AIRBORNE one,
// which has nothing to deploy either -- the original never makes a construction
// aircraft wait for its stance -- and whose extra tick is no longer a mystery:
// it lathes twice on the tick it creates the nanoframe (docs/TOTALA-EXE.md
// section 107), which RWE now does too. A ground mobile builder pays its own COB
// deploy sequence before INBUILDSTANCE, which is the mod's data rather than the
// engine's behaviour, and is never an episode. See docs/TA-DEMOS.md.
//
// THE ARITHMETIC BEING PINNED. A builder contributes p = WorkerTime / 30 build
// units a tick -- integer division, in the engine and in the demo tooling alike.
// The first increment lands on the tick the nanoframe appears, so an episode's
// duration is one less than the number of increments -- and two less for a
// construction aircraft, because that first tick pays two of them. The original
// finishes when a single-precision fraction counted up by p / BuildTime passes
// 1.0f; RWE adds p to an unsigned counter and finishes at BuildTime. Those two
// agree except where BuildTime divides exactly by p, and expectedDurationDelta
// is where they do not. The two classes carry the same convention, so a -1 here
// always means that difference and never the aircraft's second lathe.

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

        /**
         * Whether the builder is a construction aircraft, from its own FBI's
         * Canfly. It decides how many increments the first tick pays -- two
         * rather than one -- so a test cannot count them without it.
         */
        bool builderFlies;

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
                << (cell.kind == "airborne" ? "true" : "false") << ", "
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

    // --- --stall-episodes, --emit-stall-cpp: what a stalled settle costs -----
    //
    // The stall half of the economy oracle. TA settles each player's resources
    // once a second and carries whatever a consumer was granted and not paid for
    // as DEBT on that consumer; between settles a unit in debt is refused
    // outright (docs/TOTALA-EXE.md section 23, and section 108 for the decode
    // this pass rests on). The corpus samples a player's stores one settle in
    // four, so a settle's fractions cannot be read off it -- but the debt rule
    // leaves a shape on factory build timings that can be, with the 0x28 stream
    // as the independent witness that a stall happened.
    //
    // tools/tad-stalltime.py is the reference and this is its port. The two
    // print the same three sections and the same episode list, and must agree
    // line for line; if they part company it is the port that is wrong.

    /** What the stall pass keeps of one demo once the walk has moved on. */
    struct StallDemo
    {
        std::string demo;
        std::vector<Episode> episodes;
        std::vector<EpisodeHandler::ResourceRecord> records;
        std::map<uint8_t, unsigned int> senderBlock;
    };

    constexpr long long stallSettleTicks = 30;

    /**
     * How far a 0x28's tick may sit from the settle it reports: a sender stamps
     * it with the last 0x2c serial it sent, which runs a few ticks behind.
     */
    constexpr long long stallSampleLag = 6;

    /** The share of one sender's samples that must sit that close to a settle. */
    constexpr double stallPhaseShare = 0.95;

    /**
     * The scored builds that do not land on their residue, and how many ticks
     * SHORT of it each falls. The same four, for the same reason, as
     * KNOWN_EXCEPTIONS in tools/tad-stalltime.py: all short and none long, and a
     * debt only lengthens a job where an assist only shortens one.
     */
    struct StallKnownException
    {
        const char* demo;
        uint16_t builderId;
        uint32_t startTick;
        long long shortfall;
    };

    constexpr StallKnownException stallKnownExceptions[] = {
        {"14725.ted", 2008, 56516, 19},
        {"14727.ted", 8056, 25783, 2},
        {"14728.ted", 6064, 26139, 18},
        {"14730.ted", 7653, 81332, 4},
    };

    long long nearestSettle(long long tick)
    {
        return ((tick + stallSettleTicks / 2) / stallSettleTicks) * stallSettleTicks;
    }

    bool isWatcherRecord(const TadResourceStats& stats)
    {
        return stats.metalStorage == 0.0f && stats.energyStorage == 0.0f;
    }

    /** One paired build by an immobile builder, named, with its lateness. */
    struct FactoryBuild
    {
        const Episode* episode;
        std::string builder;
        std::string product;
        unsigned int model;
        unsigned int rweModel;
        long long late;
        bool spoilt;
        bool scoredCell;
    };

    struct StallEpisode
    {
        std::string demo;
        unsigned int ownerBlock;
        uint16_t builderId;
        std::string builder;
        unsigned int workerTime;

        std::string previousProduct;
        unsigned int previousBuildTime;
        unsigned int previousCostMetal;
        unsigned int previousCostEnergy;
        uint32_t previousStart;
        uint32_t previousFinish;

        long long settle;
        bool metalEmpty;
        uint32_t sampleTick;

        std::string product;
        unsigned int buildTime;
        unsigned int costMetal;
        unsigned int costEnergy;
        uint32_t start;
        uint32_t finish;
        unsigned int model;
        unsigned int rweModel;
        long long late;
        long long residue;
        bool hit;
        long long furtherStalls;
    };

    struct StallReport
    {
        std::vector<std::tuple<std::string, unsigned int, std::size_t, std::size_t>> senders;
        unsigned int agreeingCells = 0;
        std::map<std::string, unsigned int> shape;
        std::map<std::pair<std::string, bool>, unsigned int> shapeWithEmptySample;
        std::map<std::string, unsigned int> tally;
        std::vector<StallEpisode> scored;
    };

    StallReport mineStalls(
        const std::vector<StallDemo>& demos,
        const std::vector<std::string>& loadOrder,
        const std::map<std::string, UnitFacts>& unitFacts,
        const std::set<std::string>& wrongDataSet,
        unsigned int minBuilds)
    {
        StallReport report;

        // 1. The phase, per sender, watchers dropped.
        for (const auto& d : demos)
        {
            if (wrongDataSet.count(d.demo) != 0)
            {
                continue;
            }
            std::map<uint8_t, std::pair<std::size_t, std::size_t>> perSender;
            for (const auto& r : d.records)
            {
                if (isWatcherRecord(r.stats))
                {
                    continue;
                }
                auto& [n, near] = perSender[r.sender];
                ++n;
                auto t = static_cast<long long>(r.tick);
                near += std::llabs(t - nearestSettle(t)) <= stallSampleLag ? 1 : 0;
            }
            for (const auto& [sender, counts] : perSender)
            {
                report.senders.emplace_back(d.demo, sender, counts.first, counts.second);
            }
        }

        // The build cells, over the clean episodes as tools/tad-buildtime.py sees
        // them, and only the ones whose mode the float32 model explains.
        std::vector<Episode> clean;
        std::vector<const Episode*> everything;
        for (const auto& d : demos)
        {
            if (wrongDataSet.count(d.demo) != 0)
            {
                continue;
            }
            for (const auto& e : d.episodes)
            {
                everything.push_back(&e);
                if (e.clean())
                {
                    clean.push_back(e);
                }
            }
        }
        std::set<std::pair<std::string, std::string>> agreeing;
        for (const auto& cell : mineBuildCells(clean, loadOrder, unitFacts, wrongDataSet, minBuilds))
        {
            if (cell.kind == "immobile" && cell.mode == cell.floatModel)
            {
                agreeing.emplace(cell.builder, cell.product);
            }
        }
        report.agreeingCells = static_cast<unsigned int>(agreeing.size());

        // Builders named the way the build cells name them: the most recent build
        // of that id to finish by the time this one started.
        std::map<std::pair<std::string, uint16_t>, std::vector<std::pair<uint32_t, std::string>>> lives;
        for (const auto* e : everything)
        {
            if (auto name = tadUnitNameForTypeIndex(loadOrder, e->typeIndex))
            {
                lives[std::make_pair(e->demo, e->unitId)].emplace_back(e->finishTick, *name);
            }
        }
        for (auto& [id, list] : lives)
        {
            std::sort(list.begin(), list.end());
        }

        static const std::set<std::string> spoiling{
            "frame took damage", "builder took damage", "speed change", "builder in another owner block", "no elapsed ticks"};

        std::vector<FactoryBuild> builds;
        for (const auto* e : everything)
        {
            auto life = lives.find(std::make_pair(e->demo, e->builderId));
            if (life == lives.end())
            {
                continue;
            }
            auto builder = buildAtTick(life->second, e->startTick);
            auto product = tadUnitNameForTypeIndex(loadOrder, e->typeIndex);
            if (!builder || !product)
            {
                continue;
            }
            auto builderFacts = unitFacts.find(builder->second);
            auto productFacts = unitFacts.find(*product);
            if (builderFacts == unitFacts.end() || productFacts == unitFacts.end())
            {
                continue;
            }
            if (builderClass(builderFacts->second) != "immobile")
            {
                continue;
            }
            auto p = builderFacts->second.workerTime / 30;
            if (p == 0 || productFacts->second.buildTime == 0)
            {
                continue;
            }
            auto model = tadTicksToBuild(productFacts->second.buildTime, p) - 1;
            auto spoilt = std::any_of(e->rejections.begin(), e->rejections.end(), [](const std::string& r) { return spoiling.count(r) != 0; });
            builds.push_back(FactoryBuild{
                e,
                builder->second,
                *product,
                model,
                rweTicksToBuild(productFacts->second.buildTime, p) - 1,
                static_cast<long long>(e->durationTicks()) - static_cast<long long>(model),
                spoilt,
                agreeing.count(std::make_pair(builder->second, *product)) != 0});
        }

        std::map<std::pair<std::string, unsigned int>, std::vector<std::tuple<uint32_t, float, float>>> samples;
        for (const auto& d : demos)
        {
            if (wrongDataSet.count(d.demo) != 0)
            {
                continue;
            }
            for (const auto& r : d.records)
            {
                if (isWatcherRecord(r.stats))
                {
                    continue;
                }
                auto block = d.senderBlock.find(r.sender);
                if (block == d.senderBlock.end())
                {
                    continue;
                }
                samples[std::make_pair(d.demo, block->second)].emplace_back(r.tick, r.stats.metalStored, r.stats.energyStored);
            }
        }
        for (auto& [key, rows] : samples)
        {
            std::sort(rows.begin(), rows.end());
        }
        static const std::vector<std::tuple<uint32_t, float, float>> noSamples;
        auto samplesOf = [&](const std::string& demo, unsigned int block) -> const std::vector<std::tuple<uint32_t, float, float>>& {
            auto it = samples.find(std::make_pair(demo, block));
            return it == samples.end() ? noSamples : it->second;
        };
        auto empty = [](const std::tuple<uint32_t, float, float>& s) {
            return std::get<1>(s) == 0.0f || std::get<2>(s) == 0.0f;
        };

        // 2. The quantum.
        for (const auto& b : builds)
        {
            if (b.spoilt || !b.scoredCell)
            {
                continue;
            }
            std::string kind = b.late < 0 ? "early"
                : b.late == 0             ? "on the model"
                : b.late % stallSettleTicks == 0 ? "late by 30k"
                                                 : "late otherwise";
            ++report.shape[kind];
            auto start = static_cast<long long>(b.episode->startTick);
            auto finish = static_cast<long long>(b.episode->finishTick);
            bool stalled = false;
            for (const auto& s : samplesOf(b.episode->demo, b.episode->ownerBlock))
            {
                auto settle = nearestSettle(std::get<0>(s));
                if (start <= settle - stallSettleTicks && settle + stallSettleTicks <= finish && empty(s))
                {
                    stalled = true;
                    break;
                }
            }
            ++report.shapeWithEmptySample[std::make_pair(kind, stalled)];
        }

        // 3. The episodes.
        std::map<std::pair<std::string, uint16_t>, std::vector<const FactoryBuild*>> byBuilder;
        for (const auto& b : builds)
        {
            byBuilder[std::make_pair(b.episode->demo, b.episode->builderId)].push_back(&b);
        }

        for (auto& [key, runs] : byBuilder)
        {
            std::stable_sort(runs.begin(), runs.end(), [](const FactoryBuild* a, const FactoryBuild* b) {
                return std::make_pair(a->episode->startTick, a->episode->finishTick)
                    < std::make_pair(b->episode->startTick, b->episode->finishTick);
            });

            for (std::size_t i = 1; i < runs.size(); ++i)
            {
                const auto& prev = *runs[i - 1];
                const auto& cur = *runs[i];
                const auto* next = i + 1 < runs.size() ? runs[i + 1] : nullptr;

                auto start = static_cast<long long>(cur.episode->startTick);
                if (start % stallSettleTicks == 0)
                {
                    continue;
                }
                auto settle = start - start % stallSettleTicks;
                auto prevFinish = static_cast<long long>(prev.episode->finishTick);
                if (!(settle - stallSettleTicks <= prevFinish && prevFinish <= settle - 1))
                {
                    continue;
                }
                if (prev.builder != cur.builder)
                {
                    continue;
                }

                std::vector<const std::tuple<uint32_t, float, float>*> near;
                const std::tuple<uint32_t, float, float>* firstEmpty = nullptr;
                for (const auto& s : samplesOf(cur.episode->demo, cur.episode->ownerBlock))
                {
                    auto t = static_cast<long long>(std::get<0>(s));
                    if (t >= settle - stallSampleLag && t <= settle + stallSampleLag)
                    {
                        near.push_back(&s);
                        if (firstEmpty == nullptr && empty(s))
                        {
                            firstEmpty = &s;
                        }
                    }
                }
                if (near.empty())
                {
                    continue;
                }

                auto residue = stallSettleTicks - start % stallSettleTicks;
                auto lands = cur.late >= residue && (cur.late - residue) % stallSettleTicks == 0;
                auto neighboursUnassisted = prev.late >= 0 && (next == nullptr || next->late >= 0);

                // The control: the same pattern over a settle the sample says did
                // not stall. If the residue came from the selection rather than
                // from the debt, it would show up here too.
                if (firstEmpty == nullptr)
                {
                    if (!cur.spoilt && cur.scoredCell && neighboursUnassisted && cur.late >= 0)
                    {
                        ++report.tally[std::string("control: settle not stalled, residue ") + (lands ? "predicted" : "not predicted")];
                    }
                    continue;
                }

                ++report.tally["candidates"];
                if (cur.spoilt)
                {
                    ++report.tally["rejected: damage or speed change"];
                    continue;
                }
                if (!cur.scoredCell)
                {
                    ++report.tally["rejected: cell not explained by the build model"];
                    continue;
                }
                if (!neighboursUnassisted)
                {
                    ++report.tally["rejected: a neighbouring build was assisted"];
                    continue;
                }
                if (cur.late < 0)
                {
                    ++report.tally["rejected: assisted"];
                    continue;
                }

                const auto& builderFacts = unitFacts.at(cur.builder);
                const auto& productFacts = unitFacts.at(cur.product);
                const auto& previousFacts = unitFacts.at(prev.product);
                report.scored.push_back(StallEpisode{
                    cur.episode->demo,
                    cur.episode->ownerBlock,
                    cur.episode->builderId,
                    cur.builder,
                    builderFacts.workerTime,
                    prev.product,
                    previousFacts.buildTime,
                    previousFacts.buildCostMetal,
                    previousFacts.buildCostEnergy,
                    prev.episode->startTick,
                    prev.episode->finishTick,
                    settle,
                    std::get<1>(*firstEmpty) == 0.0f,
                    std::get<0>(*firstEmpty),
                    cur.product,
                    productFacts.buildTime,
                    productFacts.buildCostMetal,
                    productFacts.buildCostEnergy,
                    cur.episode->startTick,
                    cur.episode->finishTick,
                    cur.model,
                    cur.rweModel,
                    cur.late,
                    residue,
                    lands,
                    lands ? (cur.late - residue) / stallSettleTicks : -1});
            }
        }

        return report;
    }

    const StallKnownException* stallKnownException(const StallEpisode& e)
    {
        for (const auto& known : stallKnownExceptions)
        {
            if (e.demo == known.demo && e.builderId == known.builderId && e.start == known.startTick)
            {
                return &known;
            }
        }
        return nullptr;
    }

    /** Prints the report in the reference script's layout, so the two diff. Returns the failure count. */
    unsigned int printStalls(const StallReport& report, bool list)
    {
        unsigned int failures = 0;

        std::size_t total = 0;
        std::size_t near = 0;
        const std::tuple<std::string, unsigned int, std::size_t, std::size_t>* worst = nullptr;
        for (const auto& s : report.senders)
        {
            total += std::get<2>(s);
            near += std::get<3>(s);
            if (worst == nullptr
                || static_cast<double>(std::get<3>(s)) / static_cast<double>(std::get<2>(s))
                    < static_cast<double>(std::get<3>(*worst)) / static_cast<double>(std::get<2>(*worst)))
            {
                worst = &s;
            }
        }
        std::cout << "\n1. settle phase: " << near << " of " << total << " resource samples, from "
                  << report.senders.size() << " senders,\n"
                  << "   sit within " << stallSampleLag << " ticks of a multiple of " << stallSettleTicks << " of the demo clock\n";
        if (worst != nullptr)
        {
            std::cout << "   least aligned sender: " << std::get<0>(*worst) << " sender " << std::get<1>(*worst)
                      << ", " << std::get<3>(*worst) << " of " << std::get<2>(*worst) << "\n";
        }
        for (const auto& [demo, sender, n, k] : report.senders)
        {
            if (static_cast<double>(k) / static_cast<double>(n) < stallPhaseShare)
            {
                std::cout << "MISS phase: " << demo << " sender " << sender << ": only " << k << " of " << n
                          << " samples near a settle\n";
                ++failures;
            }
        }

        auto shape = [&](const std::string& kind) {
            auto it = report.shape.find(kind);
            return it == report.shape.end() ? 0u : it->second;
        };
        auto withEmpty = [&](const std::string& kind) {
            auto it = report.shapeWithEmptySample.find(std::make_pair(kind, true));
            return it == report.shapeWithEmptySample.end() ? 0u : it->second;
        };
        std::cout << "\n2. factory builds on the " << report.agreeingCells << " cells the build model explains:\n";
        for (const auto* kind : {"on the model", "late by 30k", "late otherwise", "early"})
        {
            std::cout << "   " << std::left << std::setw(15) << kind << std::right << " " << std::setw(6) << shape(kind)
                      << "   with an empty store sampled inside: " << std::setw(5) << withEmpty(kind) << "\n";
        }
        auto late = shape("late by 30k") + shape("late otherwise");
        if (late != 0)
        {
            std::cout << "   " << shape("late by 30k") << " of " << late
                      << " late builds are late by whole seconds, where chance gives 1 in " << stallSettleTicks << "\n";
        }

        std::cout << "\n3. a factory refused into its next job by a stall the 0x28 stream saw:\n";
        for (const auto& [key, count] : report.tally)
        {
            std::cout << "   " << std::left << std::setw(50) << key << std::right << " " << std::setw(5) << count << "\n";
        }
        std::set<long long> residues;
        std::set<std::pair<std::string, unsigned int>> players;
        std::set<std::string> demosHit;
        std::size_t hits = 0;
        for (const auto& e : report.scored)
        {
            if (e.hit)
            {
                ++hits;
                residues.insert(e.residue);
                players.emplace(e.demo, e.ownerBlock);
                demosHit.insert(e.demo);
            }
        }
        std::cout << "   scored " << report.scored.size() << ", residue predicted exactly in " << hits << "\n"
                  << "   " << residues.size() << " distinct residues, " << players.size() << " players, "
                  << demosHit.size() << " demos\n";

        if (list)
        {
            for (const auto& e : report.scored)
            {
                std::cout << "   " << e.demo << " block " << e.ownerBlock << " " << e.builder << " " << e.previousProduct
                          << "->" << e.product << " finish " << e.previousFinish << " settle " << e.settle << " ("
                          << (e.metalEmpty ? "metal" : "energy") << " empty at " << e.sampleTick << ") start " << e.start
                          << " late " << e.late << " residue " << e.residue << (e.hit ? "" : "   <-- MISS") << "\n";
            }
        }

        std::size_t seen = 0;
        for (const auto& e : report.scored)
        {
            auto shortfall = ((e.residue - e.late) % stallSettleTicks + stallSettleTicks) % stallSettleTicks;
            if (const auto* known = stallKnownException(e))
            {
                ++seen;
                if (!e.hit && shortfall == known->shortfall)
                {
                    std::cout << "KNOWN " << e.demo << " " << e.builder << " -> " << e.product << " at " << e.start << ": "
                              << shortfall << " ticks short of its residue, which only an assist can make\n";
                    continue;
                }
                std::cout << "MOVED " << e.demo << " " << e.builder << " -> " << e.product << " at " << e.start
                          << ": was " << known->shortfall << " short, now late " << e.late << " against residue "
                          << e.residue << "\n";
                ++failures;
                continue;
            }
            if (!e.hit)
            {
                std::cout << "MISS " << e.demo << " " << e.builder << " " << e.builderId << " -> " << e.product
                          << ": start " << e.start << ", late " << e.late << ", predicted " << e.residue << " + 30k ("
                          << shortfall << " short)\n";
                ++failures;
            }
        }
        if (seen != std::size(stallKnownExceptions))
        {
            std::cout << "MOVED: " << (std::size(stallKnownExceptions) - seen)
                      << " build(s) named as known exceptions are no longer scored\n";
            ++failures;
        }

        if (report.scored.empty())
        {
            std::cout << "nothing to score\n";
            return failures + 1;
        }
        if (failures != 0)
        {
            std::cout << "\n" << failures << " disagreement(s)\n";
            return failures;
        }
        std::cout << "\nevery sender settles on the common phase; " << hits << " of " << report.scored.size()
                  << " scored builds land on their residue and the " << (report.scored.size() - hits)
                  << " known exception(s) still read what they read\n";
        return failures;
    }

    /**
     * Picks the stall episodes worth checking in, and writes them.
     *
     * Variety, not volume: what an episode asserts beyond "a stall costs whole
     * seconds" is the residue, 30 - start % 30, so the rule is one episode per
     * distinct residue. Of the builds that share one, the one with the fewest
     * further stalled seconds wins -- it is the shortest run and the least of
     * it rests on settles no sample saw -- then the earliest by provenance.
     */
    void writeStallEpisodes(std::ostream& out, const StallReport& report)
    {
        std::map<long long, const StallEpisode*> byResidue;
        for (const auto& e : report.scored)
        {
            if (!e.hit)
            {
                continue;
            }
            auto& slot = byResidue[e.residue];
            if (slot == nullptr
                || std::make_tuple(e.furtherStalls, e.demo, e.ownerBlock, e.start)
                    < std::make_tuple(slot->furtherStalls, slot->demo, slot->ownerBlock, slot->start))
            {
                slot = &e;
            }
        }

        unsigned int divergent = 0;
        for (const auto& [residue, e] : byResidue)
        {
            divergent += e->rweModel == e->model ? 0 : 1;
        }
        std::cout << byResidue.size() << " stall episode(s), " << divergent << " of them where RWE is expected to differ\n";

        out << R"(#pragma once

// GENERATED FILE -- do not edit by hand. Regenerate with tad_episodes
// --emit-stall-cpp; the command, and the corpus it needs, are in
// docs/TA-DEMOS.md.
//
// Episodes mined from real Total Annihilation games, for the stall cases in
// economy.test.cpp. Its siblings hold the storage, build-timing and weapon
// episodes; they share no struct and regenerate independently.
//
// WHY THIS IS A HEADER OF STRUCTS AND NOT A DATA FILE. rwe_test is hermetic --
// it reads no files, mounts no VFS and opens no archive -- and it stays that
// way. Demos and mod files never enter the repository either. So the numbers
// travel as source: each unit's own FBI values are transcribed inline beside the
// observation they explain, and a test can be read without either.
//
// WHAT AN EPISODE IS. A factory finishes one product in a second whose settle a
// 0x28 sample shows stalled -- a store read exactly empty -- and starts its next
// product before the following settle. It was granted resources in the stalled
// second, so the settle left it in debt, so the new job is refused from its
// first tick until a settle pays the debt off. TA settles every player on the
// same ticks, multiples of 30 of the demo clock (docs/TOTALA-EXE.md section
// 108), so the job starts late by exactly 30 - startTick % 30, plus a whole 30
// for every further settle the player stayed stalled.
//
// WHAT IS PREDICTED AND WHAT IS READ. residueTicks comes from the start tick and
// the settle cadence alone; nothing about it is read from the build it
// predicts, and over settles the sample says did NOT stall the same selection
// predicts no build at all (tools/tad-stalltime.py prints the control).
// furtherStalledSettles IS read from the observation, because the corpus sees
// one settle in four and the ones between samples are not observed; a test
// replays that many and asserts the rest.
//
// WHICH BUILDS MAY BE HERE. Only an immobile builder, on a (builder, product)
// cell whose modal duration the float32 build model already explains, with
// neither the build nor the builder's jobs either side of it early against the
// model: an early build means something assisted it, and an assist moves a
// duration by an amount nothing in the stream records. One episode per residue.

namespace rwe
{
    struct TadStallEpisode
    {
        /** Provenance: the demo, the owner block, and the factory's unit id in it. */
        const char* demo;
        unsigned int ownerBlock;
        unsigned int builderId;

        /** The factory, with its own WorkerTime from the FBI. */
        const char* builderName;
        unsigned int workerTime;

        /**
         * The job the factory finished in the stalled second, with its FBI
         * figures. All a test needs of it is that the factory was granted a
         * tick's worth of it before the settle; previousFinishTick is that tick.
         */
        const char* previousProductName;
        unsigned int previousBuildTime;
        unsigned int previousBuildCostMetal;
        unsigned int previousBuildCostEnergy;
        unsigned int previousStartTick;
        unsigned int previousFinishTick;

        /**
         * The settle that stalled, and the 0x28 that saw it: the sample's tick
         * and which store read empty. The sample lags the settle by the few
         * ticks a sender's clock runs behind.
         */
        unsigned int stalledSettleTick;
        bool metalEmpty;
        unsigned int sampleTick;

        /** The job that was refused into, with its FBI figures. */
        const char* productName;
        unsigned int buildTime;
        unsigned int buildCostMetal;
        unsigned int buildCostEnergy;
        unsigned int startTick;
        unsigned int finishTick;

        /** The float32 build model's duration for this job, unimpeded. */
        unsigned int modelDurationTicks;

        /** Observed: (finishTick - startTick) - modelDurationTicks. */
        unsigned int lateTicks;

        /** Predicted: 30 - startTick % 30. lateTicks is this plus 30 * furtherStalledSettles. */
        unsigned int residueTicks;

        /** Read from the observation: settles after the sampled one that also stalled. */
        unsigned int furtherStalledSettles;

        /**
         * What RWE is expected to differ by, and why. Computed, as the build
         * episodes' is: RWE's integer accumulator against TA's float32 fraction,
         * which part company only where BuildTime divides exactly by the rate.
         * A settle is not in it -- RWE settles every player on the same ticks,
         * and so, it turns out, does TA.
         */
        int expectedDurationDelta;
        const char* expectedDifference;
    };

    // clang-format off
    inline constexpr TadStallEpisode tadStallEpisodes[] = {
)";

        for (const auto& [residue, e] : byResidue)
        {
            auto delta = static_cast<int>(e->rweModel) - static_cast<int>(e->model);
            out << "        {\"" << e->demo << "\", " << e->ownerBlock << ", " << e->builderId << ",\n"
                << "            \"" << e->builder << "\", " << e->workerTime << ",\n"
                << "            \"" << e->previousProduct << "\", " << e->previousBuildTime << ", "
                << e->previousCostMetal << ", " << e->previousCostEnergy << ", " << e->previousStart << ", "
                << e->previousFinish << ",\n"
                << "            " << e->settle << ", " << (e->metalEmpty ? "true" : "false") << ", " << e->sampleTick << ",\n"
                << "            \"" << e->product << "\", " << e->buildTime << ", " << e->costMetal << ", "
                << e->costEnergy << ", " << e->start << ", " << e->finish << ",\n"
                << "            " << e->model << ", " << e->late << ", " << e->residue << ", " << e->furtherStalls << ",\n"
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

        // EXACT, not three decimals. A 16.16 coordinate is exactly representable
        // in a double, and the shortest round-trip form hands the script the
        // same number the port reads. Three decimals was enough while the models
        // measured a distance; the footprint model floors positions onto
        // sixteen-unit squares, and a rounded coordinate on the wrong side of a
        // square boundary moved whole pairings -- CORVAMP's share read 52% in the
        // script and 66% in the port until this changed.
        auto exact = [](std::ostream& o, int32_t fixed) {
            std::array<char, 32> buffer;
            auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), tadFixedToDouble(fixed));
            o.write(buffer.data(), end - buffer.data());
        };
        auto position = [&exact](std::ostream& o, const char* prefix, const TadPosition& p) {
            o << ",\"" << prefix << "x\":";
            exact(o, p.x);
            o << ",\"" << prefix << "y\":";
            exact(o, p.y);
            o << ",\"" << prefix << "z\":";
            exact(o, p.z);
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
    // after it. The survivors are scored against the first step on which the
    // round, flown along its aim line, stands in a map square of the victim's
    // footprint -- which is where TA detonates it (0x49B090), and not at the
    // aim point. The step number is the flight time. The aim-point model this
    // replaced, `ceil(distance / (weaponvelocity / 30)) - 1`, had a -1 read as a
    // first step on the firing tick; it was the footprint.
    //
    // THIS AND THE SCRIPT MUST KEEP AGREEING, cell for cell, and the script is
    // the reference. --weapon-cells prints the same table the script prints.

    /** One (shooter type, weapon slot) cell of the corpus. */
    struct WeaponCell
    {
        std::string shooter;
        unsigned int slot;
        std::string weapon;

        /** Which of the two models was scored against, from weaponClass. */
        std::string weaponClass;

        /** The weapon's own TDF values, for transcribing into the fixture. */
        bool selfProp;
        bool burnBlow;
        unsigned int velocity;
        unsigned int startVelocity;
        unsigned int acceleration;
        unsigned int range;
        float weaponTimer;
        bool noAutoRange;
        unsigned int damage;

        /**
         * Pairings the cell was scored over, and how many the class's victim
         * bound threw away getting there. Zero dropped for a constant-speed
         * cell, which is scored over every victim.
         */
        unsigned int pairings;
        unsigned int pairingsAtMode;
        unsigned int pairingsDropped;

        /** Modal `flight - model`, which the model says is zero. */
        int modeDelta;

        /** The modal damage value, which should be the weapon's own. */
        unsigned int modalDamage;

        /** One representative pairing that landed on the mode. */
        std::string demo;
        uint32_t shotTick;
        uint32_t damageTick;
        TadPosition origin;
        TadPosition target;
        std::string victim;
        unsigned int victimFootprintX;
        unsigned int victimFootprintZ;
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

        /**
         * What was shot at, empty where the id's build was never seen. The
         * drift bound reads its FBI `maxvelocity`, and an unnamed victim cannot
         * be shown to have held still, so the bound rejects it rather than
         * treating it as one that did.
         */
        std::string victim;
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
            std::string victim;
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
                Fired{record.tick, shooter, record.shot.weaponSlot, tadDistance(record.shot.origin, record.shot.target), record.shot.origin, record.shot.target, nameOf(record.shot.targetId, record.tick)});
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
                    handler.demo, shot.shooter, shot.slot, shot.tick, only->first, shot.distance, only->second, shot.origin, shot.target, shot.victim});
            }
        }
        return out;
    }

    /**
     * What a self-propelled round leaves the barrel at, in world units a tick.
     *
     * The same question createProjectileFromWeapon asks, in the same order: a
     * `startvelocity` of zero is full speed for a weapon with no motor and a
     * standstill for one with a motor.
     */
    double tadLaunchSpeed(const WeaponFacts& facts)
    {
        if (facts.startVelocity != 0)
        {
            return static_cast<double>(facts.startVelocity) / 30.0;
        }
        return facts.acceleration == 0 ? static_cast<double>(facts.velocity) / 30.0 : 0.0;
    }

    /**
     * How many ticks the motor runs for, which is what motorOutFrame counts to.
     *
     * IT CHANGES PAIRINGS BUT NO CELL'S MODE. A missile fired far enough does
     * run its motor out and coast -- a MISSILE_GF_HEAVY's stops at tick 15 --
     * but not often enough to move a mode: adding the term left every mode
     * where it was and moved no share by more than three points. It is in the
     * model on the strength of being the engine's arithmetic, not of what it
     * does to this corpus.
     */
    unsigned int tadBurnTicks(const WeaponFacts& facts)
    {
        if (facts.velocity != 0 && !facts.noAutoRange)
        {
            return static_cast<unsigned int>(
                static_cast<double>(facts.range) / (static_cast<double>(facts.velocity) / 30.0));
        }
        return static_cast<unsigned int>(facts.weaponTimer * 30.0f);
    }

    /**
     * The per-tick gravity every ballistic round in the game falls under: the
     * map's own `gravity`, 112 in nearly everything shipped, over 30 squared.
     * 0x49BD10 takes it off velocity.y each tick and the ballistic branch of
     * RWE's updateProjectiles is the same line. TOTALA-EXE.md section 7 pins
     * the scale: the smoke emitter reads the same word and lifts a puff by
     * four times it.
     */
    constexpr double tadGravity = 112.0 / (30.0 * 30.0);

    /** One unit of TA's sixteen-bit angle, in radians. */
    constexpr double tadAngleUnit = 2.0 * 3.14159265358979323846 / 65536.0;

    /**
     * The angle a ballistic gun elevates to, or nothing where it cannot reach.
     *
     * THE FLAT ROOT, ALWAYS. 0x49A890 solves the standard ballistic quadratic --
     * the same discriminant RWE's computeFiringAngles forms, arrived at by a
     * different factoring -- and picks between the roots at 0x49AA11 against
     * `minbarrelangle` as a floor and pi/4 as a ceiling. The high root exceeds
     * 45 degrees for every target inside the gun's range and equals it only at
     * the range itself, so the ceiling rejects it every time and the original
     * always fires the flat one. That is RWE's `pitches->second`.
     *
     * No solution means the weapon does not fire at all (`cmp ax,0x8000` at
     * 0x49D61B), so a geometry with none is not a shot this model accounts for.
     */
    std::optional<double> tadBallisticPitch(
        const WeaponFacts& facts, const double origin[3], const double target[3])
    {
        auto speed = static_cast<double>(facts.velocity) / 30.0;
        auto dx = target[0] - origin[0];
        auto dz = target[2] - origin[2];
        auto flat = std::sqrt(dx * dx + dz * dz);
        if (speed <= 0.0 || flat <= 0.0)
        {
            return std::nullopt;
        }
        auto rise = target[1] - origin[1];
        auto inner = (tadGravity * flat * flat) + (2.0 * speed * speed * rise);
        auto discriminant = (speed * speed * speed * speed) - (tadGravity * inner);
        if (discriminant < 0.0)
        {
            return std::nullopt;
        }
        return std::atan(((speed * speed) - std::sqrt(discriminant)) / (tadGravity * flat));
    }

    /**
     * The step on which the round first stands in one of the victim's squares,
     * or nothing if it passes the footprint without landing in one.
     *
     * A port of tools/tad-weapontime.py's flight_model, which is the reference.
     * How far each step goes depends on the class: a constant-speed round
     * covers weaponvelocity / 30 every step, a self-propelled one gains its
     * acceleration up to the cap while the motor runs and then moves, as
     * updateSelfPropelledProjectile flies one, and a ballistic one covers
     * weaponvelocity / 30 * cos(pitch) HORIZONTALLY -- the ballistic branch of
     * updateProjectiles touches only velocity.y, so the horizontal half of the
     * launch vector never changes and the arc's fall never enters a flight
     * time. Where it stops does not depend on the class: TA detonates a round
     * the first tick its move puts it in a map square an enemy unit occupies
     * (0x49B090, straight after the move in 0x49B720), and a unit occupies its
     * footprint, stamped at its position with the left edge rounded to the
     * nearest square -- computeFootprintRegion. The aim point stands in for the
     * victim's position.
     *
     * The step number IS the flight time: the shot-to-damage interval is the
     * number of moves it took.
     */
    std::optional<int> tadFootprintFlight(
        const std::string& weaponClassName,
        const WeaponFacts& facts,
        const TadPosition& originFixed,
        const TadPosition& targetFixed,
        unsigned int footprintX,
        unsigned int footprintZ)
    {
        const double origin[3] = {tadFixedToDouble(originFixed.x), tadFixedToDouble(originFixed.y), tadFixedToDouble(originFixed.z)};
        const double target[3] = {tadFixedToDouble(targetFixed.x), tadFixedToDouble(targetFixed.y), tadFixedToDouble(targetFixed.z)};

        auto x0 = static_cast<long long>(std::floor((target[0] - footprintX * 8.0) / 16.0 + 0.5));
        auto z0 = static_cast<long long>(std::floor((target[2] - footprintZ * 8.0) / 16.0 + 0.5));

        auto ballistic = weaponClassName == "ballistic";
        auto dx = target[0] - origin[0];
        auto dy = target[1] - origin[1];
        auto dz = target[2] - origin[2];

        // The span the class's own step is measured along: the line in space
        // for a round that flies straight, and the horizontal distance for a
        // shell, whose horizontal speed is the only part that never changes.
        auto span = ballistic
            ? std::sqrt(dx * dx + dz * dz)
            : std::sqrt(dx * dx + dy * dy + dz * dz);
        auto ux = span > 0.0 ? dx / span : 0.0;
        auto uz = span > 0.0 ? dz / span : 0.0;

        // Past this the line has left any square the footprint could cover.
        auto giveUp = span + 16.0 * static_cast<double>(footprintX + footprintZ + 2);

        auto accelerating = weaponClassName == "accelerating";
        auto cap = static_cast<double>(facts.velocity) / 30.0;
        auto speed = accelerating ? tadLaunchSpeed(facts) : cap;
        if (ballistic)
        {
            auto pitch = tadBallisticPitch(facts, origin, target);
            if (!pitch)
            {
                return std::nullopt;
            }
            speed = cap * std::cos(*pitch);
        }
        auto acceleration = static_cast<double>(facts.acceleration) / 900.0;
        auto burn = tadBurnTicks(facts);

        double travelled = 0.0;
        for (unsigned int k = 1;; ++k)
        {
            if (accelerating && k <= burn)
            {
                speed = std::min(cap, speed + acceleration);
            }
            travelled += speed;

            auto sx = static_cast<long long>(std::floor((origin[0] + ux * travelled) / 16.0));
            auto sz = static_cast<long long>(std::floor((origin[2] + uz * travelled) / 16.0));
            if (sx >= x0 && sx < x0 + static_cast<long long>(footprintX)
                && sz >= z0 && sz < z0 + static_cast<long long>(footprintZ))
            {
                return static_cast<int>(k);
            }
            if (travelled > giveUp || k >= 4000)
            {
                return std::nullopt;
            }
        }
    }

    /**
     * The step the round stops on when the whole arc is flown, jitter included.
     *
     * The same horizontal stop as tadFootprintFlight, with two things it does
     * not need: the heading may be off the aim line, and the round is followed
     * in y as well, so that a shell the jitter sends into the ground SHORT of
     * the victim is reported as not having been stopped by the footprint at
     * all. A port of the script's ballistic_arc.
     */
    std::optional<int> tadBallisticArc(
        const WeaponFacts& facts,
        const double origin[3],
        const double target[3],
        unsigned int footprintX,
        unsigned int footprintZ,
        double pitch,
        double headingError)
    {
        auto x0 = static_cast<long long>(std::floor((target[0] - footprintX * 8.0) / 16.0 + 0.5));
        auto z0 = static_cast<long long>(std::floor((target[2] - footprintZ * 8.0) / 16.0 + 0.5));

        auto dx = target[0] - origin[0];
        auto dz = target[2] - origin[2];
        auto flat = std::sqrt(dx * dx + dz * dz);
        if (flat <= 0.0)
        {
            return std::nullopt;
        }

        auto speed = static_cast<double>(facts.velocity) / 30.0;
        auto bearing = std::atan2(dx, dz) + headingError;
        auto ux = std::sin(bearing);
        auto uz = std::cos(bearing);
        auto horizontal = speed * std::cos(pitch);
        auto rise = speed * std::sin(pitch);

        auto x = origin[0];
        auto y = origin[1];
        auto z = origin[2];
        auto giveUp = flat + 16.0 * static_cast<double>(footprintX + footprintZ + 2);

        for (unsigned int k = 1; k < 4000; ++k)
        {
            // updateProjectiles' ballistic branch, then the move: gravity onto
            // the velocity, and the position after it.
            rise -= tadGravity;
            x += ux * horizontal;
            y += rise;
            z += uz * horizontal;

            auto sx = static_cast<long long>(std::floor(x / 16.0));
            auto sz = static_cast<long long>(std::floor(z / 16.0));
            if (sx >= x0 && sx < x0 + static_cast<long long>(footprintX)
                && sz >= z0 && sz < z0 + static_cast<long long>(footprintZ))
            {
                return static_cast<int>(k);
            }
            // Coming down past the height the victim stands at, before reaching
            // it: the ground took the round and the footprint did not.
            if (rise < 0.0 && y < target[1])
            {
                return std::nullopt;
            }
            auto travelledX = x - origin[0];
            auto travelledZ = z - origin[2];
            if (std::sqrt(travelledX * travelledX + travelledZ * travelledZ) > giveUp)
            {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    /**
     * Whether the weapon's own aim jitter could move this pairing's answer.
     *
     * THE BALLISTIC CLASS'S SECOND BOUND, and the reason its cells read badly
     * without one. The original perturbs every turret shot before it spawns
     * (0x49D6D7): heading and pitch each take `rand(accuracy) - accuracy/2`,
     * widened by however hurt the shooter is and narrowed by its kills, and
     * `sprayangle` is a second draw on the heading alone. For a round that
     * flies level a pitch error of d costs tan(pitch)*d of the horizontal
     * speed, under one percent. For a shell it is a RANGE error of
     * 2*cot(2*pitch)*d, because the range goes as sin(2*pitch) -- ten percent
     * of the flight at CANNON_ART_MEDIUM's accuracy of 750, six or seven ticks
     * on a sixty-five-tick shell -- and nothing in a demo records the draw.
     *
     * So a ballistic pairing is scored only where replaying the whole arc at
     * the corners of the weapon's own cone gives the same answer. There is
     * nothing in the bound to tune: it is the weapon's declared `accuracy` run
     * through the original's own formula. It is a LOWER bound on the jitter,
     * because the health term opens the cone for a damaged shooter and hit
     * points are not in the stream.
     *
     * tools/tad-weapontime.py --cone prints the split it makes, and this is a
     * port of that script's within_aim_cone_bound.
     */
    bool tadWithinAimConeBound(
        const WeaponFacts& facts,
        const Pairing& pairing,
        unsigned int footprintX,
        unsigned int footprintZ)
    {
        const double origin[3] = {tadFixedToDouble(pairing.origin.x), tadFixedToDouble(pairing.origin.y), tadFixedToDouble(pairing.origin.z)};
        const double target[3] = {tadFixedToDouble(pairing.target.x), tadFixedToDouble(pairing.target.y), tadFixedToDouble(pairing.target.z)};

        auto pitch = tadBallisticPitch(facts, origin, target);
        if (!pitch)
        {
            return false;
        }
        auto answer = tadFootprintFlight("ballistic", facts, pairing.origin, pairing.target, footprintX, footprintZ);
        if (!answer)
        {
            return false;
        }

        // The health term cancels exactly at full health and veterancy only
        // narrows, so the weapon's own `accuracy` is the floor of the cone and
        // the width either side of the aim is half of it. `sprayangle` is drawn
        // on the heading only -- changeDirectionByRandomAngle rotates in XZ.
        auto pitchHalf = (static_cast<double>(facts.accuracy) / 2.0) * tadAngleUnit;
        auto headingHalf = (static_cast<double>(facts.accuracy + facts.sprayAngle) / 2.0) * tadAngleUnit;

        const double pitchErrors[3] = {-pitchHalf, 0.0, pitchHalf};
        const double headingErrors[3] = {-headingHalf, 0.0, headingHalf};
        for (auto pitchError : pitchErrors)
        {
            for (auto headingError : headingErrors)
            {
                auto arc = tadBallisticArc(facts, origin, target, footprintX, footprintZ, *pitch + pitchError, headingError);
                if (!arc || *arc != *answer)
                {
                    return false;
                }
            }
        }
        return true;
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
        // `burst` is asked BEFORE `ballistic`, and it has to be. A burst
        // weapon's isolation filter cannot mean what it means elsewhere
        // whatever shape its rounds fly, so a weapon that is both falls on the
        // excluded side. CANNON_FIDO is the one in this data set -- six shells
        // from one trigger, thrown off the aim line by a 1536 `sprayangle` --
        // and it sat in the ballistic table until that class became scored.
        if (facts.burst > 1)
        {
            return "burst";
        }
        if (facts.ballistic)
        {
            // TA dispatches a round's flight on `selfprop` first (0x49B9C2), so
            // a weapon carrying both flags is flown by the motor and not by the
            // arc, off a launch angle the ballistic solver chose. That is a
            // fourth shape and neither model. ROCKET_HEAVY is the only one here
            // and nothing in the corpus fires it, so it is named not modelled.
            if (facts.selfProp)
            {
                return "ballistic selfprop";
            }
            return "ballistic";
        }
        if (facts.velocity == 0)
        {
            return "no velocity";
        }
        // `cruise` and `twophase` are read only on the self-propelled path, and
        // each replaces the flight with a different shape -- a cruise missile
        // climbs to a fixed altitude and flies over the aim point before coming
        // down, a two-phase one turns over and restarts its motor -- so they are
        // classes of their own rather than cells that happen to read badly.
        // ROCKET_HRK is the one that matters over this corpus: `selfprop` with
        // `cruise`, and a start speed equal to its cap, so reading the
        // velocities alone put it in the constant-speed table.
        if (facts.selfProp && facts.cruise)
        {
            return "cruise";
        }
        if (facts.selfProp && facts.twoPhase)
        {
            return "two phase";
        }
        // Whether the round leaves the barrel at the speed it will keep, asked
        // the way the engine asks it rather than off the field alone -- see
        // tadLaunchSpeed for why the field alone does not answer it.
        if (std::abs(tadLaunchSpeed(facts) - static_cast<double>(facts.velocity) / 30.0) > 1e-6)
        {
            return "accelerating";
        }
        return "constant speed";
    }

    /**
     * How far the victim could have travelled while the shot was in the air, or
     * nothing where it could not be named.
     *
     * A 0x0d records WHERE THE SHOT WAS AIMED, so a victim that moves is not
     * where the distance says it is when the round arrives. Bucketing the whole
     * corpus by this number sorts both scored classes onto one monotone curve --
     * tools/tad-weapontime.py --drift prints it -- and it is what makes the
     * missile class scoreable: a laser crossing 200 units in six ticks barely
     * notices that its target moved and a missile spending thirty ticks getting
     * there does.
     */
    std::optional<double> tadVictimDrift(
        const std::map<std::string, UnitFacts>& unitFacts, const Pairing& pairing)
    {
        auto victim = unitFacts.find(pairing.victim);
        if (pairing.victim.empty() || victim == unitFacts.end())
        {
            return std::nullopt;
        }
        return static_cast<double>(victim->second.maxVelocity)
            * static_cast<double>(pairing.damageTick - pairing.shotTick);
    }

    /**
     * Whether a pairing may be scored, which depends on its weapon's class.
     *
     * Every class needs its victim named, because the round stops on the
     * victim's footprint. A constant-speed cell is then scored over every such
     * victim; its pairings are nearly all at the still end of the drift curve
     * already. A self-propelled one is
     * scored only where the victim could not have outrun ONE STEP of the
     * projectile -- the projectile's own step and not a constant, because the
     * quantity being measured is quantised in steps. The bound is not tuned: it
     * was fixed under the aim-point model, and the footprint model was scored
     * under it unchanged.
     *
     * A ballistic cell takes that same bound AND the aim cone, for the reasons
     * tadWithinAimConeBound gives: its flights are three to ten times longer,
     * so the drift bound bites far harder, and a pitch error is a range error
     * rather than a speed error.
     */
    bool tadScoreablePairing(
        const std::string& weaponClassName,
        const WeaponFacts& facts,
        const std::map<std::string, UnitFacts>& unitFacts,
        const Pairing& pairing)
    {
        auto drift = tadVictimDrift(unitFacts, pairing);
        if (!drift)
        {
            return false;
        }
        if (weaponClassName != "accelerating" && weaponClassName != "ballistic")
        {
            return true;
        }
        if (*drift >= static_cast<double>(facts.velocity) / 30.0)
        {
            return false;
        }
        if (weaponClassName != "ballistic")
        {
            return true;
        }
        const auto& victim = unitFacts.at(pairing.victim);
        return tadWithinAimConeBound(facts, pairing, victim.footprintX, victim.footprintZ);
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
        for (const auto& [key, everything] : grouped)
        {
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

            auto className = weaponClass(weapon->second);

            // The class picks both the model and the victims it may be scored
            // over, and minPairings applies to what survives the second -- so a
            // missile cell whose pairings were nearly all against aircraft drops
            // out rather than being scored thin.
            std::vector<const Pairing*> group;
            for (const auto* p : everything)
            {
                if (tadScoreablePairing(className, weapon->second, unitFacts, *p))
                {
                    group.push_back(p);
                }
            }
            if (group.size() < minPairings)
            {
                continue;
            }

            // flight - model, or nothing where the model has the round step
            // over the footprint without landing in it. A miss counts against
            // the cell's share and never wins its mode.
            auto deltaFor = [&](const Pairing& p) -> std::optional<int> {
                const auto& victim = unitFacts.at(p.victim);
                auto model = tadFootprintFlight(className, weapon->second, p.origin, p.target, victim.footprintX, victim.footprintZ);
                if (!model)
                {
                    return std::nullopt;
                }
                return static_cast<int>(p.damageTick - p.shotTick) - *model;
            };

            std::map<int, unsigned int> deltas;
            std::map<unsigned int, unsigned int> damages;
            int mode = 0;
            unsigned int atMode = 0;
            for (const auto* p : group)
            {
                ++damages[p->damage];
                auto delta = deltaFor(*p);
                if (!delta)
                {
                    continue;
                }
                auto count = ++deltas[*delta];
                if (count > atMode)
                {
                    mode = *delta;
                    atMode = count;
                }
            }
            if (atMode == 0)
            {
                continue;
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
                auto delta = deltaFor(*p);
                if (!delta || *delta != mode)
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
                key.first, key.second, weaponName, className, weapon->second.selfProp, weapon->second.burnBlow, weapon->second.velocity, weapon->second.startVelocity, weapon->second.acceleration, weapon->second.range, weapon->second.weaponTimer, weapon->second.noAutoRange, weapon->second.defaultDamage, static_cast<unsigned int>(group.size()), atMode, static_cast<unsigned int>(everything.size() - group.size()), mode, modalDamage, representative->demo, representative->shotTick, representative->damageTick, representative->origin, representative->target, representative->victim, unitFacts.at(representative->victim).footprintX, unitFacts.at(representative->victim).footprintZ});
        }

        std::sort(out.begin(), out.end(), [](const WeaponCell& a, const WeaponCell& b) {
            return std::tie(a.shooter, a.slot) < std::tie(b.shooter, b.slot);
        });
        return out;
    }


    /**
     * Writes the weapon-flight episodes.
     *
     * One episode per scored cell, over both scored classes, and only the cells
     * the model predicts. A cell that does not is SKIPPED rather than checked
     * in with its offset written into expectedFlightDelta, because that field
     * is for a divergence somebody decided on and not for an observation nobody
     * has explained -- the same rule that once kept airborne builders out of
     * the build fixture, until TOTALA-EXE.md §107 explained their tick and RWE
     * was changed to match. Under the footprint model there are no skipped
     * cells; under the aim-point model it replaced there were four.
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
            if (weapon == weaponFacts.end())
            {
                continue;
            }
            if (cell.weaponClass != "constant speed" && cell.weaponClass != "accelerating"
                && cell.weaponClass != "ballistic")
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
// WHICH SHOTS MAY BE HERE. The three classes the models describe: weapons that
// fly at a constant speed, weapons with a motor, and weapons that lob a shell. A
// vlaunch rocket goes up before it goes anywhere, a cruise missile climbs and
// crosses the aim point before coming down, a torpedo travels through water, and
// a burst weapon fires several rounds from one trigger so the isolation filter
// cannot mean what it means elsewhere. Those four are four more oracles, not
// discrepancies. See docs/TA-DEMOS.md.
//
// THE ARITHMETIC BEING PINNED. A round does not stop at the point it was aimed
// at. It detonates the first tick its move puts it in a map square an enemy unit
// occupies (0x49B090, straight after the move in 0x49B720), and a unit occupies
// its FootprintX by FootprintZ squares -- so the flight time is the number of
// steps until the round stands on the victim's footprint, which is about half a
// footprint short of the aim point. How far a step goes depends on the class: a
// constant-speed projectile covers weaponVelocity / 30 world units -- the same
// conversion LoadingScene_util.cpp does -- a self-propelled one leaves at
// startVelocity and gains weaponAcceleration / 900 a tick up to the same cap
// while its motor runs, and coasts after, and a BALLISTIC one covers
// weaponVelocity / 30 times the cosine of the angle its gun elevated to,
// horizontally, every tick. That angle is the flat root of the firing solution
// 0x49A890 solves and computeBallisticHeadingAndPitch reproduces; the pi/4
// ceiling at 0x49AA11 rejects the high root for every target inside the gun's
// range, so there is no lofted artillery arc in the game. The falling half of
// the arc never enters a flight time, because the ballistic branch of
// updateProjectiles touches only velocity.y. RWE moves a projectile and then
// tests it against the occupied grid, in that order, so expectedFlightDelta is
// zero everywhere below. The field is kept because the fixture's whole purpose
// is to survive a deliberate divergence; a non-zero value here would have to
// name the docs/TOTALA-EXE.md section that licensed it, exactly as the build
// fixture's does.
//
// WHY THE MISSILE AND SHELL ROWS NAME THEIR VICTIM. A 0x0d records where the
// shot was AIMED, so a victim that moves while the round is in the air is not
// where the distance says it is when it arrives. Over a missile's twenty to
// forty ticks, or a shell's thirty to sixty, that is most of the error, so those
// two classes are scored only over the pairings whose victim could not have
// outrun one step of the projectile -- `victimName` is the representative's, and
// it is a building or a slow ground unit in every such row here. A
// constant-speed cell needs no such bound and gets none.
// tools/tad-weapontime.py --drift prints the measurement behind that.
//
// AND WHY THERE ARE SO FEW SHELL ROWS. A ballistic cell carries a second bound
// the other two do not need. The original jitters every turret shot's heading
// AND pitch by rand(accuracy) - accuracy/2 (0x49D6D7); for a round that flies
// level a pitch error costs under a percent of its horizontal speed, but for a
// shell it is a range error of 2*cot(2*pitch) times the error, ten percent of
// the flight at CANNON_ART_MEDIUM's accuracy of 750. Nothing in a demo records
// the draw, so a ballistic pairing is scored only where replaying the arc at the
// corners of the weapon's own cone gives the same answer -- which leaves the
// artillery cells, the ones with by far the most pairings in the corpus, with
// nothing to be scored over. tools/tad-weapontime.py --cone prints that split
// and the cells it costs.

namespace rwe
{
    struct TadWeaponEpisode
    {
        const char* demo;
        const char* shooterName;
        unsigned int weaponSlot;
        const char* weaponName;

        /** What was shot at -- see the note on the victim bound above. */
        const char* victimName;

        /**
         * The victim's FBI FootprintX and FootprintZ, in map squares: what the
         * round stops on. The victim stands at the aim point.
         */
        unsigned int victimFootprintX;
        unsigned int victimFootprintZ;

        /** The tick the shot was fired on, and the tick its damage arrived. */
        uint32_t shotTick;
        uint32_t damageTick;

        /** How many pairings the cell held, and how many shared this flight time. */
        unsigned int pairings;
        unsigned int pairingsAtMode;

        /**
         * Whether the round has a motor, and whether it is a shell. The two are
         * exclusive and either may be false, which is the constant-speed class.
         * Between them they decide which model the episode is held to and which
         * physics type the test builds for it.
         */
        bool selfPropelled;
        bool ballistic;

        /** TDF weaponvelocity, in world units a SECOND. Divide by 30 for a tick. */
        unsigned int weaponVelocity;

        /**
         * TDF startvelocity and weaponacceleration, also per second (and per
         * second squared). Both zero for a constant-speed round. A startVelocity
         * of zero with an acceleration means the missile leaves from a
         * standstill; with no acceleration it means full speed.
         */
        unsigned int startVelocity;
        unsigned int weaponAcceleration;

        /**
         * What times the motor: `weaponRange / weaponVelocity` ticks, or
         * `weaponTimerTicks` where the weapon says noAutoRange. Two episodes
         * below outlive their motor and coast the rest of the way in --
         * CORMIST's, seventeen steps against a fifteen-tick burn, and
         * ARMAABOT's, sixteen against the same -- so these are load-bearing for
         * those two and describe every other.
         */
        unsigned int weaponRange;
        unsigned int weaponTimerTicks;
        bool noAutoRange;

        /**
         * Whether running out of motor detonates the round where it is instead
         * of letting it coast on. The two episodes that do outlive their motor
         * have it clear, so nothing here detonates early; it travels with them so
         * that a regeneration producing a burnblow round that did could not
         * quietly be flown as though it coasted.
         */
        bool burnBlow;

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
                << cell->slot << ", \"" << cell->weapon << "\", \"" << cell->victim << "\", "
                << cell->victimFootprintX << ", " << cell->victimFootprintZ << ",\n"
                << "            " << cell->shotTick << ", " << cell->damageTick << ", "
                << cell->pairings << ", " << cell->pairingsAtMode << ",\n"
                << "            " << (cell->selfProp ? "true" : "false") << ", "
                << (cell->weaponClass == "ballistic" ? "true" : "false") << ", "
                << cell->velocity << ", " << cell->startVelocity << ", " << cell->acceleration << ",\n"
                << "            " << cell->range << ", "
                << static_cast<unsigned int>(cell->weaponTimer * 30.0f) << ", "
                << (cell->noAutoRange ? "true" : "false") << ", "
                << (cell->burnBlow ? "true" : "false") << ", " << cell->damage << ",\n"
                << "            " << cell->origin.x << ", " << cell->origin.y << ", " << cell->origin.z << ",\n"
                << "            " << cell->target.x << ", " << cell->target.y << ", " << cell->target.z << ",\n"
                << "            " << (cell->damageTick - cell->shotTick) << ", 0, nullptr},\n";
        }

        out << "    };\n"
            << "    // clang-format on\n"
            << "}\n";
    }

    // --- --unit-state: decode every 0x2c and check it against the stream ---
    //
    // The decode came out of TotalA.exe (docs/TA-DEMOS.md, "0x2c, unit state"),
    // so what this pass exists to do is hold it to the corpus, with checks that
    // share nothing with the decoder: where a 0x09 put a nanoframe, how fast the
    // FBI says a unit can go, whether a building stays where it was, and where a
    // 0x0d says a shooter stood. A decode that failed those would not be a
    // decode, so the pass exits non-zero if anything fails to decode at all, and
    // prints the rest for reading.

    struct UnitStateTally
    {
        unsigned long decoded = 0;
        unsigned long failed = 0;
        unsigned long updates = 0;
        unsigned long groundUpdates = 0;
        unsigned long airUpdates = 0;
        unsigned long syncs = 0;
        unsigned long carried = 0;

        // Against the 0x09 that created the unit.
        unsigned long buildsChecked = 0;
        unsigned long buildTypeAgrees = 0;
        unsigned long buildPositionChecked = 0;
        unsigned long buildPositionAgrees = 0;
        unsigned long buildRotationAgrees = 0;

        // Against the FBI.
        unsigned long speedPresentMobile = 0;
        unsigned long speedPresentImmobile = 0;
        unsigned long speedAbsentMobile = 0;
        unsigned long speedAbsentImmobile = 0;
        unsigned long speedWithinMax = 0;
        unsigned long speedOverMax = 0;

        // Between successive syncs of one unit, maxUnits ticks apart.
        unsigned long immobileSame = 0;
        unsigned long immobileMoved = 0;
        unsigned long mobileWithinReach = 0;
        unsigned long mobileTooFar = 0;

        // A ground unit's first waypoint against where a 0x0d within ten ticks
        // puts it; and the same distance to another unit's waypoint, as the
        // control that says what "near" is worth.
        std::array<unsigned long, 4> waypointDistance{};
        std::array<unsigned long, 4> controlDistance{};

        // A 0x0d's origin and aim point against the full-state position of an
        // immobile shooter or target: exact, under 8, under 32, further.
        std::array<unsigned long, 4> shooterDistance{};
        std::array<unsigned long, 4> targetDistance{};

        void add(const UnitStateTally& o)
        {
            decoded += o.decoded;
            failed += o.failed;
            updates += o.updates;
            groundUpdates += o.groundUpdates;
            airUpdates += o.airUpdates;
            syncs += o.syncs;
            carried += o.carried;
            buildsChecked += o.buildsChecked;
            buildTypeAgrees += o.buildTypeAgrees;
            buildPositionChecked += o.buildPositionChecked;
            buildPositionAgrees += o.buildPositionAgrees;
            buildRotationAgrees += o.buildRotationAgrees;
            speedPresentMobile += o.speedPresentMobile;
            speedPresentImmobile += o.speedPresentImmobile;
            speedAbsentMobile += o.speedAbsentMobile;
            speedAbsentImmobile += o.speedAbsentImmobile;
            speedWithinMax += o.speedWithinMax;
            speedOverMax += o.speedOverMax;
            immobileSame += o.immobileSame;
            immobileMoved += o.immobileMoved;
            mobileWithinReach += o.mobileWithinReach;
            mobileTooFar += o.mobileTooFar;
            for (std::size_t i = 0; i < 4; ++i)
            {
                waypointDistance[i] += o.waypointDistance[i];
                controlDistance[i] += o.controlDistance[i];
                shooterDistance[i] += o.shooterDistance[i];
                targetDistance[i] += o.targetDistance[i];
            }
        }
    };

    struct UnitStateHandler : TadHandler
    {
        const std::vector<std::string>& loadOrder;
        const std::map<std::string, UnitFacts>& unitFacts;
        std::ostream* json;
        bool withUpdates;

        std::optional<TadHeader> header;
        std::optional<TadUnitTable> unitTable;
        std::optional<TadUnitStateLayout> layout;
        std::string demo;

        std::map<uint8_t, uint32_t> tick;

        struct Sync
        {
            uint32_t tick;
            TadUnitSync state;
        };

        /** Keyed on (sender, index within its block). */
        std::map<std::pair<uint8_t, uint16_t>, std::vector<Sync>> syncs;

        struct Build
        {
            uint8_t sender;
            uint32_t tick;
            TadBuildStarted event;
        };
        std::vector<Build> builds;

        struct Waypoint
        {
            uint8_t sender;
            uint32_t tick;
            uint16_t index;
            TadWaypoint first;
        };
        std::vector<Waypoint> waypoints;

        /** Every 0x0d shooter position, by global id, in tick order. */
        std::map<uint16_t, std::vector<std::pair<uint32_t, TadPosition>>> shooters;

        /** Every 0x0d aimed at a unit: (target id, aim point). */
        std::vector<std::pair<uint16_t, TadPosition>> aims;

        UnitStateTally tally;
        int32_t minX = INT32_MAX, maxX = INT32_MIN, minZ = INT32_MAX, maxZ = INT32_MIN;

        UnitStateHandler(
            const std::vector<std::string>& loadOrder,
            const std::map<std::string, UnitFacts>& unitFacts,
            std::ostream* json,
            bool withUpdates,
            std::string demo)
            : loadOrder(loadOrder), unitFacts(unitFacts), json(json), withUpdates(withUpdates), demo(std::move(demo))
        {
        }

        const UnitFacts* facts(uint16_t typeIndex) const
        {
            auto name = tadUnitNameForTypeIndex(loadOrder, typeIndex);
            if (!name)
            {
                return nullptr;
            }
            auto it = unitFacts.find(*name);
            return it == unitFacts.end() ? nullptr : &it->second;
        }

        std::string name(uint16_t typeIndex) const
        {
            auto n = tadUnitNameForTypeIndex(loadOrder, typeIndex);
            return n ? *n : std::to_string(typeIndex);
        }

        void onHeader(const TadHeader& h) override
        {
            header = h;
        }

        void onUnitData(const TadBytes& record) override
        {
            unitTable = tadDecodeUnitTable(record);
            if (!header || !unitTable || unitTable->restricted.size() != loadOrder.size())
            {
                return;
            }

            std::vector<bool> canFly;
            for (std::size_t i = 1; i <= loadOrder.size(); ++i)
            {
                auto f = facts(static_cast<uint16_t>(i));
                canFly.push_back(f && f->canFly);
            }
            layout = tadUnitStateLayout(canFly, header->maxUnits);
        }

        void writePosition(std::ostream& out, const TadPosition& p)
        {
            out << "[" << tadFixedToDouble(p.x) << "," << tadFixedToDouble(p.y) << "," << tadFixedToDouble(p.z) << "]";
        }

        void onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets, const TadWalkStats&) override
        {
            if (!layout)
            {
                return;
            }

            for (const auto& s : subPackets)
            {
                if (s.empty())
                {
                    continue;
                }

                auto code = static_cast<TadSubPacketCode>(s[0]);
                if (code == TadSubPacketCode::UnitBuildStarted)
                {
                    if (auto e = tadDecodeBuildStarted(s); e && tick.count(packet.sender))
                    {
                        builds.push_back(Build{packet.sender, tick[packet.sender], *e});
                    }
                    continue;
                }
                if (code == TadSubPacketCode::WeaponFired)
                {
                    if (auto e = tadDecodeShot(s); e && tick.count(packet.sender))
                    {
                        shooters[e->shooterId].emplace_back(tick[packet.sender], e->origin);
                        if (e->targetId != 0)
                        {
                            aims.emplace_back(e->targetId, e->target);
                        }
                    }
                    continue;
                }
                if (code != TadSubPacketCode::UnitStatAndMove)
                {
                    continue;
                }

                auto state = tadDecodeUnitState(s, *layout);
                if (!state)
                {
                    ++tally.failed;
                    continue;
                }
                ++tally.decoded;
                tick[packet.sender] = state->tick;

                for (const auto& u : state->updates)
                {
                    ++tally.updates;
                    if (auto path = std::get_if<TadGroundPath>(&u.mover))
                    {
                        ++tally.groundUpdates;
                        if (!path->waypoints.empty())
                        {
                            waypoints.push_back(Waypoint{packet.sender, state->tick, u.index, path->waypoints.front()});
                        }
                    }
                    else
                    {
                        ++tally.airUpdates;
                    }

                    if (json && withUpdates)
                    {
                        auto& out = *json;
                        out << "{\"demo\":\"" << demo << "\",\"sender\":" << unsigned(packet.sender)
                            << ",\"tick\":" << state->tick << ",\"kind\":\"update\",\"index\":" << u.index
                            << ",\"type\":\"" << name(u.typeIndex) << "\"";
                        if (auto path = std::get_if<TadGroundPath>(&u.mover))
                        {
                            out << ",\"blocked\":" << (path->blocked ? "true" : "false") << ",\"waypoints\":[";
                            for (std::size_t i = 0; i < path->waypoints.size(); ++i)
                            {
                                out << (i ? "," : "") << "[" << path->waypoints[i].x << "," << path->waypoints[i].z << "]";
                            }
                            out << "]";
                        }
                        else
                        {
                            const auto& air = std::get<TadAirMover>(u.mover);
                            out << ",\"movementMode\":" << unsigned(air.movementMode);
                            if (auto goal = std::get_if<TadMoveGoal>(&air.goal))
                            {
                                out << ",\"goalFlags\":" << unsigned(goal->flags);
                                if (goal->position)
                                {
                                    out << ",\"goal\":";
                                    writePosition(out, *goal->position);
                                }
                                if (goal->attachedUnitId)
                                {
                                    out << ",\"goalUnit\":" << *goal->attachedUnitId;
                                }
                            }
                            else if (auto moving = std::get_if<TadMovingGoal>(&air.goal))
                            {
                                out << ",\"movingGoal\":";
                                writePosition(out, moving->position);
                                out << ",\"goalVelocity\":";
                                writePosition(out, moving->velocity);
                            }
                        }
                        out << "}\n";
                    }
                }

                if (state->sync && state->sync->typeIndex != 0)
                {
                    const auto& u = *state->sync;
                    ++tally.syncs;
                    if (u.carried)
                    {
                        ++tally.carried;
                    }
                    else
                    {
                        minX = std::min(minX, u.position.x);
                        maxX = std::max(maxX, u.position.x);
                        minZ = std::min(minZ, u.position.z);
                        maxZ = std::max(maxZ, u.position.z);
                    }
                    syncs[{packet.sender, u.index}].push_back(Sync{state->tick, u});

                    if (json)
                    {
                        auto& out = *json;
                        out << "{\"demo\":\"" << demo << "\",\"sender\":" << unsigned(packet.sender)
                            << ",\"tick\":" << state->tick << ",\"kind\":\"sync\",\"index\":" << u.index
                            << ",\"type\":\"" << name(u.typeIndex) << "\",\"health\":" << u.health
                            << ",\"buildProgress\":" << unsigned(u.buildProgress)
                            << ",\"flags10E\":" << unsigned(u.flags10E)
                            << ",\"motionState\":" << unsigned(u.motionState);
                        if (u.carried)
                        {
                            out << ",\"carrier\":" << u.carried->carrierId << ",\"piece\":" << int(u.carried->piece);
                        }
                        else
                        {
                            out << ",\"position\":";
                            writePosition(out, u.position);
                            out << ",\"rotation\":[" << u.rotation.x << "," << u.rotation.y << "," << u.rotation.z << "]";
                            if (u.speed)
                            {
                                out << ",\"speed\":" << tadFixedToDouble(*u.speed);
                            }
                        }
                        out << "}\n";
                    }
                }
            }
        }

        static std::size_t distanceBucket(double d)
        {
            return d < 8.0 ? 0 : d < 32.0 ? 1 : d < 128.0 ? 2 : 3;
        }

        /** Runs the checks once the whole demo is in. */
        void check()
        {
            if (!layout)
            {
                return;
            }
            auto maxUnits = header->maxUnits;

            // Which block each sender's own units live in, learned from its builds.
            std::map<uint8_t, std::map<unsigned int, unsigned int>> blockVotes;
            for (const auto& b : builds)
            {
                if (auto block = tadOwnerBlockOfUnitId(b.event.unitId, maxUnits))
                {
                    ++blockVotes[b.sender][*block];
                }
            }
            std::map<uint8_t, unsigned int> blockOf;
            for (const auto& [sender, votes] : blockVotes)
            {
                auto best = std::max_element(votes.begin(), votes.end(), [](const auto& a, const auto& b) {
                    return a.second < b.second;
                });
                blockOf[sender] = best->first;
            }

            // The first sync of the built unit's slot after its 0x09.
            for (const auto& b : builds)
            {
                auto block = tadOwnerBlockOfUnitId(b.event.unitId, maxUnits);
                if (!block)
                {
                    continue;
                }
                auto index = static_cast<uint16_t>(b.event.unitId - 1 - *block * maxUnits);
                auto it = syncs.find({b.sender, index});
                if (it == syncs.end())
                {
                    continue;
                }
                auto next = std::lower_bound(it->second.begin(), it->second.end(), b.tick, [](const Sync& s, uint32_t t) {
                    return s.tick < t;
                });
                if (next == it->second.end() || next->tick - b.tick > maxUnits)
                {
                    continue;
                }

                ++tally.buildsChecked;
                if (next->state.typeIndex != b.event.typeIndex)
                {
                    continue;
                }
                ++tally.buildTypeAgrees;

                // A unit that can move may already have left the pad.
                auto f = facts(b.event.typeIndex);
                if (!f || f->maxVelocity > 0.0f || next->state.carried)
                {
                    continue;
                }
                ++tally.buildPositionChecked;
                if (next->state.position == b.event.position)
                {
                    ++tally.buildPositionAgrees;
                    if (next->state.rotation == b.event.rotation)
                    {
                        ++tally.buildRotationAgrees;
                    }
                }
            }

            for (const auto& [key, list] : syncs)
            {
                for (std::size_t i = 0; i < list.size(); ++i)
                {
                    const auto& s = list[i].state;
                    auto f = facts(s.typeIndex);
                    if (!f || s.carried)
                    {
                        continue;
                    }
                    auto mobile = f->maxVelocity > 0.0f;
                    if (s.speed)
                    {
                        ++(mobile ? tally.speedPresentMobile : tally.speedPresentImmobile);
                        if (mobile)
                        {
                            ++(tadFixedToDouble(*s.speed) <= f->maxVelocity * 1.1 + 0.01 ? tally.speedWithinMax : tally.speedOverMax);
                        }
                    }
                    else
                    {
                        ++(mobile ? tally.speedAbsentMobile : tally.speedAbsentImmobile);
                    }

                    if (i == 0)
                    {
                        continue;
                    }
                    const auto& p = list[i - 1];
                    if (p.state.typeIndex != s.typeIndex || p.state.carried || list[i].tick - p.tick != maxUnits)
                    {
                        continue;
                    }
                    auto dx = tadFixedToDouble(s.position.x) - tadFixedToDouble(p.state.position.x);
                    auto dz = tadFixedToDouble(s.position.z) - tadFixedToDouble(p.state.position.z);
                    auto d = std::sqrt(dx * dx + dz * dz);
                    if (!mobile)
                    {
                        ++(d == 0.0 ? tally.immobileSame : tally.immobileMoved);
                    }
                    else
                    {
                        ++(d <= f->maxVelocity * maxUnits * 1.1 + 1.0 ? tally.mobileWithinReach : tally.mobileTooFar);
                    }
                }
            }

            for (auto& [id, list] : shooters)
            {
                std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            }

            // An immobile unit's position is the same in every full-state record,
            // so any of them will do: compare a shot's origin and aim point to it
            // horizontally. The origin is the firing piece, not the unit's
            // origin, so it lands near rather than on.
            std::map<unsigned int, uint8_t> senderOfBlock;
            for (const auto& [sender, block] : blockOf)
            {
                senderOfBlock[block] = sender;
            }
            auto immobilePosition = [&](uint16_t id) -> std::optional<TadPosition> {
                auto block = tadOwnerBlockOfUnitId(id, maxUnits);
                if (!block || !senderOfBlock.count(*block))
                {
                    return std::nullopt;
                }
                auto it = syncs.find({senderOfBlock[*block], static_cast<uint16_t>(id - 1 - *block * maxUnits)});
                if (it == syncs.end() || it->second.size() < 2)
                {
                    return std::nullopt;
                }
                // Only a slot that held one immobile type, unmoved, all game,
                // so that a recycled id cannot put the shot at the wrong unit.
                const auto& first = it->second.front().state;
                auto f = facts(first.typeIndex);
                if (!f || f->maxVelocity > 0.0f || first.carried)
                {
                    return std::nullopt;
                }
                for (const auto& s : it->second)
                {
                    if (s.state.typeIndex != first.typeIndex || s.state.carried || !(s.state.position == first.position))
                    {
                        return std::nullopt;
                    }
                }
                return first.position;
            };
            auto exactBucket = [](const TadPosition& a, const TadPosition& b) -> std::size_t {
                auto d = std::max(
                    std::abs(tadFixedToDouble(a.x) - tadFixedToDouble(b.x)),
                    std::abs(tadFixedToDouble(a.z) - tadFixedToDouble(b.z)));
                return d == 0.0 ? 0 : d < 8.0 ? 1 : d < 32.0 ? 2 : 3;
            };
            for (const auto& [id, list] : shooters)
            {
                if (auto p = immobilePosition(id))
                {
                    for (const auto& shot : list)
                    {
                        ++tally.shooterDistance[exactBucket(shot.second, *p)];
                    }
                }
            }
            for (const auto& [id, aim] : aims)
            {
                if (auto p = immobilePosition(id))
                {
                    ++tally.targetDistance[exactBucket(aim, *p)];
                }
            }

            // A fixed stride through the waypoints stands in for a random control,
            // so a re-run prints the same numbers.
            std::size_t control = waypoints.size() / 2 + 1;
            for (std::size_t w = 0; w < waypoints.size(); ++w)
            {
                const auto& wp = waypoints[w];
                auto block = blockOf.find(wp.sender);
                if (block == blockOf.end())
                {
                    continue;
                }
                auto id = tadUnitIdOfIndex(block->second, wp.index, maxUnits);
                auto it = shooters.find(id);
                if (it == shooters.end())
                {
                    continue;
                }
                auto next = std::lower_bound(it->second.begin(), it->second.end(), wp.tick, [](const auto& s, uint32_t t) {
                    return s.first < t;
                });
                const TadPosition* best = nullptr;
                uint32_t bestGap = 11;
                for (auto c : {next, next == it->second.begin() ? next : std::prev(next)})
                {
                    if (c == it->second.end())
                    {
                        continue;
                    }
                    auto gap = c->first > wp.tick ? c->first - wp.tick : wp.tick - c->first;
                    if (gap < bestGap)
                    {
                        bestGap = gap;
                        best = &c->second;
                    }
                }
                if (!best)
                {
                    continue;
                }
                auto x = tadFixedToDouble(best->x);
                auto z = tadFixedToDouble(best->z);
                auto chebyshev = [&](const TadWaypoint& p) {
                    return std::max(std::abs(x - p.x), std::abs(z - p.z));
                };
                ++tally.waypointDistance[distanceBucket(chebyshev(wp.first))];
                ++tally.controlDistance[distanceBucket(chebyshev(waypoints[(w + control) % waypoints.size()].first))];
            }
        }
    };

    void printUnitStateTally(const UnitStateTally& t, const std::string& indent)
    {
        auto pct = [](unsigned long a, unsigned long b) {
            std::ostringstream ss;
            ss << a << "/" << b << " (" << std::fixed << std::setprecision(1) << (b ? 100.0 * a / b : 0.0) << "%)";
            return ss.str();
        };
        auto buckets = [](const std::array<unsigned long, 4>& a) {
            std::ostringstream ss;
            ss << "<8 " << a[0] << ", <32 " << a[1] << ", <128 " << a[2] << ", further " << a[3];
            return ss.str();
        };
        std::cout << indent << "0x2c decoded " << t.decoded << ", failed " << t.failed
                  << "; " << t.updates << " mover updates (" << t.groundUpdates << " ground, " << t.airUpdates << " air), "
                  << t.syncs << " full-state records (" << t.carried << " attached)\n"
                  << indent << "  sync after a 0x09, type agrees: " << pct(t.buildTypeAgrees, t.buildsChecked)
                  << "; immobile position agrees: " << pct(t.buildPositionAgrees, t.buildPositionChecked)
                  << ", rotation too: " << t.buildRotationAgrees << "\n"
                  << indent << "  speed present: mobile " << t.speedPresentMobile << ", immobile " << t.speedPresentImmobile
                  << "; absent: mobile " << t.speedAbsentMobile << ", immobile " << t.speedAbsentImmobile
                  << "; within 1.1 x MaxVelocity " << pct(t.speedWithinMax, t.speedWithinMax + t.speedOverMax) << "\n"
                  << indent << "  one cycle apart: immobile unmoved " << pct(t.immobileSame, t.immobileSame + t.immobileMoved)
                  << ", mobile within MaxVelocity reach " << pct(t.mobileWithinReach, t.mobileWithinReach + t.mobileTooFar) << "\n"
                  << indent << "  first waypoint to 0x0d shooter (<=10 ticks): " << buckets(t.waypointDistance) << "\n"
                  << indent << "  another unit's waypoint, as control:        " << buckets(t.controlDistance) << "\n"
                  << indent << "  immobile shooter, 0x0d origin to its position: exact " << t.shooterDistance[0]
                  << ", <8 " << t.shooterDistance[1] << ", <32 " << t.shooterDistance[2] << ", further " << t.shooterDistance[3] << "\n"
                  << indent << "  immobile target, 0x0d aim point to its position: exact " << t.targetDistance[0]
                  << ", <8 " << t.targetDistance[1] << ", <32 " << t.targetDistance[2] << ", further " << t.targetDistance[3] << "\n";
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
                  << "  --stall-episodes  print the settle phase, the stall quantum and the stall episodes\n"
                  << "  --emit-stall-cpp  write the stall episodes as a C++ header (needs --units)\n"
                  << "  --cells       print the (builder, product) build-timing cells\n"
                  << "  --weapon-cells  print the (shooter, weapon slot) flight-time cells\n"
                  << "  --emit-weapon-cpp  write the weapon-flight episodes as a C++ header\n"
                  << "  --window      isolation window for the weapon pass, ticks (default 300)\n"
                  << "  --min-pairings  pairings a weapon cell needs (default 30)\n"
                  << "  --weapon-slots  check the 0x0d trailing byte against each shooter's FBI\n"
                  << "  --unit-state  decode every 0x2c and check it against the stream (needs --units)\n"
                  << "  --emit-unit-state  also write the decoded full-state records as JSON Lines\n"
                  << "  --with-updates  with --emit-unit-state, write the per-tick mover updates too\n"
                  << "  --max-types   distinct unit types an episode may carry (default 6)\n"
                  << "  --max-per-player  episodes to keep per player (default 3)\n"
                  << "  --max-cells   build-timing episodes to check in (default 28)\n"
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
    std::size_t maxCells = args.contains("max-cells") ? std::stoul(args.getString("max-cells")) : 28;

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

    // The stall pass needs every build, rejected or not -- the builds it is about
    // are the ones marked "owner stalled" -- and every resource sample, so it
    // keeps its own copy of each demo rather than depending on --all.
    auto wantStalls = args.contains("stall-episodes") || args.contains("emit-stall-cpp");
    std::vector<StallDemo> stallDemos;

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

        if (wantStalls)
        {
            stallDemos.push_back(StallDemo{handler.demo, handler.episodes, handler.resourceRecords, handler.senderBlock});
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
        std::vector<const BuildCell*> airborne;
        for (const auto& cell : buildCells)
        {
            if (cell.kind == "immobile")
            {
                scored.push_back(&cell);
            }
            else if (cell.kind == "airborne")
            {
                airborne.push_back(&cell);
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

        // Two classes are scored, each against its own model, and the third is
        // not scored at all: a ground mobile builder pays its own COB deploy
        // before INBUILDSTANCE, which is mod data rather than engine behaviour.
        // The reference script splits the same three ways and prints the same
        // two tables. docs/TA-DEMOS.md.
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

        // The airborne table, against two increments on the 0x09's tick
        // (TOTALA-EXE.md section 107). Its model column is already two less
        // than the increment count, which is why it is printed rather than the
        // immobile table's "extra": the two would not mean the same thing.
        std::sort(airborne.begin(), airborne.end(), [](const BuildCell* a, const BuildCell* b) {
            return std::tie(b->builds, a->builder, a->product) < std::tie(a->builds, b->builder, b->product);
        });
        std::cout << "\nthe " << airborne.size()
                  << " pairs whose builder flies, against two increments on the 0x09's tick:\n\n"
                  << "  builder    product      BuildTime   p  model   mode     n  share  delta\n";
        for (const auto* cell : airborne)
        {
            auto delta = static_cast<int>(cell->integerModel) - static_cast<int>(cell->mode);
            auto agrees = cell->mode == cell->floatModel;
            misses += agrees ? 0 : 1;
            std::cout << "  " << std::left << std::setw(10) << cell->builder << " "
                      << std::setw(12) << cell->product << std::right
                      << std::setw(10) << cell->buildTime
                      << std::setw(4) << cell->p
                      << std::setw(7) << cell->floatModel
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

    if (wantStalls)
    {
        if (loadOrder.empty())
        {
            std::cerr << "--stall-episodes and --emit-stall-cpp need --units: a build is scored only on a"
                      << " cell the build model explains, and an episode carries its units' FBI values\n";
            return 1;
        }

        auto report = mineStalls(stallDemos, loadOrder, unitFacts, wrongDataSet, minBuilds);
        if (args.contains("stall-episodes"))
        {
            for (const auto& demo : wrongDataSet)
            {
                std::cout << "excluding " << demo << ": its unit table disagrees with --units\n";
            }
            printStalls(report, true);
        }

        if (args.contains("emit-stall-cpp"))
        {
            auto out = args.getString("emit-stall-cpp");
            std::ostringstream generated;
            writeStallEpisodes(generated, report);
            if (!writeGeneratedHeader(out, generated.str()))
            {
                std::cerr << "cannot write " << out << "\n";
                return 1;
            }
        }
    }

    std::vector<WeaponCell> weaponCells;
    if (!pairings.empty())
    {
        weaponCells = mineWeaponCells(pairings, unitFacts, weaponFacts, minPairings);
    }

    if (args.contains("weapon-cells"))
    {
        // The same tables tools/tad-weapontime.py prints, in the same order, so
        // the two can be diffed cell for cell. The script is the reference.
        std::map<std::string, std::vector<const WeaponCell*>> byClass;
        for (const auto& cell : weaponCells)
        {
            byClass[cell.weaponClass].push_back(&cell);
        }

        std::cout << "\n"
                  << pairings.size() << " shots paired, " << weaponCells.size()
                  << " cells with " << minPairings << "+ scoreable pairings\n";

        unsigned int agreeing = 0;
        unsigned int scoredCells = 0;
        unsigned int damageAgreeing = 0;
        for (const auto* className : {"constant speed", "accelerating", "ballistic"})
        {
            auto group = byClass.find(className);
            if (group == byClass.end())
            {
                continue;
            }

            std::vector<const WeaponCell*> sorted(group->second.begin(), group->second.end());
            std::stable_sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
                return a->pairings > b->pairings;
            });

            unsigned int dropped = 0;
            for (const auto* cell : sorted)
            {
                dropped += cell->pairingsDropped;
            }

            std::cout << "\n";
            if (std::string(className) == "constant speed")
            {
                std::cout << "the " << sorted.size() << " cells whose weapon flies at a constant"
                          << " speed, stopped on the victim's\nfootprint, over every victim that can be named ("
                          << dropped << " could not)\n\n";
            }
            else if (std::string(className) == "ballistic")
            {
                std::cout << "the " << sorted.size() << " cells whose weapon lobs a shell, launched"
                          << " at the flat root of TA's own\nfiring solution and stepped horizontally at"
                          << " weaponvelocity/30 * cos(pitch), stopped on\nthe victim's footprint, over the"
                          << " victims that could not outrun a step of it and\nthe shots the weapon's own aim"
                          << " cone could not have moved\n(" << dropped
                          << " pairings dropped by those two bounds or an unnamed victim)\n\n";
            }
            else
            {
                std::cout << "the " << sorted.size() << " cells whose weapon has a motor, flown as"
                          << " RWE flies one and stopped on the\nvictim's footprint, over the victims that"
                          << " could not outrun a step of it\n(" << dropped
                          << " pairings dropped by that bound or an unnamed victim)\n\n";
            }

            std::cout << "  " << std::left << std::setw(13) << "shooter" << " " << std::right << std::setw(2) << "sl"
                      << " " << std::left << std::setw(22) << "weapon" << std::right
                      << std::setw(7) << "v/30" << std::setw(6) << "n" << std::setw(7) << "delta"
                      << std::setw(7) << "share" << std::setw(8) << "damage" << std::setw(7) << "decl" << "\n";

            for (const auto* cell : sorted)
            {
                ++scoredCells;
                agreeing += cell->modeDelta == 0 ? 1 : 0;
                damageAgreeing += cell->modalDamage == cell->damage ? 1 : 0;
                std::cout << "  " << std::left << std::setw(13) << cell->shooter << " "
                          << std::right << std::setw(2) << cell->slot << " "
                          << std::left << std::setw(22) << cell->weapon << std::right
                          << std::setw(7) << std::fixed << std::setprecision(1) << (cell->velocity / 30.0)
                          << std::setw(6) << cell->pairings
                          << std::setw(6) << std::showpos << cell->modeDelta << std::noshowpos
                          // The share is formed and rounded exactly as the
                          // script forms it -- the division first, then a
                          // half-to-even rounding -- so that a cell sitting on
                          // a .5 does not read one point apart in two tables
                          // that are meant to be diffed.
                          << std::setw(6) << std::setprecision(0)
                          << (100.0 * (static_cast<double>(cell->pairingsAtMode) / cell->pairings)) << "%"
                          << std::setw(8) << cell->modalDamage << std::setw(7) << cell->damage
                          << (cell->modeDelta == 0 ? "" : "   <-- disagrees") << "\n";
            }
        }

        std::cout << "\n"
                  << damageAgreeing << " of " << scoredCells
                  << " scored cells carry their weapon's own [DAMAGE] default as the modal damage\n";
        std::cout << "\nthe classes neither model describes, listed and never scored:\n";
        for (const auto& [kind, group] : byClass)
        {
            if (kind == "constant speed" || kind == "accelerating")
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
                  << (scoredCells - agreeing) << " disagreement(s) with the model\n";
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

    if (args.contains("unit-state") || args.contains("emit-unit-state"))
    {
        if (loadOrder.empty())
        {
            std::cerr << "--unit-state needs --units: which serialiser wrote a unit's entry is"
                      << " decided by its type's canfly, and a type index's width by the type count\n";
            return 1;
        }

        std::ofstream json;
        if (args.contains("emit-unit-state"))
        {
            json.open(args.getString("emit-unit-state"));
            if (!json)
            {
                std::cerr << "cannot write " << args.getString("emit-unit-state") << "\n";
                return 1;
            }
        }

        UnitStateTally total;
        for (const auto& path : paths)
        {
            std::ifstream stream(path, std::ios::binary);
            UnitStateHandler handler(
                loadOrder,
                unitFacts,
                json.is_open() ? &json : nullptr,
                args.getBool("with-updates"),
                path.filename().string());
            try
            {
                rwe::readTad(stream, handler);
            }
            catch (const rwe::TadException& e)
            {
                std::cerr << path.filename().string() << ": " << e.what() << "\n";
                return 1;
            }

            if (!handler.layout)
            {
                std::cout << path.filename().string() << ": unit state skipped, its type count "
                          << (handler.unitTable ? handler.unitTable->restricted.size() : 0)
                          << " is not --units' " << loadOrder.size() << "\n";
                continue;
            }

            handler.check();
            std::cout << path.filename().string() << ": map \"" << handler.header->mapName << "\""
                      << ", full-state positions x " << rwe::tadFixedToDouble(handler.minX) << ".."
                      << rwe::tadFixedToDouble(handler.maxX) << ", z " << rwe::tadFixedToDouble(handler.minZ)
                      << ".." << rwe::tadFixedToDouble(handler.maxZ) << "\n";
            printUnitStateTally(handler.tally, "  ");
            total.add(handler.tally);
        }

        std::cout << "unit state, all demos:\n";
        printUnitStateTally(total, "  ");
        if (total.failed > 0)
        {
            return 1;
        }
    }

    return 0;
}
