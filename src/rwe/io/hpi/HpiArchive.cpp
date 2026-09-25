#include "HpiArchive.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <rwe/io/hpi/hpi_util.h>
#include <rwe/io/io_util.h>
#include <rwe/util/match.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    /**
     * How much of a directory tree may still be walked. Every entry in a
     * genuine archive is a distinct record in the directory block, so there
     * can be no more of them than the block has room for; a count past that
     * means entries that point back at their own ancestors, which used to
     * recurse until the stack ran out. The depth limit is the same guard for
     * a chain that is merely very long. Issue #75.
     */
    struct DirectoryWalkBudget
    {
        std::size_t entriesLeft;
        unsigned int depth{0};
    };

    constexpr unsigned int MaxDirectoryDepth = 128;

    HpiArchive::DirectoryEntry
    convertDirectoryEntry(const HpiDirectoryEntry& entry, const char* buffer, std::size_t size, DirectoryWalkBudget& budget);

    HpiArchive::File convertFile(const HpiFileData& file)
    {
        HpiArchive::File f{static_cast<HpiArchive::File::CompressionScheme>(file.compressionScheme), file.dataOffset, file.fileSize};
        return f;
    }

    HpiArchive::Directory
    convertDirectory(const HpiDirectoryData& directory, const char* buffer, std::size_t size, DirectoryWalkBudget& budget)
    {
        if (directory.entryListOffset + (directory.numberOfEntries * sizeof(HpiDirectoryEntry)) > size)
        {
            throw HpiException("Runaway directory entry list");
        }
        if (directory.numberOfEntries > budget.entriesLeft)
        {
            throw HpiException("Directory tree has more entries than the archive holds");
        }
        budget.entriesLeft -= directory.numberOfEntries;
        if (++budget.depth > MaxDirectoryDepth)
        {
            throw HpiException("Directory tree nested too deep");
        }

        std::vector<HpiArchive::DirectoryEntry> v;
        auto p = reinterpret_cast<const HpiDirectoryEntry*>(buffer + directory.entryListOffset);
        for (std::size_t i = 0; i < directory.numberOfEntries; ++i)
        {
            v.push_back(convertDirectoryEntry(p[i], buffer, size, budget));
        }

        --budget.depth;
        return HpiArchive::Directory{v};
    }

    HpiArchive::DirectoryEntry
    convertDirectoryEntry(const HpiDirectoryEntry& entry, const char* buffer, std::size_t size, DirectoryWalkBudget& budget)
    {
        // Before the scan for the terminator, which walks forward until it
        // meets the end: started past the end, it never would.
        if (entry.nameOffset >= size)
        {
            throw HpiException("Runaway directory entry name");
        }
        auto nameSize = stringSize(buffer + entry.nameOffset, buffer + size);
        if (!nameSize)
        {
            throw HpiException("Runaway directory entry name");
        }

        std::string name(buffer + entry.nameOffset, *nameSize);
        if (entry.isDirectory != 0)
        {
            if (entry.dataOffset + sizeof(HpiDirectoryData) > size)
            {
                throw HpiException("Runaway directory data offset");
            }

            auto d = reinterpret_cast<const HpiDirectoryData*>(buffer + entry.dataOffset);
            auto data = convertDirectory(*d, buffer, size, budget);
            return HpiArchive::DirectoryEntry{name, data};
        }
        else
        {
            if (entry.dataOffset + sizeof(HpiFileData) > size)
            {
                throw HpiException("Runaway file data offset");
            }

            auto f = reinterpret_cast<const HpiFileData*>(buffer + entry.dataOffset);
            auto data = convertFile(*f);
            return HpiArchive::DirectoryEntry{name, data};
        }
    }

    HpiArchive::HpiArchive(std::istream* stream) : stream(stream)
    {
        auto v = readRaw<HpiVersion>(*stream);
        if (v.marker != HpiMagicNumber)
        {
            throw HpiException("Invalid HPI file marker");
        }

        if (v.version != HpiVersionNumber)
        {
            throw HpiException("Unsupported HPI version");
        }

        auto h = readRaw<HpiHeader>(*stream);

        decryptionKey = transformKey(static_cast<unsigned char>(h.headerKey));

        // Both checked before the read, not after it. The read fills from
        // `start` to `directorySize`, and with `start` past the end that
        // length wrapped round to four billion: the archive's own bytes were
        // written past the buffer before anything looked at them. The
        // directory block of the largest shipped archive is well under a
        // megabyte. Issue #75.
        if (h.directorySize > MaxDirectoryBytes)
        {
            throw HpiException("Directory block too large");
        }
        if (static_cast<std::size_t>(h.start) + sizeof(HpiDirectoryData) > h.directorySize)
        {
            throw HpiException("Runaway root directory");
        }

        stream->seekg(h.start);
        auto data = std::make_unique<char[]>(h.directorySize);
        readAndDecrypt(*stream, decryptionKey, data.get() + h.start, h.directorySize - h.start);

        auto directory = reinterpret_cast<HpiDirectoryData*>(data.get() + h.start);
        DirectoryWalkBudget budget{h.directorySize / sizeof(HpiDirectoryEntry)};
        _root = convertDirectory(*directory, data.get(), h.directorySize, budget);
    }

    const HpiArchive::Directory& HpiArchive::root() const
    {
        return _root;
    }

    void HpiArchive::extract(const HpiArchive::File& file, char* buffer) const
    {
        stream->seekg(file.offset);
        switch (file.compressionScheme)
        {
            case HpiArchive::File::CompressionScheme::None:
                readAndDecrypt(*stream, decryptionKey, buffer, file.size);
                break;
            case HpiArchive::File::CompressionScheme::LZ77:
            case HpiArchive::File::CompressionScheme::ZLib:
                extractCompressed(*stream, decryptionKey, buffer, file.size);
                break;
            default:
                throw HpiException("Invalid file entry compression scheme");
        }
    }

    std::optional<std::reference_wrapper<const HpiArchive::File>> findFileInner(const HpiArchive::Directory& dir, const std::string& name)
    {
        auto it = std::find_if(
            dir.entries.begin(),
            dir.entries.end(),
            [name](const HpiArchive::DirectoryEntry& e) {
                return toUpper(e.name) == toUpper(name);
            });

        if (it == dir.entries.end())
        {
            return std::nullopt;
        }

        return match(
            it->data,
            [&](const HpiArchive::Directory&) { return std::optional<std::reference_wrapper<const HpiArchive::File>>(); },
            [&](const HpiArchive::File& f) { return std::make_optional(std::ref(f)); });
    }

    std::optional<std::reference_wrapper<const HpiArchive::File>> HpiArchive::findFile(const std::string& path) const
    {
        auto components = split(path, '/');

        const Directory* dir = &root();

        for (auto cIt = components.cbegin(), cEnd = --components.cend(); cIt != cEnd; ++cIt)
        {
            auto& c = *cIt;
            auto begin = dir->entries.begin();
            auto end = dir->entries.end();
            auto it = std::find_if(
                begin,
                end,
                [c](const DirectoryEntry& e) {
                    return toUpper(e.name) == toUpper(c);
                });
            if (it == end)
            {
                return std::nullopt;
            }

            auto foundDir = std::get_if<Directory>(&it->data);
            if (!foundDir)
            {
                return std::nullopt;
            }

            dir = foundDir;
        }

        return findFileInner(*dir, components.back());
    }

    std::optional<std::reference_wrapper<const HpiArchive::Directory>> HpiArchive::findDirectory(const std::string& path) const
    {
        auto components = split(path, '/');

        const Directory* dir = &root();

        for (const auto& c : components)
        {
            auto begin = dir->entries.begin();
            auto end = dir->entries.end();
            auto it = std::find_if(
                begin,
                end,
                [c](const DirectoryEntry& e) {
                    return toUpper(e.name) == toUpper(c);
                });
            if (it == end)
            {
                return std::nullopt;
            }

            auto foundDir = std::get_if<Directory>(&it->data);
            if (!foundDir)
            {
                return std::nullopt;
            }

            dir = foundDir;
        }

        return *dir;
    }
}
