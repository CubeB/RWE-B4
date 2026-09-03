#include "AudioService.h"

#include <algorithm>

namespace rwe
{
    AudioService::AudioService(
        SdlContext* sdlContext,
        SdlMixerContext* sdlMixerContext,
        AbstractVirtualFileSystem* fileSystem)
        : sdlContext(sdlContext),
          sdlMixerContext(sdlMixerContext),
          fileSystem(fileSystem)
    {
    }

    void AudioService::allocateTracks(unsigned int count)
    {
        tracks.reserve(count);
        for (unsigned int i = tracks.size(); i < count; ++i)
        {
            auto track = sdlMixerContext->createTrack();
            if (!track)
            {
                throw std::runtime_error("Failed to create mixer track");
            }
            tracks.push_back(std::move(track));
            setupTrackCallback(i);
        }
    }

    void AudioService::setupTrackCallback(int trackIndex)
    {
        sdlMixerContext->setTrackStoppedCallback(
            tracks[trackIndex].get(),
            [](void* userdata, MIX_Track* track)
            {
                // Runs on the audio thread with the track locked: only note it down.
                auto* self = static_cast<AudioService*>(userdata);
                for (unsigned int i = 0; i < self->tracks.size(); ++i)
                {
                    if (self->tracks[i].get() == track)
                    {
                        std::scoped_lock<std::mutex> lock(self->finishedChannelsLock);
                        self->finishedChannels.push_back(static_cast<int>(i));
                        break;
                    }
                }
            },
            this);
    }

    int AudioService::findFreeTrack()
    {
        // Search unreserved tracks for a free one
        for (unsigned int i = reservedCount; i < tracks.size(); ++i)
        {
            if (!sdlMixerContext->trackPlaying(tracks[i].get()))
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    AudioService::LoopToken AudioService::loopSound(const SoundHandle& sound)
    {
        int channel = findFreeTrack();
        if (channel == -1)
        {
            return LoopToken();
        }

        auto* track = tracks[channel].get();
        sdlMixerContext->setTrackAudio(track, sound.get());
        sdlMixerContext->setTrackGain(track, soundEnabled ? defaultGain * soundVolumeScale : 0.0f);

        auto props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        sdlMixerContext->playTrack(track, props);
        SDL_DestroyProperties(props);

        return LoopToken(this, channel, sound);
    }

    std::vector<std::string> AudioService::getMusicPlaylist()
    {
        std::vector<std::string> playlist;
        for (const auto& name : fileSystem->getFileNames("music", ".mp3"))
        {
            playlist.push_back("music/" + name);
        }
        std::sort(playlist.begin(), playlist.end());
        return playlist;
    }

    std::optional<std::string> AudioService::getThemePath()
    {
        auto playlist = getMusicPlaylist();
        if (playlist.empty())
        {
            return std::nullopt;
        }
        for (const auto& path : playlist)
        {
            auto lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("theme") != std::string::npos)
            {
                return path;
            }
        }
        return playlist.front();
    }

    void AudioService::setSoundVolume(float volume)
    {
        soundVolumeScale = std::clamp(volume, 0.0f, 1.0f);
    }

    void AudioService::setSoundEnabled(bool enabled)
    {
        soundEnabled = enabled;
    }

    void AudioService::setMusicVolume(float volume)
    {
        musicVolumeScale = std::clamp(volume, 0.0f, 1.0f);
        if (musicTrack)
        {
            sdlMixerContext->setTrackGain(musicTrack.get(), musicGain * musicVolumeScale * musicFadeScale);
        }
    }

    void AudioService::setMusicFadeScale(float scale)
    {
        musicFadeScale = std::clamp(scale, 0.0f, 1.0f);
        if (musicTrack)
        {
            sdlMixerContext->setTrackGain(musicTrack.get(), musicGain * musicVolumeScale * musicFadeScale);
        }
    }

    void AudioService::setMusicEnabled(bool enabled)
    {
        musicEnabled = enabled;
        if (!enabled)
        {
            stopMusic();
        }
    }

    bool AudioService::playMusic(const std::string& vfsPath, bool loop)
    {
        // The enable switch gates game music only; a movie soundtrack comes
        // through playMusicFromMemory and plays regardless.
        if (!musicEnabled)
        {
            return false;
        }

        auto bytes = fileSystem->readFile(vfsPath);
        if (!bytes)
        {
            return false;
        }

        return playMusicFromMemory(std::move(*bytes), loop);
    }

    bool AudioService::playMusicFromMemory(std::vector<char>&& bytes, bool loop)
    {
        stopMusic();
        musicBytes = std::move(bytes);

        auto rwOps = sdlContext->rwFromConstMem(musicBytes.data(), musicBytes.size());
        // No predecode: a four-minute track decoded to PCM is tens of
        // megabytes, and the mixer is perfectly happy streaming the mp3.
        auto audio = sdlMixerContext->loadAudioIO(rwOps.release(), false, true);
        if (!audio)
        {
            musicBytes.clear();
            return false;
        }
        musicAudio = std::move(audio);

        if (!musicTrack)
        {
            musicTrack = sdlMixerContext->createTrack();
        }

        musicFadeScale = 1.0f;
        sdlMixerContext->setTrackAudio(musicTrack.get(), musicAudio.get());
        sdlMixerContext->setTrackGain(musicTrack.get(), musicGain * musicVolumeScale);

        auto props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loop ? -1 : 0);
        sdlMixerContext->playTrack(musicTrack.get(), props);
        SDL_DestroyProperties(props);

        return true;
    }

