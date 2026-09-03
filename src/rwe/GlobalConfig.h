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

        /** 0 off, 1 mono, 2 stereo. */
        unsigned int soundMode{2};

        /** Unit voice acknowledgements: 0 off, 1 medium, 2 full. */
        unsigned int unitSpeech{2};

        /** Screen gamma percentage, 50 to 133 (the original's own range); 100 is untouched. */
        unsigned int gamma{100};

        /** Model lighting, the VISUALS page's Shading switch; on in the original. */
        bool shading{true};

        /** Edge anti-aliasing: the original supersamples the unit and box-filters it down. */
        bool antiAlias{true};
    };
}
