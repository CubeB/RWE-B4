#include "TdfParser.h"
#include <rwe/util/rwe_string.h>

#include <utf8.h>

namespace rwe
{
    namespace
    {
        std::string atPosition(std::size_t line, std::size_t column, const std::string& message)
        {
            return "line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message;
        }
    }

    std::string describeTdfCodePoint(TdfCodePoint cp)
    {
        if (cp >= 0x20 && cp < 0x7f)
        {
            return std::string("'") + static_cast<char>(cp) + "'";
        }

        return "code point " + std::to_string(cp);
    }

    TdfParserException::TdfParserException(std::size_t line, std::size_t column, const char* message) : runtime_error(atPosition(line, column, message)), line(line), column(column) {}

    TdfParserException::TdfParserException(std::size_t line, std::size_t column, const std::string& message) : runtime_error(atPosition(line, column, message)), line(line), column(column) {}
}