    void AudioService::stopMusic()
    {
        if (musicTrack)
        {
            sdlMixerContext->stopTrack(musicTrack.get());
        }
        musicAudio.reset();
        musicBytes.clear();
    }

    bool AudioService::musicPlaying()
    {
        return musicTrack && sdlMixerContext->trackPlaying(musicTrack.get());
    }

    int AudioService::playSound(const SoundHandle& sound)
    {
        int channel = findFreeTrack();
        if (channel == -1)
        {
            return -1;
        }

        auto* track = tracks[channel].get();
        sdlMixerContext->setTrackAudio(track, sound.get());
        sdlMixerContext->setTrackGain(track, soundEnabled ? defaultGain * soundVolumeScale : 0.0f);
        sdlMixerContext->playTrack(track);

        return channel;
    }

    std::optional<AudioService::SoundHandle> AudioService::loadSound(const std::string& soundName)
    {
        auto soundIter = soundBank.find(soundName);
        if (soundIter != soundBank.end())
        {
            return soundIter->second;
        }

        auto bytes = fileSystem->readFile("sounds/" + soundName + ".WAV");
        if (!bytes)
        {
            return std::nullopt;
        }

        auto rwOps = sdlContext->rwFromConstMem(bytes->data(), bytes->size());
        auto audio = sdlMixerContext->loadAudioIO(rwOps.get(), true, false);
        if (!audio)
        {
            return std::nullopt;
        }

        std::shared_ptr<MIX_Audio> sound(audio.release(), [](MIX_Audio* a) { MIX_DestroyAudio(a); });
        soundBank[soundName] = sound;

        return sound;
    }

    void AudioService::reserveChannels(unsigned int count)
    {
        if (count > tracks.size())
        {
            allocateTracks(count);
        }
        reservedCount = count;
    }

    void AudioService::haltChannel(int channel)
    {
        if (channel >= 0 && static_cast<unsigned int>(channel) < tracks.size())
        {
            sdlMixerContext->stopTrack(tracks[channel].get());
        }
    }

    void AudioService::playSoundIfFree(const AudioService::SoundHandle& sound, unsigned int channel)
    {
        if (channel >= tracks.size())
        {
            return;
        }

        if (sdlMixerContext->trackPlaying(tracks[channel].get()))
        {
            return;
        }

        auto* track = tracks[channel].get();
        sdlMixerContext->setTrackAudio(track, sound.get());
        sdlMixerContext->setTrackGain(track, soundEnabled ? defaultGain * soundVolumeScale : 0.0f);
        sdlMixerContext->playTrack(track);
    }

    Observable<int>& AudioService::getChannelFinished()
    {
        return channelFinished;
    }

    void AudioService::dispatchFinishedChannels()
    {
        std::vector<int> finished;
        {
            std::scoped_lock<std::mutex> lock(finishedChannelsLock);
            finished.swap(finishedChannels);
        }
        for (auto channel : finished)
        {
            channelFinished.next(channel);
        }
    }

    void AudioService::setVolume(int channel, int volume)
    {
        if (channel >= 0 && static_cast<unsigned int>(channel) < tracks.size())
        {
            // Old scale: 0-128 (MIX_MAX_VOLUME). New scale: 0.0-1.0
            float gain = static_cast<float>(volume) / 128.0f;
            sdlMixerContext->setTrackGain(tracks[channel].get(), gain);
        }
    }

    AudioService::LoopToken::LoopToken(AudioService* audioService, int channel, const AudioService::SoundHandle& sound)
        : audioService(audioService), channel(channel), sound(sound)
    {
    }

    AudioService::LoopToken::~LoopToken()
    {
        if (channel != -1)
        {
            audioService->haltChannel(channel);
        }
    }

    AudioService::LoopToken& AudioService::LoopToken::operator=(AudioService::LoopToken&& other) noexcept
    {
        audioService = other.audioService;
        channel = other.channel;
        sound = std::move(other.sound);

        other.channel = -1;

        return *this;
    }

    AudioService::LoopToken::LoopToken(AudioService::LoopToken&& other) noexcept
        : audioService(other.audioService), channel(other.channel), sound(std::move(other.sound))
    {
        other.channel = -1;
    }

    AudioService::LoopToken::LoopToken() : audioService(nullptr), channel(-1), sound(nullptr) {}

    std::optional<std::reference_wrapper<const AudioService::SoundHandle>> AudioService::LoopToken::getSound()
    {
        if (channel == -1)
        {
            return std::nullopt;
        }

        return sound;
    }
}
