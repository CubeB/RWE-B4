#include "PathMapping.h"
#include <rwe/util/OpaqueArgs.h>

namespace rwe
{
    PathMapping configuredPathMapping(const OpaqueArgs& args)
    {
        PathMapping m;
        m.ai = args.getString("dir-ai", "ai");
        m.anims = args.getString("dir-anims", "anims");
        m.bitmaps = args.getString("dir-bitmaps", "bitmaps");
        m.camps = args.getString("dir-camps", "camps");
        m.downloads = args.getString("dir-downloads", "download");
        m.features = "features";
        m.fonts = args.getString("dir-fonts", "fonts");
        m.gamedata = args.getString("dir-gamedata", "gamedata");
        m.guis = args.getString("dir-guis", "guis");
        m.maps = args.getString("dir-maps", "maps");
        m.objects3d = args.getString("dir-objects3d", "objects3d");
        m.palettes = args.getString("dir-palettes", "palettes");
        m.scripts = args.getString("dir-scripts", "scripts");
        m.sounds = args.getString("dir-sounds", "sounds");
        m.textures = args.getString("dir-textures", "textures");
        m.unitpics = args.getString("dir-unitpics", "unitpics");
        m.units = args.getString("dir-units", "units");
        m.weapons = args.getString("dir-weapons", "weapons");
        return m;
    }
}
