#include "pcx.h"

namespace rwe
{
    PcxException::PcxException(const char* message) : runtime_error(message) {}

    namespace
    {
        void appendUint16(std::vector<uint8_t>& out, uint16_t v)
        {
            out.push_back(static_cast<uint8_t>(v & 0xFF));
            out.push_back(static_cast<uint8_t>(v >> 8));
        }

        void encodeRun(std::vector<uint8_t>& out, const uint8_t* data, std::size_t length)
        {
            std::size_t i = 0;
            while (i < length)
            {
                auto value = data[i];
                std::size_t count = 1;
                while (i + count < length && data[i + count] == value && count < 63)
                {
                    ++count;
                }
                if (count > 1 || (value & 0xC0) == 0xC0)
                {
                    out.push_back(static_cast<uint8_t>(0xC0 | count));
                }
                out.push_back(value);
                i += count;
            }
        }
    }

    std::vector<uint8_t> encodePcx24(unsigned int width, unsigned int height, const std::vector<uint8_t>& rgb)
    {
        auto bytesPerLine = static_cast<uint16_t>(width + (width % 2));
        std::vector<uint8_t> out;
        out.reserve(128 + (static_cast<std::size_t>(bytesPerLine) * 3 * height));
        out.push_back(10); // manufacturer
        out.push_back(5);  // version
        out.push_back(1);  // encoding: run-length
        out.push_back(8);  // bits per pixel, per plane
        appendUint16(out, 0);
        appendUint16(out, 0);
        appendUint16(out, static_cast<uint16_t>(width - 1));
        appendUint16(out, static_cast<uint16_t>(height - 1));
        appendUint16(out, 72);
        appendUint16(out, 72);
        out.insert(out.end(), 48, 0); // the 16-colour map, unused
        out.push_back(0);             // reserved
        out.push_back(3);             // planes
        appendUint16(out, bytesPerLine);
        appendUint16(out, 1); // palette info: colour
        appendUint16(out, 0);
        appendUint16(out, 0);
        out.insert(out.end(), 54, 0);

        std::vector<uint8_t> plane(bytesPerLine, 0);
        for (unsigned int y = 0; y < height; ++y)
        {
            const auto* row = rgb.data() + (static_cast<std::size_t>(y) * width * 3);
            for (unsigned int c = 0; c < 3; ++c)
            {
                for (unsigned int x = 0; x < width; ++x)
                {
                    plane[x] = row[(x * 3) + c];
                }
                encodeRun(out, plane.data(), plane.size());
            }
        }
        return out;
    }
}
