#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <rwe/observable/Subject.h>
#include <rwe/sdl/SdlContext.h>
#include <rwe/sdl/SdlMixerContext.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <unordered_map>
#include <vector>

namespace rwe
{
    class AudioService
    {
    public:
        using Sound = MIX_Audio;
        using SoundHandle = std::shared_ptr<Sound>;

        class LoopToken
        {
        private:
            AudioService* audioService;
            int channel;
            SoundHandle sound;

        public:
            LoopToken();
            LoopToken(AudioService* audioService, int channel, const SoundHandle& sound);
            ~LoopToken();
            LoopToken(const LoopToken&) = delete;
            LoopToken& operator=(const LoopToken&) = delete;
            LoopToken(LoopToken&& other) noexcept;
            LoopToken& operator=(LoopToken&& other) noexcept;
            std::optional<std::reference_wrapper<const SoundHandle>> getSound();
        };

    private:
        SdlContext* sdlContext;
        SdlMixerContext* sdlMixerContext;
        AbstractVirtualFileSystem* fileSystem;
        std::unordered_map<std::string, std::shared_ptr<Sound>> soundBank;
        Subject<int> channelFinished;

        /** Tracks the mixer reported finished, noted on the audio thread and announced from the main thread. */
        std::mutex finishedChannelsLock;
        std::vector<int> finishedChannels;

        // Track pool: maps channel indices to MIX_Track pointers.
        // Tracks 0..reservedCount-1 are "reserved" (used by playSoundIfFree).
        std::vector<SdlMixerContext::TrackPtr> tracks;
        unsigned int reservedCount{0};

        // Default gain applied to sounds on load (equivalent to old MIX_MAX_VOLUME/4)
        static constexpr float defaultGain = 0.25f;

        // Music sits under the effects rather than over them.
        static constexpr float musicGain = 0.3f;

        // User-set volume scales, 0 to 1, applied on top of the base gains.
        float soundVolumeScale{1.0f};
        float musicVolumeScale{1.0f};
        bool musicEnabled{true};

        /**
         * The one track music plays on. The GOG release ships the CD audio as
         * music/<track>.mp3 in the game directory, which the VFS picks up
         * along with everything else, so music is read the same way as any
         * other game file. The compressed bytes have to stay alive for as
         * long as the mixer is streaming from them, hence the buffer here.
         */
        SdlMixerContext::TrackPtr musicTrack;
        SdlMixerContext::AudioPtr musicAudio;
        std::vector<char> musicBytes;

    public:
        AudioService(SdlContext* sdlContext, SdlMixerContext* sdlMixerContext, AbstractVirtualFileSystem* fileSystem);
        AudioService(const AudioService&) = delete;
        AudioService(const AudioService&&) = delete;
        AudioService& operator=(const AudioService&) = delete;
        AudioService& operator=(AudioService&&) = delete;

        void allocateTracks(unsigned int count);

        LoopToken loopSound(const SoundHandle& sound);

        int playSound(const SoundHandle& sound);

        std::optional<SoundHandle> loadSound(const std::string& soundName);

        void reserveChannels(unsigned int count);

        void playSoundIfFree(const SoundHandle& sound, unsigned int channel);

        /**
         * Plays a music file from the VFS on the dedicated music track,
         * replacing whatever was playing. Returns false when the file is not
         * there or will not decode, so a caller can stop asking.
         */
        bool playMusic(const std::string& vfsPath, bool loop);

        /** As playMusic, but from bytes already in hand (a movie soundtrack). */
        bool playMusicFromMemory(std::vector<char>&& bytes, bool loop);

        void stopMusic();

        bool musicPlaying();

        void setSoundVolume(float volume);
        float getSoundVolume() const { return soundVolumeScale; }

        /** Applies to the playing track immediately. */
        void setMusicVolume(float volume);
        float getMusicVolume() const { return musicVolumeScale; }

        /** Turning music off stops it there and then; movies are unaffected. */
        void setMusicEnabled(bool enabled);
        bool isMusicEnabled() const { return musicEnabled; }

        void setVolume(int channel, int volume);

        Observable<int>& getChannelFinished();

        /**
         * Announces the channels that have finished since the last call.
         * Call from the main thread: the mixer reports them from its audio
         * thread while holding its own locks, so nothing that talks back to
         * the mixer may run there.
         */
        void dispatchFinishedChannels();

    private:
        void haltChannel(int channel);
        int findFreeTrack();
        void setupTrackCallback(int trackIndex);
    };
}
