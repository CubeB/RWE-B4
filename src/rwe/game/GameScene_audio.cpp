#include "GameScene.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <rwe/CroppedViewport.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/game/SaveFile.h>
#include <rwe/io/gui/gui.h>
#include <rwe/game/save_util.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util.h>
#include <rwe/MainMenuScene.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/Mesh.h>
#include <rwe/camera_util.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/game/OrderButtons.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/matrix_util.h>
#include <rwe/render/render_prof.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/resource_io.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/Index.h>
#include <rwe/util/match.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>

// Sound, music and the notifications that go with them, split out of
// GameScene.cpp for the reason set out at the head of GameScene_render.cpp:
// the single translation unit had outgrown the 32767 sections a COFF object
// can address.

namespace rwe
{
    void GameScene::playUiSound(const AudioService::SoundHandle& handle)
    {
        sceneContext.audioService->playSoundIfFree(handle, UnitSelectChannel);
    }

    void GameScene::playNotificationSound(const PlayerId& playerId, const AudioService::SoundHandle& sound)
    {
        if (playerId == localPlayerId)
        {
            sceneContext.audioService->playSoundIfFree(sound, UnitSelectChannel);
        }
    }


    std::optional<std::string> getSoundName(const SoundClass& c, UnitSoundType sound)
    {
        switch (sound)
        {
            case UnitSoundType::Select1:
                return c.select1;
            case UnitSoundType::UnitComplete:
                return c.unitComplete;
            case UnitSoundType::Activate:
                return c.activate;
            case UnitSoundType::Deactivate:
                return c.deactivate;
            case UnitSoundType::Ok1:
                return c.ok1;
            case UnitSoundType::Arrived1:
                return c.arrived1;
            case UnitSoundType::Cant1:
                return c.cant1;
            case UnitSoundType::UnderAttack:
                return c.underAttack;
            case UnitSoundType::Build:
                return c.build;
            case UnitSoundType::Repair:
                return c.repair;
            case UnitSoundType::Working:
                return c.working;
            case UnitSoundType::Cloak:
                return c.cloak;
            case UnitSoundType::Uncloak:
                return c.uncloak;
            case UnitSoundType::Capture:
                return c.capture;
            case UnitSoundType::Count5:
                return c.count5;
            case UnitSoundType::Count4:
                return c.count4;
            case UnitSoundType::Count3:
                return c.count3;
            case UnitSoundType::Count2:
                return c.count2;
            case UnitSoundType::Count1:
                return c.count1;
            case UnitSoundType::Count0:
                return c.count0;
            case UnitSoundType::CancelDestruct:
                return c.cancelDestruct;
            default:
                throw std::logic_error("Invalid sound type");
        }
    }

    std::optional<AudioService::SoundHandle> getSound(const GameSimulation& sim, const GameMediaDatabase& meshDb, const std::string& unitType, UnitSoundType soundType)
    {
        const auto& unitDefinition = sim.unitDefinitions.at(unitType);
        const auto& soundClass = meshDb.getSoundClassOrDefault(unitDefinition.soundCategory);
        const auto& soundId = getSoundName(soundClass, soundType);
        if (soundId)
        {
            return meshDb.tryGetSoundHandle(*soundId);
        }
        return std::nullopt;
    }

    void GameScene::playUnitNotificationSound(const PlayerId& playerId, const std::string& unitType, UnitSoundType soundType)
    {
        // SOUNDS.GUI's Unit Sounds setting. Off silences the chatter
        // entirely; Medium keeps only what a player needs to hear -- the
        // warnings and the completions -- and drops the acknowledgements.
        // (Which sounds sit in "medium" is inference: the original's own
        // split has not been read out of the binary.)
        if (unitSpeechSetting == UnitSpeechLevel::Off)
        {
            return;
        }
        if (unitSpeechSetting == UnitSpeechLevel::Medium)
        {
            switch (soundType)
            {
                case UnitSoundType::Select1:
                case UnitSoundType::Ok1:
                case UnitSoundType::Arrived1:
                    return;
                default:
                    break;
            }
        }

        auto sound = getSound(simulation, gameMediaDatabase, unitType, soundType);
        if (sound)
        {
            playNotificationSound(playerId, *sound);
        }
    }

    namespace
    {
        /**
         * TA's ten player colours, read off the shipped palette by eye --
         * close enough for tinting a line of text.
         */
        Color playerColorToRgb(const PlayerColorIndex& index)
        {
            static const Color colors[] = {
                Color(60, 88, 244),   // blue
                Color(228, 32, 32),   // red
                Color(252, 252, 252), // white
                Color(24, 208, 24),   // green
                Color(44, 60, 148),   // navy
                Color(180, 72, 180),  // purple
                Color(252, 252, 0),   // yellow
                Color(96, 96, 96),    // black, lifted so it still reads
                Color(128, 192, 252), // sky
                Color(240, 160, 40),  // orange
            };
            return index.value < 10 ? colors[index.value] : Color(255, 255, 255);
        }
    }

