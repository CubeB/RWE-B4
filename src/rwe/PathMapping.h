#pragma once

#include <string>

namespace rwe
{
    class OpaqueArgs;

    struct PathMapping
    {
        std::string ai;
        std::string anims;
        std::string bitmaps;
        std::string camps;
        std::string downloads;
        std::string features;
        std::string fonts;
        std::string gamedata;
        std::string guis;
        /** Unit build-menu GUIs, separate from front-end screen layouts. */
        std::string builderGuis{"guis"};
        std::string maps;
        std::string objects3d;
        std::string palettes;
        std::string scripts;
        std::string sounds;
        std::string textures;
        std::string unitpics;
        std::string units;
        std::string weapons;
    };

    // Explicit mod roots; vanilla defaults remain unchanged. MOD-DIRS-01 in
    // docs/TA-MOD-COMPATIBILITY-FINDINGS.md records this RWE extension.
    PathMapping configuredPathMapping(const OpaqueArgs& args);
}
