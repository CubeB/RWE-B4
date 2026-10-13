#include "BuildIdentity.h"
#include <algorithm>

namespace rwe
{
    // Defined in the file CMake regenerates at every build.
    extern const char* const generatedBuildId;

    namespace
    {
        constexpr const char* unknownIdentity = "unknown";
    }

    const std::string& buildIdentity()
    {
        static const std::string identity = generatedBuildId != nullptr && generatedBuildId[0] != '\0'
            ? std::string(generatedBuildId)
            : std::string(unknownIdentity);
        return identity;
    }

    std::string shortBuildIdentity(const std::string& identity)
    {
        if (identity.empty())
        {
            return "an older version";
        }
        if (identity == unknownIdentity)
        {
            return identity;
        }

        const auto dirty = identity.size() > 2 && identity.compare(identity.size() - 2, 2, "-d") == 0;
        return identity.substr(0, 8) + (dirty ? "-d" : "");
    }

    bool buildsCompatible(const std::string& mine, const std::string& theirs)
    {
        if (mine.empty() || mine == unknownIdentity)
        {
            return true;
        }
        if (theirs.empty())
        {
            return false;
        }
        if (theirs == unknownIdentity)
        {
            return true;
        }
        return mine == theirs;
    }

    bool isPlausibleBuildIdentity(const std::string& text)
    {
        if (text == unknownIdentity)
        {
            return true;
        }
        if (text.empty() || text.size() > 42)
        {
            return false;
        }

        auto digits = text;
        if (digits.size() > 2 && digits.compare(digits.size() - 2, 2, "-d") == 0)
        {
            digits.resize(digits.size() - 2);
        }
        return !digits.empty()
            && std::all_of(digits.begin(), digits.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }
}
