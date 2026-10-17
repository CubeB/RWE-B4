#include "GafArchive.h"
#include <algorithm>
#include <memory>
#include <rwe/io/gaf/gaf_util.h>
#include <rwe/io/io_util.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    namespace
    {
        /**
         * Reads one entry and its frame table. `framesLeft` is how many more
         * frame records the whole file can hold; an entry that claims more is
         * refused, so a file's pointers cannot be made to name the same big
         * entry over and over to build gigabytes of tables from kilobytes.
         */
        GafArchive::Entry readEntry(std::istream& stream, std::uint64_t pointer, std::uint64_t fileSize, std::uint64_t& framesLeft)
        {
            if (pointer + sizeof(GafEntry) > fileSize)
            {
                throw GafException("GAF entry lies past the end of the file");
            }

            stream.seekg(static_cast<std::streamoff>(pointer));
            auto entry = readGafRaw<GafEntry>(stream);

            if (entry.frames > framesLeft)
            {
                throw GafException("GAF entries claim more frames than the file can hold");
            }
            framesLeft -= entry.frames;
            if (pointer + sizeof(GafEntry) + static_cast<std::uint64_t>(entry.frames) * sizeof(GafFrameEntry) > fileSize)
            {
                throw GafException("GAF frame table lies past the end of the file");
            }

            auto nullPos = std::find(entry.name, entry.name + GafMaxNameLength, '\0');
            auto nameLength = nullPos - entry.name;
            std::string name(reinterpret_cast<char*>(entry.name), nameLength);

            std::vector<GafFrameEntry> frames;
            frames.reserve(entry.frames);

            for (std::size_t i = 0; i < entry.frames; ++i)
            {
                frames.emplace_back(readGafRaw<GafFrameEntry>(stream));
            }

            return GafArchive::Entry{std::move(name), entry.unknown1, entry.unknown2, std::move(frames)};
        }
    }

    const std::vector<GafArchive::Entry>& GafArchive::entries() const
    {
        return _entries;
    }

    GafArchive::GafArchive(std::istream* stream)
    {
        auto fileSize = streamSize(*stream);
        auto header = readGafRaw<GafHeader>(*stream);

        // No version test: the original has none, and five shipped files
        // carry version 0 -- anims/vismasks.gaf and anims/terrain.gaf.
        // BT-155, docs/BYTE-TACTICS-SPEC-ROOM-FINDINGS.md on branch
        // spec-room-findings; static byte-tactics reading at 97d7ac59.

        // Reserved before any entry is read, so a twelve-byte file could ask
        // for four billion. TA's largest GAFs hold a few hundred. Issue #75.
        constexpr uint32_t MaxEntries = 65536;
        if (header.entries > MaxEntries)
        {
            throw GafException("GAF entry count out of range");
        }

        // Each entry has a four-byte pointer in the file, so a count the file
        // has no room for is a lie.
        if (sizeof(GafHeader) + static_cast<std::uint64_t>(header.entries) * sizeof(uint32_t) > fileSize)
        {
            throw GafException("GAF entry count does not fit in the file");
        }
        _entries.reserve(header.entries);

        // Every frame has a record of its own in the file, so the frames of
        // all the entries together cannot outnumber the records the file can
        // hold. The per-entry limit (65535 frames) alone lets a few hundred
        // kilobytes of identical pointers build tens of gigabytes of tables.
        std::uint64_t framesLeft = fileSize / sizeof(GafFrameEntry);

        for (std::size_t i = 0; i < header.entries; ++i)
        {
            auto pointer = readGafRaw<uint32_t>(*stream);
            auto previous = stream->tellg();

            _entries.push_back(readEntry(*stream, pointer, fileSize, framesLeft));
            stream->seekg(previous);
        }

        _stream = stream;
    }

    std::optional<std::reference_wrapper<const GafArchive::Entry>> GafArchive::findEntry(const std::string& name) const
    {
        auto pos = std::find_if(_entries.begin(), _entries.end(), [&name](const Entry& e) { return toUpper(e.name) == toUpper(name); });

        if (pos == _entries.end())
        {
            return std::nullopt;
        }

        return *pos;
    }

    void GafArchive::extract(const GafArchive::Entry& entry, GafReaderAdapter& adapter)
    {
        extractGafEntry(_stream, entry.frameOffsets, adapter);
    }
}
