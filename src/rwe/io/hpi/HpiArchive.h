#pragma once

#include <cstdint>
#include <functional>
#include <istream>
#include <optional>
#include <rwe/io/hpi/hpi_headers.h>
#include <variant>
#include <vector>

namespace rwe
{
    class HpiArchive
    {
    public:
        /** The largest directory block an archive may declare; see the constructor. Issue #75. */
        static constexpr std::uint32_t MaxDirectoryBytes = 64u * 1024u * 1024u;

        /**
         * The largest file an archive may declare. The size is taken from the
         * archive and allocated before a byte is read, so a few bytes of
         * directory could ask for four gigabytes. TA's largest files, its
         * films, are tens of megabytes. Issue #75.
         */
        static constexpr std::uint32_t MaxFileBytes = 512u * 1024u * 1024u;

        struct DirectoryEntry;
        struct File
        {
            enum class CompressionScheme
            {
                None = 0,
                LZ77,
                ZLib
            };
            CompressionScheme compressionScheme;
            std::size_t offset;
            std::size_t size;
        };
        struct Directory
        {
            std::vector<DirectoryEntry> entries;
        };
        struct DirectoryEntry
        {
            std::string name;
            std::variant<File, Directory> data;
        };

    private:
        std::istream* stream;
        unsigned char decryptionKey;
        Directory _root;

    public:
        explicit HpiArchive(std::istream* stream);

        const Directory& root() const;

        std::optional<std::reference_wrapper<const File>> findFile(const std::string& path) const;

        std::optional<std::reference_wrapper<const Directory>> findDirectory(const std::string& path) const;

        void extract(const File& file, char* buffer) const;
    };
}
