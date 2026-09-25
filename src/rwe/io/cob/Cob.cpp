#include "Cob.h"
#include <rwe/io/io_util.h>
#include <stdexcept>

namespace rwe
{
    CobScript parseCob(std::istream& stream)
    {
        auto header = readRaw<CobHeader>(stream);

        // Each count sizes an allocation before a byte behind it is read, and
        // every scripts/*.cob in every installed archive is loaded at game
        // start. The static count is allocated again for every unit that
        // runs the script. TA's scripts are a few kilobytes with a few dozen
        // functions and pieces; these are far past any. Issue #75.
        constexpr uint32_t MaxCodeWords = 1u << 20;
        constexpr uint32_t MaxEntries = 4096;
        if (!stream || header.codeLength > MaxCodeWords || header.numberOfScripts > MaxEntries
            || header.numberOfPieces > MaxEntries || header.staticVariableCount > MaxEntries)
        {
            throw std::runtime_error("COB header counts out of range");
        }

        CobScript script;
        script.staticVariableCount = header.staticVariableCount;

        // read in the instructions
        script.instructions.resize(header.codeLength);
        stream.seekg(header.offsetToScriptCode);
        for (unsigned int i = 0; i < header.codeLength; ++i)
        {
            script.instructions[i] = readRaw<uint32_t>(stream);
        }


        // read in function addresses
        script.functions.resize(header.numberOfScripts);
        stream.seekg(header.offsetToScriptCodeIndexArray);
        for (unsigned int i = 0; i < header.numberOfScripts; ++i)
        {
            script.functions[i].address = readRaw<uint32_t>(stream);
        }

        // read in function names
        stream.seekg(header.offsetToScriptNameOffsetArray);
        for (unsigned int i = 0; i < header.numberOfScripts; ++i)
        {
            auto nameOffset = readRaw<uint32_t>(stream);
            auto loc = stream.tellg();
            stream.seekg(nameOffset);
            script.functions[i].name = readNullTerminatedString(stream);
            stream.seekg(loc);
        }

        // read in piece names
        script.pieces.resize(header.numberOfPieces);
        stream.seekg(header.offsetToPieceNameOffsetArray);
        for (unsigned int i = 0; i < header.numberOfPieces; ++i)
        {
            auto nameOffset = readRaw<uint32_t>(stream);
            auto loc = stream.tellg();
            stream.seekg(nameOffset);
            script.pieces[i] = readNullTerminatedString(stream);
            stream.seekg(loc);
        }

        return script;
    }
}
