#include "io_util.h"

namespace rwe
{
    std::string readNullTerminatedString(std::istream& stream)
    {
        char c;

        std::string result;

        for (;;)
        {
            stream.read(&c, 1);
            if (stream.fail() || c == '\0')
            {
                return result;
            }

            result.push_back(c);
        }
    }

    std::uint64_t streamSize(std::istream& stream)
    {
        stream.clear();
        auto previous = stream.tellg();
        stream.seekg(0, std::ios::end);
        auto end = stream.tellg();
        stream.seekg(previous);
        if (end < 0)
        {
            return 0;
        }
        return static_cast<std::uint64_t>(end);
    }
}
