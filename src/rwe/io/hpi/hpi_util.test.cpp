#include <catch2/catch_test_macros.hpp>
#include <rwe/io/hpi/hpi_util.h>
#include <string>
#include <vector>
#include <zlib.h>

namespace rwe
{
    namespace
    {
        std::vector<char> deflated(const std::string& text)
        {
            std::vector<char> out(compressBound(static_cast<uLong>(text.size())));
            uLongf outLen = static_cast<uLongf>(out.size());
            REQUIRE(compress2(reinterpret_cast<Bytef*>(out.data()), &outLen, reinterpret_cast<const Bytef*>(text.data()), static_cast<uLong>(text.size()), 9) == Z_OK);
            out.resize(outLen);
            return out;
        }

        std::string inflated(const std::vector<char>& in, std::size_t declaredSize)
        {
            std::string out(declaredSize, '\0');
            decompressZLib(in.data(), in.size(), out.data(), declaredSize);
            return out;
        }
    }

    TEST_CASE("decompressZLib", "[hpi]")
    {
        const std::string text = "[GlobalHeader]\n{\nmissionname=A Better Fate;\n}\n";
        auto stream = deflated(text);

        SECTION("a whole stream comes back as written")
        {
            REQUIRE(inflated(stream, text.size()) == text);
        }

        SECTION("a stream with no adler32 trailer still yields its declared bytes")
        {
            // What HPIZ Archiver writes: the deflate blocks are all there and
            // the last one is final, but the four trailer bytes are not.
            // The original reads these, and the V Maps pack is full of them.
            std::vector<char> untrailed(stream.begin(), stream.end() - 4);
            REQUIRE(inflated(untrailed, text.size()) == text);
        }

        SECTION("a stream cut short of its declared bytes is still refused")
        {
            std::vector<char> cut(stream.begin(), stream.end() - 12);
            std::string out(text.size(), '\0');
            REQUIRE_THROWS_AS(decompressZLib(cut.data(), cut.size(), out.data(), text.size()), HpiException);
        }

        SECTION("a stream that would overrun its declared size is refused")
        {
            std::string out(text.size() - 5, '\0');
            REQUIRE_THROWS_AS(decompressZLib(stream.data(), stream.size(), out.data(), out.size()), HpiException);
        }

        SECTION("garbage is refused")
        {
            std::vector<char> junk(40, 'x');
            std::string out(text.size(), '\0');
            REQUIRE_THROWS_AS(decompressZLib(junk.data(), junk.size(), out.data(), text.size()), HpiException);
        }
    }
}
