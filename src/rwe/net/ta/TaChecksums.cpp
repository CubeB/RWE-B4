#include "TaChecksums.h"

#include <cstddef>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        constexpr std::uint32_t TntMagic = 0x2000;
        constexpr std::size_t TntHeaderSize = 0x40;

        // As TntArchive: a map is a download, so every size below sizes a read.
        constexpr std::uint32_t TntMaxDimension = 8192;
        constexpr std::uint32_t TntMaxCount = 65536;
        constexpr std::size_t TntFeatureSize = 132;

        std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset)
        {
            return static_cast<std::uint32_t>(bytes[offset])
                | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
                | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
                | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
        }

        bool isWhitespace(std::uint8_t c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        bool equalsIgnoreCase(std::span<const std::uint8_t> text, std::string_view name)
        {
            if (text.size() < name.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < name.size(); ++i)
            {
                auto a = text[i];
                auto b = static_cast<std::uint8_t>(name[i]);
                if (a >= 'A' && a <= 'Z')
                {
                    a = static_cast<std::uint8_t>(a + ('a' - 'A'));
                }
                if (b >= 'A' && b <= 'Z')
                {
                    b = static_cast<std::uint8_t>(b + ('a' - 'A'));
                }
                if (a != b)
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * The checksum the TDF reader stores against the `[GlobalHeader]`
         * block: `0x4C4183` hashes `[bodyStart, close - 2)`, the two bytes
         * before the closing brace always dropped. The whole body, nested
         * schemas included.
         */
        std::optional<std::uint32_t> globalHeaderChecksum(std::span<const std::uint8_t> ota)
        {
            constexpr std::string_view Name = "GlobalHeader";

            std::size_t bodyStart = 0;
            bool found = false;
            for (std::size_t i = 0; i + Name.size() + 2 <= ota.size(); ++i)
            {
                if (ota[i] != '[' || !equalsIgnoreCase(ota.subspan(i + 1, Name.size()), Name)
                    || ota[i + Name.size() + 1] != ']')
                {
                    continue;
                }
                std::size_t brace = i + Name.size() + 2;
                while (brace < ota.size() && isWhitespace(ota[brace]))
                {
                    ++brace;
                }
                if (brace >= ota.size() || ota[brace] != '{')
                {
                    return std::nullopt;
                }
                bodyStart = brace + 1;
                found = true;
                break;
            }
            if (!found)
            {
                return std::nullopt;
            }

            std::size_t depth = 0;
            std::optional<std::size_t> close;
            for (std::size_t i = bodyStart; i < ota.size(); ++i)
            {
                if (ota[i] == '{')
                {
                    ++depth;
                }
                else if (ota[i] == '}')
                {
                    if (depth == 0)
                    {
                        close = i;
                        break;
                    }
                    --depth;
                }
            }
            if (!close)
            {
                return std::nullopt;
            }

            auto bodyLength = *close - bodyStart;
            auto hashLength = bodyLength >= 2 ? bodyLength - 2 : std::size_t{0};
            return taChecksum(ota.subspan(bodyStart, hashLength));
        }
    }

    std::uint32_t taChecksum(std::span<const std::uint8_t> bytes)
    {
        std::uint8_t a = 0;
        std::uint8_t b = 0;
        std::uint8_t c = 0;
        std::uint8_t d = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            auto index = static_cast<std::uint8_t>(i & 0xFF);
            auto byte = bytes[i];
            a ^= static_cast<std::uint8_t>(index + byte);
            b ^= byte;
            c += byte;
            d += static_cast<std::uint8_t>(index ^ byte);
        }
        return (static_cast<std::uint32_t>(a) << 24)
            | (static_cast<std::uint32_t>(d) << 16)
            | (static_cast<std::uint32_t>(b) << 8)
            | static_cast<std::uint32_t>(c);
    }

    std::uint32_t taUnitTypeChecksum(std::span<const std::uint8_t> fbiBytes)
    {
        return taChecksum(fbiBytes);
    }

    std::optional<std::uint32_t> taMapChecksum(
        std::span<const std::uint8_t> tnt,
        std::span<const std::uint8_t> ota)
    {
        if (tnt.size() < TntHeaderSize || readU32(tnt, 0) != TntMagic)
        {
            return std::nullopt;
        }

        auto width = readU32(tnt, 4);
        auto height = readU32(tnt, 8);
        auto attributesOffset = readU32(tnt, 0x10);
        auto numberOfFeatures = readU32(tnt, 0x1C);
        auto featuresOffset = readU32(tnt, 0x20);

        if (width == 0 || height == 0 || width > TntMaxDimension || height > TntMaxDimension
            || numberOfFeatures > TntMaxCount)
        {
            return std::nullopt;
        }

        auto attributesLength = static_cast<std::uint64_t>(width) * height * 4;
        if (attributesOffset > tnt.size() || attributesLength > tnt.size() - attributesOffset)
        {
            return std::nullopt;
        }
        auto featuresLength = static_cast<std::uint64_t>(numberOfFeatures) * TntFeatureSize;
        if (featuresOffset > tnt.size() || featuresLength > tnt.size() - featuresOffset)
        {
            return std::nullopt;
        }

        auto tntChecksum = taChecksum(tnt.subspan(0, TntHeaderSize))
            ^ taChecksum(tnt.subspan(attributesOffset, static_cast<std::size_t>(attributesLength)))
            ^ taChecksum(tnt.subspan(featuresOffset, static_cast<std::size_t>(featuresLength)));

        auto otaChecksum = globalHeaderChecksum(ota);
        if (!otaChecksum)
        {
            return std::nullopt;
        }
        return tntChecksum ^ *otaChecksum;
    }

    std::optional<std::uint32_t> taMapChecksum(
        const AbstractVirtualFileSystem& vfs,
        std::string_view mapName)
    {
        auto path = std::string("maps/").append(mapName);
        auto tnt = vfs.readFile(path + ".tnt");
        auto ota = vfs.readFile(path + ".ota");
        if (!tnt || !ota)
        {
            return std::nullopt;
        }

        auto asBytes = [](const std::vector<char>& bytes) {
            return std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(bytes.data()),
                bytes.size());
        };
        return taMapChecksum(asBytes(*tnt), asBytes(*ota));
    }
}
