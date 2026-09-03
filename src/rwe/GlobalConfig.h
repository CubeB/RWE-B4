#pragma once

namespace rwe
{
    class GlobalConfig
    {
    public:
        bool leftClickInterfaceMode{true};

        /** bordered, borderless or fullscreen; changing it takes effect on restart. */
        std::string windowMode{"bordered"};

        /** 0 to 100, as the options screen and rwe.cfg deal in. */
        unsigned int soundVolume{100};
        unsigned int musicVolume{100};
        bool musicEnabled{true};

        /** Unit shadows on or off -- the VISUALS page's Shadows toggle. */
        bool shadows{true};

        /** Screen scroll speed percentage, 25 to 200; 100 is the old fixed rate. */
        unsigned int scrollSpeed{100};
    };
}
