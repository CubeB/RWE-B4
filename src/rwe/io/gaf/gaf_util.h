#pragma once

#include <istream>
#include <rwe/io/io_util.h>
#include <stdexcept>
#include <rwe/io/gaf/GafReaderAdapter.h>
#include <rwe/io/gaf/gaf_headers.h>
#include <vector>

namespace rwe
{
    class GafException : public std::runtime_error
    {
    public:
        explicit GafException(const char* message);
    };

    /**
     * readRaw, with a short read reported as the GAF reader's own exception,
     * so a truncated file costs one sprite sheet and is caught as one.
     */
    template <typename T>
    T readGafRaw(std::istream& stream)
    {
        try
        {
            return readRaw<T>(stream);
        }
        catch (const TruncatedInputError&)
        {
            throw GafException("truncated GAF");
        }
    }

    void decompressRow(std::istream& stream, char* buffer, std::size_t rowLength, char transparencyIndex);

    void decompressFrame(std::istream& stream, char* buffer, std::size_t width, std::size_t height, char transparencyIndex);

    void extractGafEntry(std::istream* _stream, const std::vector<GafFrameEntry>& frameEntries, GafReaderAdapter& adapter);
}
