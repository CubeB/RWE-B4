#pragma once

#include <cstdint>
#include <istream>
#include <stdexcept>
#include <string>

namespace rwe
{
    /**
     * A read that asked for more bytes than the file had. Thrown by readRaw
     * where it used to be an assert, which a release build drops: the caller
     * got a struct of whatever was on the stack. Parsers whose own exception
     * type matters catch this and rethrow it as theirs.
     */
    class TruncatedInputError : public std::runtime_error
    {
    public:
        TruncatedInputError() : std::runtime_error("unexpected end of file") {}
    };

    template <typename T>
    T readRaw(std::istream& stream)
    {
        T val{};
        stream.read(reinterpret_cast<char*>(&val), sizeof(T));
        if (stream.fail())
        {
            throw TruncatedInputError();
        }
        return val;
    }

    std::string readNullTerminatedString(std::istream& stream);

    /**
     * The number of bytes in the stream, leaving the read position where it
     * was. Used to bound a count or an offset from the file against the file.
     */
    std::uint64_t streamSize(std::istream& stream);
}
