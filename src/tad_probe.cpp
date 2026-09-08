// Reads Total Annihilation demo files (.tad / .ted) and prints what is in them,
// so the container reader and the three transforms can be checked against a real
// corpus rather than against the reference implementation's own assumptions.
//
// The number this exists to produce is the desync count: subpacket codes the
// length table cannot size, subpackets that run off the end of their packet, and
// compressed payloads that run out of bytes. A clean run over a corpus is what
// item 1 of docs/TA-DEMOS.md asks for; anything else is a finding, and the codes
// it names are the short list to go and look up in TotalA.exe.
//
// Truly offline: no SDL, no GL, no VFS, just files.
//
// Usage: tad_probe --file <path> [--file <path>...] [--dir <path>] [--verbose]
//   --file     a demo to read; may be repeated
//   --dir      a directory of demos to read; recurses
//   --verbose  print every packet, not just the summary
//   --dump-unknown  show the subpackets the length table could not size
//
// Exits non-zero if any demo failed to parse or walked out of step.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_util.h>
#include <rwe/util/OpaqueArgs.h>
#include <sstream>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        std::string toPrintable(const TadBytes& data, std::size_t limit = 64)
        {
            std::string result;
            for (std::size_t i = 0; i < data.size() && result.size() < limit; ++i)
            {
                auto c = data[i];
                result.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '.');
            }
            return result;
        }

        std::string toHex(const TadBytes& data, std::size_t limit = 24)
        {
            std::ostringstream ss;
            ss << std::hex << std::setfill('0');
            for (std::size_t i = 0; i < data.size() && i < limit; ++i)
            {
                ss << std::setw(2) << unsigned(data[i]) << ' ';
            }
            if (data.size() > limit)
            {
                ss << "...";
            }
            return ss.str();
        }

        /**
         * A subpacket the walker could not size, with enough around it to tell a
         * real unknown code from a walk that had already lost its place.
         */
        struct UnknownSample
        {
            uint8_t code;
            uint8_t previousCode;
            std::size_t size;
            std::size_t position;
            std::size_t ofTotal;
            TadBytes preview;
            std::size_t previousSize;
            TadBytes previousTail;
            std::string codes;
        };

        std::string describeSector(uint32_t type)
        {
            switch (static_cast<TadExtraSectorType>(type))
            {
                case TadExtraSectorType::Comments:
                    return "comments";
                case TadExtraSectorType::Chat:
                    return "chat";
                case TadExtraSectorType::RecorderVersion:
                    return "recorder";
                case TadExtraSectorType::Date:
                    return "date";
                case TadExtraSectorType::RecorderContext:
                    return "context";
                case TadExtraSectorType::PlayerAddr:
                    return "addresses";
                case TadExtraSectorType::ModId:
                    return "mod";
                default:
                    return "type " + std::to_string(type);
            }
        }

        std::string describeSide(int8_t side)
        {
            switch (static_cast<TadSide>(side))
            {
                case TadSide::Arm:
                    return "ARM";
                case TadSide::Core:
                    return "CORE";
                case TadSide::Watch:
                    return "WATCH";
                default:
                    return "side " + std::to_string(side);
            }
        }

        struct ProbeHandler : TadHandler
        {
            bool verbose;
            bool dumpUnknown;

            std::optional<TadHeader> header;
            std::vector<TadPlayer> players;
            std::map<uint32_t, std::string> sectors;

            unsigned int packetCount = 0;
            unsigned int subPacketCount = 0;
            uint64_t wallClockMs = 0;
            std::map<uint8_t, unsigned int> codeHistogram;

            /**
             * The tick clock, tracked per sender. It is the serial inside 0x2c
             * and not Packet::time, which is wall clock contaminated by jitter,
             * by the 0x19 speed setting and by pauses.
             */
            std::map<uint8_t, uint32_t> firstTick;
            std::map<uint8_t, uint32_t> lastTick;
            unsigned int tickRegressions = 0;

            TadWalkStats stats;

            /** Counted by leading byte, so the tail of the run is not lost. */
            std::map<uint8_t, unsigned int> unknownHistogram;
            std::vector<UnknownSample> unknownSamples;

            ProbeHandler(bool verbose, bool dumpUnknown) : verbose(verbose), dumpUnknown(dumpUnknown) {}

            void onHeader(const TadHeader& h) override { header = h; }

            void onExtraSector(const TadExtraSector& s, unsigned int, unsigned int) override
            {
                sectors[s.type] = toPrintable(s.data);
            }

            void onPlayer(const TadPlayer& p, unsigned int, unsigned int) override
            {
                players.push_back(p);
            }

            void onPlayerStatus(const TadPlayerStatus& s, unsigned int, unsigned int) override
            {
                // The id is what a reject (0x1b) refers to, and the message
                // length is worth seeing: the lobby status packet is the long
                // form of the 0x20 the stream carries in its short form.
                std::cout << "  status player " << unsigned(s.number)
                          << " dplayId 0x" << std::hex << (s.dplayId ? *s.dplayId : 0) << std::dec
                          << ", " << s.statusMessage.size() << " bytes"
                          << (s.checksum.matches() ? "" : " CHECKSUM MISMATCH") << "\n";
            }

            void onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets, const TadWalkStats& s) override
            {
                ++packetCount;
                wallClockMs += packet.time;

                stats.unknownCodes += s.unknownCodes;
                stats.truncated += s.truncated;
                stats.failedDecompressions += s.failedDecompressions;

                for (std::size_t i = 0; i < subPackets.size(); ++i)
                {
                    const auto& subPacket = subPackets[i];
                    if (subPacket.empty())
                    {
                        continue;
                    }

                    ++subPacketCount;
                    ++codeHistogram[subPacket[0]];

                    if (s.total() > 0 && tadExpectedSubPacketSize(subPacket.data(), subPacket.size()) != subPacket.size())
                    {
                        // This is the one the walker could not size: it was
                        // handed the rest of the packet rather than its own
                        // length.
                        ++unknownHistogram[subPacket[0]];
                        if (dumpUnknown && unknownSamples.size() < 20)
                        {
                            unknownSamples.push_back(UnknownSample{
                                subPacket[0],
                                i > 0 && !subPackets[i - 1].empty() ? subPackets[i - 1][0] : uint8_t{0},
                                subPacket.size(),
                                i,
                                subPackets.size(),
                                TadBytes(subPacket.begin(), subPacket.begin() + std::min<std::size_t>(subPacket.size(), 24)),
                                i > 0 ? subPackets[i - 1].size() : 0,
                                i > 0 && subPackets[i - 1].size() >= 16
                                    ? TadBytes(subPackets[i - 1].end() - 16, subPackets[i - 1].end())
                                    : TadBytes(),
                                [&] {
                                    std::ostringstream ss;
                                    for (const auto& sp : subPackets)
                                    {
                                        if (!sp.empty())
                                        {
                                            ss << std::hex << unsigned(sp[0]) << "/" << std::dec << sp.size() << " ";
                                        }
                                    }
                                    return ss.str();
                                }()});
                        }
                    }

                    if (static_cast<TadSubPacketCode>(subPacket[0]) == TadSubPacketCode::UnitStatAndMove
                        && subPacket.size() >= 7)
                    {
                        uint32_t tick = static_cast<uint32_t>(subPacket[3])
                            | (static_cast<uint32_t>(subPacket[4]) << 8)
                            | (static_cast<uint32_t>(subPacket[5]) << 16)
                            | (static_cast<uint32_t>(subPacket[6]) << 24);

                        auto it = lastTick.find(packet.sender);
                        if (it == lastTick.end())
                        {
                            firstTick[packet.sender] = tick;
                        }
                        else if (tick < it->second)
                        {
                            ++tickRegressions;
                        }
                        lastTick[packet.sender] = tick;
                    }
                }

                if (verbose)
                {
                    std::cout << "  packet " << packetCount
                              << " +" << packet.time << "ms"
                              << " from " << unsigned(packet.sender)
                              << " -> " << subPackets.size() << " subpackets";
                    if (s.total() > 0)
                    {
                        std::cout << " DESYNC";
                    }
                    std::cout << "\n";
                }
            }
        };

        void report(const ProbeHandler& handler)
        {
            const auto& header = *handler.header;

            std::cout << "  version " << header.version
                      << ", map \"" << header.mapName << "\""
                      << ", maxUnits " << header.maxUnits << "\n";

            for (const auto& [type, text] : handler.sectors)
            {
                std::cout << "  " << describeSector(type) << ": " << text << "\n";
            }

            for (const auto& player : handler.players)
            {
                std::cout << "  player " << unsigned(player.number)
                          << " " << describeSide(player.side)
                          << " colour " << unsigned(player.color)
                          << " \"" << player.name << "\"\n";
            }

            std::cout << "  packets " << handler.packetCount
                      << ", subpackets " << handler.subPacketCount
                      << ", wall clock " << (handler.wallClockMs / 1000) << "s\n";

            for (const auto& [sender, last] : handler.lastTick)
            {
                auto first = handler.firstTick.at(sender);
                std::cout << "  ticks from " << unsigned(sender) << ": " << first << " to " << last
                          << " (" << (last - first) << ")\n";
            }

            if (!handler.unknownHistogram.empty())
            {
                std::cout << "  codes the table could not size:";
                for (const auto& [code, count] : handler.unknownHistogram)
                {
                    std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0')
                              << unsigned(code) << std::dec << "x" << count;
                }
                std::cout << "\n";
            }

            for (const auto& sample : handler.unknownSamples)
            {
                std::cout << "    unsized 0x" << std::hex << std::setw(2) << std::setfill('0')
                          << unsigned(sample.code) << std::dec
                          << " after 0x" << std::hex << std::setw(2) << unsigned(sample.previousCode) << std::dec
                          << " at " << sample.position << "/" << sample.ofTotal
                          << ", " << sample.size << " bytes: " << toHex(sample.preview) << "\n"
                          << "      packet was: " << sample.codes << "\n"
                          << "      tail of previous (" << sample.previousSize << " bytes): " << toHex(sample.previousTail) << "\n";
            }

            std::cout << "  subpacket codes:";
            for (const auto& [code, count] : handler.codeHistogram)
            {
                std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0')
                          << unsigned(code) << std::dec << "x" << count;
            }
            std::cout << "\n";
        }

        struct Totals
        {
            std::map<uint8_t, unsigned int> unknownHistogram;
            unsigned int files = 0;
            unsigned int failed = 0;
            unsigned int desynced = 0;
            unsigned int packets = 0;
            unsigned int subPackets = 0;
            TadWalkStats stats;
            unsigned int tickRegressions = 0;
            std::map<uint8_t, unsigned int> codeHistogram;
        };

        void probe(const std::filesystem::path& path, bool verbose, bool dumpUnknown, Totals& totals)
        {
            ++totals.files;
            std::cout << path.filename().string() << "\n";

            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                std::cout << "  ERROR: cannot open\n";
                ++totals.failed;
                return;
            }

            ProbeHandler handler(verbose, dumpUnknown);
            try
            {
                readTad(stream, handler);
            }
            catch (const TadException& e)
            {
                std::cout << "  ERROR: " << e.what() << "\n";
                ++totals.failed;
                return;
            }

            report(handler);

            totals.packets += handler.packetCount;
            totals.subPackets += handler.subPacketCount;
            totals.stats.unknownCodes += handler.stats.unknownCodes;
            totals.stats.truncated += handler.stats.truncated;
            totals.stats.failedDecompressions += handler.stats.failedDecompressions;
            totals.tickRegressions += handler.tickRegressions;

            for (const auto& [code, count] : handler.codeHistogram)
            {
                totals.codeHistogram[code] += count;
            }

            for (const auto& [code, count] : handler.unknownHistogram)
            {
                totals.unknownHistogram[code] += count;
            }

            if (handler.stats.total() > 0 || handler.tickRegressions > 0)
            {
                ++totals.desynced;
                std::cout << "  DESYNC:"
                          << " unknown codes " << handler.stats.unknownCodes
                          << ", truncated " << handler.stats.truncated
                          << ", failed decompressions " << handler.stats.failedDecompressions
                          << ", tick regressions " << handler.tickRegressions << "\n";
            }
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
        std::cerr << "usage: tad_probe --file <path> [--dir <path>] [--verbose]\n";
        return 1;
    }

    if (args.isHelpRequested() || (!args.contains("file") && !args.contains("dir")))
    {
        std::cout << "usage: tad_probe --file <path> [--file <path>...] [--dir <path>] [--verbose]\n"
                  << "\n"
                  << "  --file     a .tad or .ted demo to read; may be repeated\n"
                  << "  --dir      a directory of demos to read; recurses\n"
                  << "  --verbose  print every packet, not just the summary\n"
                  << "  --dump-unknown  show the subpackets the length table could not size\n"
                  << "\n"
                  << "Exits non-zero if any demo failed to parse or walked out of step.\n";
        return args.isHelpRequested() ? 0 : 1;
    }

    auto verbose = args.getBool("verbose");
    auto dumpUnknown = args.getBool("dump-unknown");

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
            if (entry.is_regular_file() && rwe::isDemo(entry.path()))
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

    rwe::Totals totals;
    for (const auto& path : paths)
    {
        rwe::probe(path, verbose, dumpUnknown, totals);
    }

    std::cout << "\n"
              << totals.files << " files, " << totals.failed << " failed to parse, "
              << totals.desynced << " walked out of step\n"
              << totals.packets << " packets, " << totals.subPackets << " subpackets\n"
              << "unknown codes " << totals.stats.unknownCodes
              << ", truncated " << totals.stats.truncated
              << ", failed decompressions " << totals.stats.failedDecompressions
              << ", tick regressions " << totals.tickRegressions << "\n";

    if (!totals.unknownHistogram.empty())
    {
        std::cout << "codes the table could not size:";
        for (const auto& [code, count] : totals.unknownHistogram)
        {
            std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0')
                      << unsigned(code) << std::dec << "x" << count;
        }
        std::cout << "\n";
    }

    if (totals.files > 1)
    {
        std::cout << "subpacket codes seen:";
        for (const auto& [code, count] : totals.codeHistogram)
        {
            std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0')
                      << unsigned(code) << std::dec << "x" << count;
        }
        std::cout << "\n";
    }

    return (totals.failed > 0 || totals.desynced > 0) ? 1 : 0;
}
