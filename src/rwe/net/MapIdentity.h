#pragma once

#include <string>

namespace rwe
{
    class AbstractVirtualFileSystem;

    /**
     * A map's identity for a lobby: the sizes of its `.ota` and `.tnt` and a
     * hash over both, as `<ota size>-<tnt size>-<hash>` in lower-case hex. Two
     * machines with a same-named map that differs in either file get different
     * strings, which a same-named check by file existence cannot tell and a
     * lockstep game finds out as a desync (#720). The hash is FNV-1a 64: it
     * is there to catch an honest mismatch, and says nothing against someone
     * who builds a map to collide with one.
     *
     * Empty when either file cannot be read, which the lobby takes as "no
     * identity to compare" and not as a mismatch.
     */
    std::string mapIdentity(const AbstractVirtualFileSystem& vfs, const std::string& mapName);

    /** The same over the two files' bytes. */
    std::string mapIdentityOf(const std::string& ota, const std::string& tnt);

    /** Whether a string is shaped like a map identity: only [0-9a-f-], at most 48 characters. */
    bool isPlausibleMapIdentity(const std::string& text);
}
