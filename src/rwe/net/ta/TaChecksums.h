#pragma once

// Total Annihilation's checksum (`0x4B6BA0`), and the two things it is asked
// for: a unit type's content-derived id and a hosted map's checksum.
// docs/TOTALA-EXE-DATA.md §117 is the decode, and tools/exe/mapcrc.py replays it.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rwe
{
    class AbstractVirtualFileSystem;

    /**
     * TA's checksum over a byte range, from its routine at `0x4B6BA0`: four
     * one-byte accumulators walking the input and combined big-endian as
     * `A D B C`. Not a CRC, and not any standard checksum.
     */
    std::uint32_t taChecksum(std::span<const std::uint8_t> bytes);

    /**
     * A unit type's content-derived id, the `a` field of a `0x1a` unit-sync
     * record: the checksum over the raw bytes of its FBI file, with no path,
     * name or canonicalisation in it.
     */
    std::uint32_t taUnitTypeChecksum(std::span<const std::uint8_t> fbiBytes);

    /**
     * The map checksum a host writes at bytes 170-173 of its `0x20` status:
     * the checksum over three `.tnt` blocks (the 64-byte header, the tile
     * attributes and the features) XORed with the checksum over the body of
     * the `.ota`'s `[GlobalHeader]` block. Nothing when either file is not a
     * map, or a block is out of bounds.
     */
    std::optional<std::uint32_t> taMapChecksum(
        std::span<const std::uint8_t> tnt,
        std::span<const std::uint8_t> ota);

    /** The same, reading `maps/<mapName>.tnt` and `.ota` out of the VFS. */
    std::optional<std::uint32_t> taMapChecksum(
        const AbstractVirtualFileSystem& vfs,
        std::string_view mapName);
}
