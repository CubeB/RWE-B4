#include "PosterCapture.h"

#include <algorithm>
#include <cmath>

namespace rwe
{
    unsigned int posterMinimumDimension(float visibleWorldSpan)
    {
        // The original's rule is the visible tile span times sixteen. A tile
        // is sixteen world units here too (MapTerrain::HeightTileWidthInWorldUnits),
        // so the conversion is the same one; round up rather than down so a
        // poster is never narrower than the view it was asked to cover.
        constexpr float TileWorldUnits = 16.0f;
        if (!(visibleWorldSpan > 0.0f) || !std::isfinite(visibleWorldSpan))
        {
            return static_cast<unsigned int>(TileWorldUnits);
        }

        auto tiles = std::ceil(static_cast<double>(visibleWorldSpan) / TileWorldUnits);
        auto rounded = tiles * TileWorldUnits;
        if (rounded < 1.0)
        {
            return 1;
        }
        if (rounded > static_cast<double>(MaxPosterDimension))
        {
            return MaxPosterDimension;
        }
        return static_cast<unsigned int>(rounded);
    }

    PosterSize clampPosterSize(
        unsigned int requestedWidth,
        unsigned int requestedHeight,
        unsigned int mapWidth,
        unsigned int mapHeight,
        unsigned int minimumWidth,
        unsigned int minimumHeight)
    {
        auto clampOne = [](unsigned int requested, unsigned int map, unsigned int minimum) {
            if (map == 0)
            {
                return 0u;
            }
            auto floor = std::min(minimum, map);
            return std::clamp(requested, floor, map);
        };

        return PosterSize{
            clampOne(requestedWidth, mapWidth, minimumWidth),
            clampOne(requestedHeight, mapHeight, minimumHeight),
        };
    }

    bool posterFitsLimits(PosterSize size)
    {
        if (size.width == 0 || size.height == 0)
        {
            return false;
        }
        if (size.width > MaxPosterDimension || size.height > MaxPosterDimension)
        {
            return false;
        }
        return static_cast<unsigned long long>(size.width) * static_cast<unsigned long long>(size.height) <= MaxPosterPixels;
    }

    float clampPosterAxis(float centre, unsigned int size, float mapMinimum, float mapMaximum)
    {
        auto span = mapMaximum - mapMinimum;
        if (!(span > 0.0f))
        {
            return mapMinimum;
        }
        if (static_cast<float>(size) >= span)
        {
            return mapMinimum;
        }

        auto wanted = centre - (static_cast<float>(size) / 2.0f);
        return std::clamp(wanted, mapMinimum, mapMaximum - static_cast<float>(size));
    }

    std::string posterFileName()
    {
        // The reference names the file "a BIGSHOT bitmap"; the exact extension
        // is not in the prose finding. RWE writes its own screenshot format
        // (24-bit PCX, TOTALA-EXE.md section 88), so the stem is kept and the
        // format family is RWE's.
        return "BIGSHOT.pcx";
    }

    namespace
    {
        void appendUint16(std::vector<std::uint8_t>& out, std::uint16_t v)
        {
            out.push_back(static_cast<std::uint8_t>(v & 0xFF));
            out.push_back(static_cast<std::uint8_t>(v >> 8));
        }

        bool writeRun(std::ofstream& out, const std::uint8_t* data, std::size_t length)
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
                    char header = static_cast<char>(0xC0 | count);
                    out.write(&header, 1);
                }
                out.write(reinterpret_cast<const char*>(&value), 1);
                i += count;
            }
            return static_cast<bool>(out);
        }
    }

    PosterPcxWriter::PosterPcxWriter(std::filesystem::path path, unsigned int width, unsigned int height)
        : destination(std::move(path)), width(width)
    {
        if (width == 0 || height == 0 || width > 0xFFFFu || height > 0xFFFFu)
        {
            return;
        }

        auto parent = destination.parent_path();
        if (!parent.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                return;
            }
        }

        auto paddedWidth = width + (width % 2);
        if (paddedWidth > 0xFFFFu)
        {
            return;
        }
        bytesPerLine = paddedWidth;
        plane.resize(bytesPerLine);

        std::vector<std::uint8_t> header;
        header.reserve(128);
        header.push_back(10); // manufacturer
        header.push_back(5);  // version
        header.push_back(1);  // encoding: run-length
        header.push_back(8);  // bits per pixel, per plane
        appendUint16(header, 0);
        appendUint16(header, 0);
        appendUint16(header, static_cast<std::uint16_t>(width - 1));
        appendUint16(header, static_cast<std::uint16_t>(height - 1));
        appendUint16(header, 72);
        appendUint16(header, 72);
        header.insert(header.end(), 48, 0); // the 16-colour map, unused
        header.push_back(0);                // reserved
        header.push_back(3);                // planes
        appendUint16(header, static_cast<std::uint16_t>(bytesPerLine));
        appendUint16(header, 1); // palette info: colour
        appendUint16(header, 0);
        appendUint16(header, 0);
        header.insert(header.end(), 54, 0);

        out.open(destination, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return;
        }
        ownsFile = true;
        out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (!out)
        {
            out.close();
        }
    }

    PosterPcxWriter::~PosterPcxWriter()
    {
        if (!finished)
        {
            removePartial();
        }
    }

    bool PosterPcxWriter::writeRow(const std::uint8_t* rgb)
    {
        if (!out.is_open() || rgb == nullptr)
        {
            return false;
        }

        for (unsigned int channel = 0; channel < 3; ++channel)
        {
            std::fill(plane.begin(), plane.end(), 0);
            for (unsigned int x = 0; x < width; ++x)
            {
                plane[x] = rgb[(static_cast<std::size_t>(x) * 3) + channel];
            }
            if (!writeRun(out, plane.data(), plane.size()))
            {
                return false;
            }
        }
        return true;
    }

    bool PosterPcxWriter::finish()
    {
        if (!out.is_open())
        {
            return false;
        }
        out.flush();
        finished = out.good();
        out.close();
        return finished;
    }

    void PosterPcxWriter::removePartial()
    {
        if (out.is_open())
        {
            out.close();
        }
        if (ownsFile)
        {
            std::error_code ec;
            std::filesystem::remove(destination, ec);
            ownsFile = false;
        }
        finished = false;
    }
}
