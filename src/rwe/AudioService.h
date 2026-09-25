#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <optional>
#include <rwe/audio/EffectsCompressor.h>
#include <rwe/observable/Subject.h>
#include <rwe/sdl/SdlContext.h>
#include <rwe/sdl/SdlMixerContext.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <unordered_map>
#include <vector>

namespace rwe
{
    /**
     * One unreserved track's state, as seen by selectTrackForSound: whether
     * it is currently playing, and -- if it is -- the identity of the sound
     * on it. Kept free of MIX_Track/MIX_Audio so the picking logic below can
     * run under a test with no mixer device behind it.
     */
    struct TrackSlot
    {
        bool playing;
        const void* soundKey;
    };

    /**
     * Chooses the unreserved track a new copy of `soundKey` should play on.
     * TA's own per-effect voice limit has not been read out of the exe --
     * only the eight-slot unit-notification queue at `0x47FAD0` is decoded
     * (docs/TOTALA-EXE.md S:"underattack, repair and cant"), and that is a
     * different mechanism for a different class of sound. Absent the
     * original's rule for weapon fire and impacts, this caps concurrent
     * copies of one sample conservatively instead of guessing at a limiter:
     * a handful of overlapping copies of the same wav reads as one loud
     * effect, forty of them reads as clipping (issue #58). A track is never
     * taken from a sound still playing on it -- the search only ever returns
     * a track that is already free -- so nothing already sounding is cut
     * short by this.
     */
    std::optional<unsigned int> selectTrackForSound(
        const std::vector<TrackSlot>& tracks,
        unsigned int reservedCount,
        const void* soundKey,
        unsigned int maxConcurrentCopies);

    /**
     * The gain an effect track should be given, from the 0-128 channel
     * volume the game's automatic gain control still speaks in.
     *
     * SDL2_mixer had two gains in series: Mix_VolumeChunk set the sample's
     * own level -- MIX_MAX_VOLUME/4 here, which is defaultGain -- and
     * Mix_Volume scaled the channel on top of it, so the two multiplied.
     * SDL3_mixer has one gain per track and MIX_SetTrackGain replaces it,
     * so the migration (45eb5f03) left `volume / 128` as the whole of the
     * level and the quarter was lost.
     *
     * That made every effect four times louder than intended, and it turned
     * computeSoundCeiling's budget into something it was never written to
     * be: the ceiling counts in units of one sound at the base gain and
     * allows at most eight of them, which is a peak of 2.0 when the base
     * gain is a quarter and 8.0 without it. The mixer can only clamp what
     * will not fit, and that is what a loud battle sounded like (issue #58).
     *
     * The volume setting and Sound Mode Off belong here too. Every other
     * play path applies them, and this one is reapplied to every unit sound
     * channel each frame, so leaving them out let weapon fire and impacts
     * ignore the slider and the mute outright.
     */
    float computeEffectGain(int volume, float baseGain, float volumeScale, bool enabled);

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

        /**
         * Every pooled track -- weapon fire, impacts, unit reports -- mixes
         * into this group, and the group's finished mix goes through a
         * downward compressor before it joins the music (#58, and the
         * suggestion on it). The loudness law in GameScene_audio shares a
         * fixed budget out by COUNTING sounds, which cannot know that four
         * explosions landing on one sample add up to more than four lasers
         * spread across a second; this listens to the sum itself. Music and
         * the one-shot interface sounds are not in the group and are not
         * touched. The compressor's state belongs to the audio thread alone;
         * the switch is the only thing both threads read.
         */
        MIX_Group* effectsGroup{nullptr};
        EffectsCompressor effectsCompressor;
        std::atomic<bool> effectsCompressorEnabled{true};

        // Parallel to tracks: the sound each one was last given, so
        // selectTrackForSound can count how many copies of a sample are
        // already sounding. Stale once a track finishes, but findTrackForSound
        // only ever reads an entry alongside that track's own trackPlaying(),
        // so a stale key on a silent track is never mistaken for a live copy.
        std::vector<const Sound*> trackSoundKey;

        // Never let more than this many copies of one sample sound at once.
        // See selectTrackForSound's comment for why this is RWE's own number
        // and not a ported one.
        static constexpr unsigned int maxConcurrentCopiesOfOneSound = 4;

        // Base gain for an effect, multiplied by the channel volume the
        // automatic gain control hands out -- see computeEffectGain.
        //
        // Upstream used MIX_MAX_VOLUME/4. A half is RWE's own, tuned by ear
        // against a play-test: a quarter is what the old two-gain
        // arrangement came to, and it left a lone explosion 12 dB down with
        // the whole battle playing under the music. computeSoundCeiling's
        // cap came down from eight to four to pay for it, so the worst-case
        // summed peak is exactly where it was.
        static constexpr float defaultGain = 0.5f;

        // Music sits under the effects rather than over them.
        static constexpr float musicGain = 0.3f;

        // User-set volume scales, 0 to 1, applied on top of the base gains.
        float soundVolumeScale{1.0f};
        float musicVolumeScale{1.0f};
        float musicFadeScale{1.0f};
        bool musicEnabled{true};
        bool soundEnabled{true};

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

        /** Stops what playSound started on that channel; -1, its "nothing played", is ignored. */
        void stopChannel(int channel);

        std::optional<SoundHandle> loadSound(const std::string& soundName);

        /**
         * A sound read from anywhere in the virtual file system, kept by its
         * path. loadSound looks only in sounds/, and a campaign's narration
         * lives in camps/briefs.
         */
        std::optional<SoundHandle> loadSoundFromPath(const std::string& path);

        void reserveChannels(unsigned int count);

        /** On by default. Off, the effects group is mixed as it comes, which is how it was before 2026-09-19. */
        void setEffectsCompressorEnabled(bool enabled);

        ~AudioService();

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

        /**
         * Every mp3 in the music directory, as VFS paths. Whatever is in
         * there plays -- the GOG numbered rips or a properly tagged
         * soundtrack rip both work. An entry whose name contains "theme"
         * is the title music; getThemePath finds it, falling back to the
         * first file.
         */
        std::vector<std::string> getMusicPlaylist();

        std::optional<std::string> getThemePath();

        void setSoundVolume(float volume);

        /** Sound Mode Off silences the effects without disturbing the volume setting. */
        void setSoundEnabled(bool enabled);
        bool isSoundEnabled() const { return soundEnabled; }
        float getSoundVolume() const { return soundVolumeScale; }

        /** Applies to the playing track immediately. */
        void setMusicVolume(float volume);

        /** A transient scale for fading a track out; reset to 1 by the next play. */
        void setMusicFadeScale(float scale);
        float getMusicVolume() const { return musicVolumeScale; }

        /** Turning music off stops it there and then; movies are unaffected. */
        void setMusicEnabled(bool enabled);
        bool isMusicEnabled() const { return musicEnabled; }

        void setVolume(int channel, int volume);

        /**
         * True while the mixer is still sounding this channel. A caller
         * holding on to a channel index from an earlier play (GameScene's
         * playingUnitChannels does, for the AGC in computeSoundVolume) needs
         * this to tell "still my sound" from "reused for someone else's"
         * before believing a finished notification for it -- see the note at
         * GameScene::onChannelFinished.
         */
        bool isChannelPlaying(unsigned int channel);

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
        std::optional<unsigned int> findTrackForSound(const Sound* soundKey, unsigned int maxConcurrentCopies);
        void setupTrackCallback(int trackIndex);
    };
}
