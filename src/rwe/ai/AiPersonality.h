#pragma once

#include <filesystem>
#include <optional>
#include <rwe/ai/AiEngine.h>
#include <rwe/ai/AiTuningProfile.h>
#include <string>
#include <utility>
#include <vector>

namespace rwe
{
    /**
     * A named way for a computer player to play, chosen for each computer
     * player before a skirmish: a set of AiTuningProfile knob settings laid
     * over its difficulty's profile and its faction's defaults, and under any
     * --ai-tune, so an arena run can still set a knob back.
     *
     * Some come built in -- "Original TA", which is the original's own
     * computer player and the default, then "Balanced", "Rush", "Turtle",
     * "Tech Rush", and "Easy", "Medium" and "Hard", which set the difficulty
     * and nothing else -- and more can be written as text files in the local
     * data directory's `ai` folder, one per personality, a file replacing a
     * built-in of the same name:
     *
     *     # A comment.
     *     name=Raider
     *     description=Raids from the first minute and never builds a tower.
     *     engine=rwe
     *     difficulty=standard
     *     attackArmySize=4
     *     targetDefenceCount=0
     *
     * `engine` is `classic` or `rwe` and is optional: a personality is RWE's
     * unless it says otherwise, so every file written before the choice
     * existed plays as it did (ADR-0005). The classic engine has no knobs, so
     * a classic personality that lists one is refused; it can still name a
     * difficulty. Every other line is a knob, by the names applyAiTuning
     * takes, and means something only to the RWE engine. A
     * personality is played, not replayed: a replay idles its computer
     * players and plays their recorded commands back, so it needs no record
     * of which personality made them. A saved game does, and keeps one.
     */
    struct AiPersonality
    {
        std::string name;
        std::string description;
        /** The difficulty the personality plays at, if it names one; otherwise the game's. */
        std::optional<AiDifficulty> difficulty;
        std::vector<std::pair<std::string, std::string>> knobs;
        /** Which computer player plays it. RWE's unless the file or the built-in says otherwise. */
        AiEngine engine{AiEngine::Rwe};
    };

    /** The personalities RWE ships, Original TA first. */
    const std::vector<AiPersonality>& builtInAiPersonalities();

    /**
     * Reads one personality file. `fallbackName` names it when the file does
     * not. Throws std::runtime_error naming the line for a line that is not
     * key=value, a difficulty or an engine it does not know, or a knob
     * applyAiTuning does not take -- a misspelt knob that silently did
     * nothing would make the personality look like it worked -- and for a
     * knob on a classic personality, which has none to set.
     */
    AiPersonality parseAiPersonality(const std::string& text, const std::string& fallbackName);

    /**
     * The built-in personalities followed by every `*.txt` file in
     * `directory`, in name order, a file replacing a built-in of the same
     * name. A file that will not parse is skipped and logged, so one bad file
     * does not take the skirmish screen down with it.
     */
    std::vector<AiPersonality> loadAiPersonalities(const std::optional<std::filesystem::path>& directory);

    /** By name, ignoring case. */
    std::optional<AiPersonality> findAiPersonality(const std::vector<AiPersonality>& personalities, const std::string& name);

    /**
     * Lays its knobs over `profile`, in order. Returns the first knob
     * applyAiTuning does not take, if any. Only meaningful for an RWE
     * personality: the classic engine has no AiTuningProfile knobs to set, and
     * a seat that plays it never builds a profile to lay them over.
     */
    std::optional<std::string> applyAiPersonality(AiTuningProfile& profile, const AiPersonality& personality);

    /** Where personality files live: `<local data>/ai`. */
    std::optional<std::filesystem::path> aiPersonalityDirectory();

    /** The difficulty names a personality file may give, as --ai-difficulty takes them. */
    std::optional<AiDifficulty> aiDifficultyFromName(const std::string& name);
}
