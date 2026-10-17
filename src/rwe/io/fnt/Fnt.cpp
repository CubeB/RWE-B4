#include "Fnt.h"
#include <rwe/io/io_util.h>
#include <stdexcept>

namespace rwe
{
    FntArchive::FntArchive(std::istream* stream) : _entries()
    {
        _header = readRaw<FntHeader>(*stream);

        // The height is a sixteen-bit field and each of 256 glyph textures is
        // as tall: at 65535 rows that is about 66 MB a glyph. The shipped
        // fonts are 10 to 17 rows high.
        constexpr unsigned int MaxGlyphHeight = 64;
        if (_header.glyphHeight == 0 || _header.glyphHeight > MaxGlyphHeight)
        {
            throw std::runtime_error("font glyph height out of range");
        }

        for (auto& e : _entries)
        {
            e = readRaw<uint16_t>(*stream);
        }

        _stream = stream;
    }

    FntGlyph FntArchive::extractGlyph(unsigned int index, std::vector<char>& buffer)
    {
        buffer.clear();
        auto ptr = _entries.at(index);
        if (ptr == 0)
        {
            return FntGlyph{};
        }

        // A pointer past the end of the file is a glyph that is not there.
        _stream->clear();
        _stream->seekg(ptr);
        char lengthByte = 0;
        _stream->read(&lengthByte, 1);
        if (_stream->fail())
        {
            _stream->clear();
            return FntGlyph{};
        }
        auto width = static_cast<unsigned int>(static_cast<uint8_t>(lengthByte));
        auto bytesWanted = ((width * _header.glyphHeight) + 7u) / 8u;
        buffer.resize(bytesWanted);
        if (bytesWanted > 0)
        {
            _stream->read(buffer.data(), static_cast<std::streamsize>(bytesWanted));
        }
        auto bytesRead = static_cast<unsigned int>(_stream->gcount());
        buffer.resize(bytesRead);

        // The last glyph in a file is often cut short by the end of it, which
        // leaves the stream failed; the next glyph is another seek, so the
        // stream is put right here rather than failing every read after it.
        _stream->clear();
        return FntGlyph{width, bytesRead};
    }

    int FntArchive::verticalOffset() const
    {
        return static_cast<int8_t>(_header.unknown2 & 0xFFu);
    }

    unsigned int FntArchive::glyphHeight() const
    {
        return _header.glyphHeight;
    }
}
