#include "PathMapping.h"
#include <rwe/util/OpaqueArgs.h>

namespace rwe
{
    bool applyPathMappingDirectory(PathMapping& mapping, const std::string& name, const std::string& value)
    {
        if (name == "ai") mapping.ai = value;
        else if (name == "anims") mapping.anims = value;
        else if (name == "bitmaps") mapping.bitmaps = value;
        else if (name == "camps") mapping.camps = value;
        else if (name == "downloads") mapping.downloads = value;
        else if (name == "features") mapping.features = value;
        else if (name == "fonts") mapping.fonts = value;
        else if (name == "gamedata") mapping.gamedata = value;
        else if (name == "guis") mapping.guis = value;
        else if (name == "builderGuis") mapping.builderGuis = value;
        else if (name == "maps") mapping.maps = value;
        else if (name == "objects3d") mapping.objects3d = value;
        else if (name == "palettes") mapping.palettes = value;
        else if (name == "scripts") mapping.scripts = value;
        else if (name == "sounds") mapping.sounds = value;
        else if (name == "textures") mapping.textures = value;
        else if (name == "unitpics") mapping.unitpics = value;
        else if (name == "units") mapping.units = value;
        else if (name == "weapons") mapping.weapons = value;
        else return false;
        return true;
    }

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
        // RWE-original split: Escalation's guiE contains builder pages, not
        // the front-end screens. See docs/BUILDER-GUI-RESOURCE-DIRECTORY.md.
        m.builderGuis = args.getString("dir-builder-guis", m.guis);
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
