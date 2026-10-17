#include "MapIdentity.h"
#include <cstdint>
#include <rwe/vfs/AbstractVirtualFileSystem.h>

namespace rwe
{
    namespace
    {
        constexpr std::size_t MaxIdentityLength = 48;

        std::string hex(std::uint64_t value)
        {
            static const char digits[] = "0123456789abcdef";
            if (value == 0)
            {
                return "0";
            }
            std::string out;
            while (value != 0)
            {
                out.insert(out.begin(), digits[value & 0xF]);
                value >>= 4;
            }
            return out;
        }

        void mix(std::uint64_t& hash, const std::string& bytes)
        {
            for (unsigned char c : bytes)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
        }
    }

    std::string mapIdentityOf(const std::string& ota, const std::string& tnt)
    {
        std::uint64_t hash = 14695981039346656037ull;
        mix(hash, ota);
        // A separator no length-ambiguity can pass through: the sizes are in
        // the identity too, so moving a byte from one file to the other differs.
        hash ^= 0xFF;
        hash *= 1099511628211ull;
        mix(hash, tnt);

        std::string hashText = hex(hash);
        hashText.insert(0, 16 - hashText.size(), '0');
        return hex(ota.size()) + "-" + hex(tnt.size()) + "-" + hashText;
    }

    std::string mapIdentity(const AbstractVirtualFileSystem& vfs, const std::string& mapName)
    {
        auto ota = vfs.readFile("maps/" + mapName + ".ota");
        auto tnt = vfs.readFile("maps/" + mapName + ".tnt");
        if (!ota || !tnt)
        {
            return {};
        }
        return mapIdentityOf(std::string(ota->begin(), ota->end()), std::string(tnt->begin(), tnt->end()));
    }

    bool isPlausibleMapIdentity(const std::string& text)
    {
        if (text.empty() || text.size() > MaxIdentityLength)
        {
            return false;
        }
        for (char c : text)
        {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-'))
            {
                return false;
            }
        }
        return true;
    }
}