    void GameScene::printConsole(const std::string& text, const Color& color)
    {
        // Five seconds a line, and never more than eight on screen.
        consoleMessages.push_back(ConsoleMessage{text, color, sceneTime + SceneTime(5u * 30u)});
        while (consoleMessages.size() > 8)
        {
            consoleMessages.pop_front();
        }
    }

    void GameScene::renderConsole()
    {
        while (!consoleMessages.empty() && consoleMessages.front().expires <= sceneTime)
        {
            consoleMessages.pop_front();
        }

        // Top-left of the world view, under the resource bar, newest line at
        // the bottom -- where the original prints its speech text, and in the
        // font it prints it in: COMIX, the taller of its two game fonts.
        float y = static_cast<float>(GuiSizeTop) + 16.0f;
        for (const auto& message : consoleMessages)
        {
            chromeUiRenderService.drawText(static_cast<float>(GuiSizeLeft) + 8.0f, y, message.text, *speechFont, message.color);
            y += 14.0f;
        }
    }

    void GameScene::updateSelfDestructNotifications()
    {
        // "Commander: five", printed and spoken a number a second. The words
        // zero..five sit beside the count0-count5 speech keys in the binary
        // with the format "%s: %s" under a "Speech Text" label, and SOUND.TDF
        // maps them backwards -- count5 plays the file COUNT1 -- because the
        // recordings are numbered by their position in the countdown, not by
        // the number they say.
        static const char* const countWords[] = {"zero", "one", "two", "three", "four", "five"};

        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unit.isOwnedBy(localPlayerId))
            {
                continue;
            }

