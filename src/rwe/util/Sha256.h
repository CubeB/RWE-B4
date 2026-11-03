#pragma once

#include <string>
#include <string_view>

namespace rwe
{
    // Lowercase hex SHA-256. Used for content identity, not as a security
    // primitive: callers choose what bytes define a package or a mod set.
    std::string sha256Hex(std::string_view bytes);
}
