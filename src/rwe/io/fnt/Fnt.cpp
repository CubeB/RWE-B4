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

    unsigned int FntArchive::extract(unsigned int index, char* buffer)
    {
        auto ptr = _entries.at(index);
        if (ptr == 0)
        {
            return 0;
        }

        // A pointer past the end of the file is a glyph that is not there.
        _stream->clear();
        _stream->seekg(ptr);
        char lengthByte = 0;
        _stream->read(&lengthByte, 1);
        if (_stream->fail())
        {
            _stream->clear();
            return 0;
        }
        auto length = static_cast<uint8_t>(lengthByte);
        _stream->read(buffer, length * 2);
        auto bytesRead = _stream->gcount();

        // The last glyph in a file is often cut short by the end of it, which
        // leaves the stream failed; the next glyph is another seek, so the
        // stream is put right here rather than failing every read after it.
        _stream->clear();
        return bytesRead;
    }

    unsigned int FntArchive::glyphHeight() const
    {
        return _header.glyphHeight;
    }
}