            auto it = selfDestructAnnounced.find(unitId);
            if (!unit.isAlive() || !unit.selfDestructTime)
            {
                if (it != selfDestructAnnounced.end())
                {
                    selfDestructAnnounced.erase(it);
                    if (unit.isAlive())
                    {
                        // Toggled off, not gone off.
                        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                        printConsole(unitDefinition.unitName + ": Self destruct terminated");
                        playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::CancelDestruct);
                    }
                }
                continue;
            }

            auto ticksLeft = *unit.selfDestructTime > simulation.gameTime
                ? (*unit.selfDestructTime - simulation.gameTime).value
                : 0u;
            auto secondsLeft = (ticksLeft + SimTicksPerSecond - 1) / SimTicksPerSecond;
            if (it != selfDestructAnnounced.end() && it->second == secondsLeft)
            {
                continue;
            }
            selfDestructAnnounced[unitId] = secondsLeft;

            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            auto word = secondsLeft < 6 ? std::string(countWords[secondsLeft]) : std::to_string(secondsLeft);
            printConsole(unitDefinition.unitName + ": " + word);

            std::optional<UnitSoundType> countSound;
            switch (secondsLeft)
            {
                case 5: countSound = UnitSoundType::Count5; break;
                case 4: countSound = UnitSoundType::Count4; break;
                case 3: countSound = UnitSoundType::Count3; break;
                case 2: countSound = UnitSoundType::Count2; break;
                case 1: countSound = UnitSoundType::Count1; break;
                default: break;
            }
            if (countSound)
            {
                playUnitNotificationSound(unit.owner, unit.unitType, *countSound);
            }
        }

        // Ids are reused, so entries for units that no longer exist must go.
        for (auto it = selfDestructAnnounced.begin(); it != selfDestructAnnounced.end();)
        {
            if (!simulation.units.tryGet(it->first))
            {
                it = selfDestructAnnounced.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void GameScene::updateDefeatNotifications()
    {
        // "Arm forces have been obliterated", in the fallen side and colour.
        // The original keeps a family of these -- "forces have gone to a
        // better place", "vermin have been exterminated" -- but obliterated
        // is the one everybody remembers.
        for (unsigned int i = 0; i < simulation.players.size(); ++i)
        {
            const auto& player = simulation.players[i];
            if (player.status != GamePlayerStatus::Dead || defeatAnnounced.count(i) != 0)
            {
                continue;
            }
            defeatAnnounced.insert(i);

            auto side = player.side;
            std::transform(side.begin() + 1, side.end(), side.begin() + 1, [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            printConsole(side + " forces have been obliterated", playerColorToRgb(player.color));
        }
    }

    namespace
    {
        /**
         * The default track types, decoded from the exe: when the original
         * recognises the game disc it types MCI tracks 1-7 Battle and 8-16
         * Building (0x42F7xx area), and the GOG shim plays music/<n>.mp3 by
         * raw track number. Matching the GOG rips to the tagged soundtrack by
         * duration gives these names. A file the table does not know plays as
         * Building; the title theme -- which the game itself never plays, it
         * belongs to the intro -- is left out entirely.
         */
        bool isBattleTrackName(const std::string& lowerName)
        {
            static const char* const battleNames[] = {
                "brutal battle",
                "fire and ice",
                "attack",
                "warpath",
                "march unto death",
                "ambush in the passage",
            };
            for (const auto* name : battleNames)
            {
                if (lowerName.find(name) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }
    }

    void GameScene::addBattlePoints(int points)
    {
        battlePointsRing[battleRingCursor] += points;
    }

    void GameScene::updateMusic()
    {
        // A scene on its way out has stopped its music and must not start
        // another track: the next scene picks its own.
        if (leavingScene)
        {
            return;
        }

        if (!musicPlaylistBuilt)
        {
            musicPlaylistBuilt = true;
            for (const auto& path : sceneContext.audioService->getMusicPlaylist())
            {
                auto lower = path;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find("theme") != std::string::npos)
                {
                    continue;
                }
                (isBattleTrackName(lower) ? battleTracks : buildingTracks).push_back(path);
            }
            // A one-sided soundtrack plays whatever it has in both moods.
            if (battleTracks.empty())
            {
                battleTracks = buildingTracks;
            }
            if (buildingTracks.empty())
            {
                buildingTracks = battleTracks;
            }
        }
        if (buildingTracks.empty() || !sceneContext.audioService->isMusicEnabled())
        {
            return;
        }

        // The evaluator runs once a game second, like the original's.
        auto second = simulation.gameTime.value / static_cast<unsigned int>(SimTicksPerSecond);
        if (second != lastMusicSecond)
        {
            lastMusicSecond = second;
            battleRingCursor = (battleRingCursor + 1) % battlePointsRing.size();
            battlePointsRing[battleRingCursor] = 0;

            int sum30 = 0;
            for (auto v : battlePointsRing)
            {
                sum30 += v;
            }
            int sum5 = 0;
            for (unsigned int i = 0; i < 5; ++i)
            {
                sum5 += battlePointsRing[(battleRingCursor + battlePointsRing.size() - i) % battlePointsRing.size()];
            }

            if (!musicFadeTarget && simulation.gameTime >= musicLockoutUntil)
            {
                if (musicSituation == MusicSituation::Building)
                {
                    // The unit-count gate is the original's: with thirty or
                    // fewer units the fight is not big enough for war drums.
                    unsigned int owned = 0;
                    for (const auto& [_, unit] : simulation.units)
                    {
                        if (unit.isAlive() && unit.isOwnedBy(localPlayerId))
                        {
                            ++owned;
                        }
                    }
                    if ((sum30 > 50 || sum5 > 30) && owned > 30)
                    {
                        musicFadeTarget = MusicSituation::Battle;
                    }
                }
                else
                {
                    auto inBattleFor = simulation.gameTime.value - battleEnteredTime.value;
                    if (sum30 < 10 && sum5 == 0 && inBattleFor >= 60u * SimTicksPerSecond)
                    {
                        musicFadeTarget = MusicSituation::Building;
                    }
                }
            }
        }

        // A switch fades the old track out over about 1.2 seconds, the
        // original's -vol/18 every other tick.
        if (musicFadeTarget)
        {
            musicFade -= 1.0f / 36.0f;
            if (musicFade <= 0.0f || !sceneContext.audioService->musicPlaying())
            {
                sceneContext.audioService->stopMusic();
                musicSituation = *musicFadeTarget;
                musicFadeTarget = std::nullopt;
                musicFade = 1.0f;
                sceneContext.audioService->setMusicFadeScale(1.0f);
                musicLockoutUntil = simulation.gameTime + GameTime(10u * SimTicksPerSecond);
                musicBag.clear();
                if (musicSituation == MusicSituation::Battle)
                {
                    battleEnteredTime = simulation.gameTime;
                }
                else
                {
                    // Coming down from battle gets four seconds of quiet.
                    musicHoldOffUntil = simulation.gameTime + GameTime(4u * SimTicksPerSecond);
                }
            }
            else
            {
                sceneContext.audioService->setMusicFadeScale(musicFade);
            }
            return;
        }

        if (sceneContext.audioService->musicPlaying() || simulation.gameTime < musicHoldOffUntil)
        {
            return;
        }

        // Draw the next track of the current mood from a bag, so everything
        // of that type plays before anything repeats.
        const auto& tracks = musicSituation == MusicSituation::Battle ? battleTracks : buildingTracks;
        if (musicBag.empty())
        {
            musicBag = tracks;
            for (auto i = musicBag.size(); i > 1; --i)
            {
                std::swap(musicBag[i - 1], musicBag[effectsRng() % i]);
            }
            if (musicBag.size() > 1 && musicBag.back() == lastMusicTrack)
            {
                std::swap(musicBag.back(), musicBag.front());
            }
        }
        if (musicBag.empty())
        {
            return;
        }

        auto next = musicBag.back();
        musicBag.pop_back();
        if (sceneContext.audioService->playMusic(next, false))
        {
            lastMusicTrack = next;
        }
        else
        {
            buildingTracks.erase(std::remove(buildingTracks.begin(), buildingTracks.end(), next), buildingTracks.end());
            battleTracks.erase(std::remove(battleTracks.begin(), battleTracks.end(), next), battleTracks.end());
        }
    }

    void GameScene::updateCloakNotifications()
    {
        // Cloaking and decloaking are the only thing about a cloak the original
        // tells anyone about. `0x48B173` raises notification 0xE the tick the
        // flag comes on and `0x48B1A5` raises 0xF when it goes off; the table
        // at `0x5086E8` pairs those with the sounds `cloak` and `uncloak` and
        // the captions "Cloaked" and "Visible". None of it is a COB event --
        // no shipped script of a cloakable unit has a function for either --
        // and none of it is drawn on the unit. The captions go to the console
        // in the corner, the same place the original prints them; the sounds
        // were parsed and loaded all along and simply never played.
        for (const auto& [unitId, unit] : simulation.units)
        {
            auto wasCloaked = cloakedUnits.find(unitId) != cloakedUnits.end();
            if (unit.cloaked == wasCloaked)
            {
                continue;
            }

            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            if (unit.cloaked)
            {
                cloakedUnits.insert(unitId);
                if (unit.isOwnedBy(localPlayerId))
                {
                    printConsole(unitDefinition.unitName + ": Cloaked");
                }
                playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::Cloak);
            }
            else
            {
                cloakedUnits.erase(unitId);
                if (unit.isOwnedBy(localPlayerId))
                {
                    printConsole(unitDefinition.unitName + ": Visible");
                }
                playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::Uncloak);
            }
        }

        // A unit killed while cloaked leaves its id behind, and ids are reused,
        // so a later unit would start out believed to be cloaked already and
        // never announce itself.
        for (auto it = cloakedUnits.begin(); it != cloakedUnits.end();)
        {
            it = simulation.unitExists(*it) ? std::next(it) : cloakedUnits.erase(it);
        }
    }

    void GameScene::playSoundAt(const Vector3f& /*position*/, const AudioService::SoundHandle& sound)
    {
        // FIXME: should play on a position-aware channel
        auto channel = sceneContext.audioService->playSound(sound);
        if (channel < 0)
        {
            return;
        }
        int volume;
        {
            std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
            playingUnitChannels.insert(channel);
            volume = computeSoundVolume(playingUnitChannels.size());
        }
        sceneContext.audioService->setVolume(channel, volume);
    }

    void GameScene::playWeaponStartSound(const Vector3f& position, const std::string& weaponType)
    {

        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);
        if (weaponMediaInfo.soundStart)
        {
            auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundStart);
            if (sound)
            {
                playSoundAt(position, *sound);
            }
        }
    }

    void GameScene::playWeaponImpactSound(const Vector3f& position, const std::string& weaponType, ImpactType impactType)
    {
        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);
        switch (impactType)
        {
            case ImpactType::Normal:
            {
                if (weaponMediaInfo.soundHit)
                {
                    auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundHit);
                    if (sound)
                    {
                        playSoundAt(position, *sound);
                    }
                }
                break;
            }
            case ImpactType::Water:
            {
                if (weaponMediaInfo.soundWater)
                {
                    auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundWater);
                    if (sound)
                    {
                        playSoundAt(position, *sound);
                    }
                }
                break;
            }
        }
    }

    void GameScene::spawnWeaponImpactExplosion(const Vector3f& position, const std::string& weaponType, ImpactType impactType, bool positionVisible)
    {
        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);
        auto effects = computeWeaponImpactEffects(weaponMediaInfo, impactType, positionVisible);

        if (effects.explosion)
        {
            spawnExplosion(position, *effects.explosion);
        }
        if (effects.smoke)
        {
            createLightSmoke(position);
        }
        if (effects.flash)
        {
            spawnFlash(position);
        }
    }

    void GameScene::onChannelFinished(int channel)
    {
        std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
        playingUnitChannels.erase(channel);
    }

}
