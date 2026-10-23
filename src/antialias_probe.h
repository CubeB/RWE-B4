#pragma once
namespace rwe
{
    class GraphicsContext;
    class TextureService;
    class AbstractVirtualFileSystem;
    int runAntialiasProbe(GraphicsContext&, TextureService&, AbstractVirtualFileSystem&);
}
