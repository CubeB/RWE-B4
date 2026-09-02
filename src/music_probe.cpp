#include <SDL3/SDL.h>
#include <iostream>
#include <rwe/AudioService.h>
#include <rwe/sdl/SdlContextManager.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>
#include <thread>

// Diagnostic harness for the music path: enumerates the playlist the game
// would build, resolves the theme, and tries to actually play it both
// looping and not, reporting what the mixer says. Run against a data dir:
//   music_probe <data-path>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: music_probe <data-path>" << std::endl;
        return 1;
    }

    rwe::SdlContextManager sdlManager;
    rwe::CompositeVirtualFileSystem vfs;
    rwe::addToVfs(vfs, argv[1]);

    rwe::AudioService audio(sdlManager.getSdlContext(), sdlManager.getSdlMixerContext(), &vfs);

    auto playlist = audio.getMusicPlaylist();
    std::cout << "playlist entries: " << playlist.size() << "\n";
    for (const auto& p : playlist)
    {
        std::cout << "  " << p << "\n";
    }

    auto theme = audio.getThemePath();
    std::cout << "theme: " << (theme ? *theme : std::string("<none>")) << "\n";
    if (!theme)
    {
        return 1;
    }

    std::cout << "playMusic(theme, loop=true): " << (audio.playMusic(*theme, true) ? "ok" : "FAILED") << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "musicPlaying after 500ms: " << (audio.musicPlaying() ? "yes" : "NO") << "\n";
    audio.stopMusic();

    std::cout << "playMusic(theme, loop=false): " << (audio.playMusic(*theme, false) ? "ok" : "FAILED") << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "musicPlaying after 500ms: " << (audio.musicPlaying() ? "yes" : "NO") << "\n";
    audio.stopMusic();

    return 0;
}
