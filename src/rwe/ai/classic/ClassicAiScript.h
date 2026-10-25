#pragma once

#include <string>
#include <vector>
#include <set>

namespace rwe
{
    struct TdfBlock;

    struct ClassicDownloadableScript
    {
        std::string unitName;
        std::string text;
        bool downloadable{false};
    };

    // MOD-AI-02: low-bit Downloadable flag and a maximum 63-byte command string.
    ClassicDownloadableScript parseClassicDownloadableScript(const TdfBlock& unitInfo);
    void promoteDownloadableAiScript(std::vector<ClassicDownloadableScript>& scripts, const std::vector<TdfBlock>& blocks, const std::set<std::string>& loadedTypes);
    /**
     * The original computer player's profile file (`ai/default.txt`, or the
     * mission's `aiprofile`): three directives, `plan`, `weight` and `limit`.
     * TOTALA-EXE-COMPUTER-PLAYER.md S:123; the grammar from the modders' side
     * is TA-COMMUNITY-AI.md section 1.
     *
     * This file is pure text and tables, with no simulation in it, so the
     * lock rules can be tested without building a world.
     */

    /** Which plan a seat plays: Easy -> easy, Standard -> medium, Hard and Brutal -> hard. */
    enum class ClassicPlan
    {
        Easy,
        Medium,
        Hard,
    };

    struct ClassicScriptDirective
    {
        enum class Kind
        {
            Plan,
            Weight,
            Limit,
        };

        Kind kind{Kind::Plan};

        /** Everything after the directive's name, upper-cased. */
        std::vector<std::string> args;
    };

    /**
     * The directives in a profile text, in file order. Case is ignored,
     * comments run from `//` or `#` to the end of the line, and anything that
     * is not one of the three directives is dropped.
     */
    std::vector<ClassicScriptDirective> parseClassicScript(const std::string& text);

    /**
     * `atof` as the profile reader uses it: the longest numeric prefix, zero
     * when there is none. Written out rather than taken from the C library so
     * that a decimal reads to the same double on every toolchain.
     */
    double classicParseNumber(const std::string& text);

    /** What the script needs to know of one unit type: its name and category list, both upper-cased. */
    struct ClassicScriptType
    {
        std::string name;
        std::string category;
    };

    /** One type's row in the strategy table: S:120 "Initial tables". */
    struct ClassicTypeEntry
    {
        /** Percent, 0 to 100. */
        int multiplier{100};

        /** A bound on COMPLETED units; negative is unlimited. */
        int limit{-1};

        bool multiplierLocked{false};
        bool limitLocked{false};
    };

    struct ClassicScriptReport
    {
        /** Names a directive used that matched no type and no category, once each. */
        std::vector<std::string> unknownNames;
    };

    /**
     * Runs the directives over the table, for one plan.
     *
     * `plan` disables the weight and limit directives and then re-enables them
     * for a matching `easy`, `medium`, `hard` or `any`; directives before the
     * first `plan` always run. `weight <x> <n>` scales the multiplier by n,
     * truncating and clamping to [0, 100], unless that entry's multiplier is
     * locked; `limit <x> <n>` writes the limit unless its own lock holds. A
     * line with a subject and no number takes the number as zero, so
     * `limit armsabo` forbids the type; a negative limit other than -1 stays
     * negative and blocks it too (BT-128). `any` counts only as the first word
     * after `plan`.
     *
     * `x` names a unit type, which affects that one type and LOCKS what it
     * wrote, or a category, which affects every type carrying it and does not
     * lock. Nothing special-cases "the first occurrence wins": it is what the
     * lock does to a repeated name, and a category rule can still land on
     * anything a name has not claimed.
     *
     * `entries` must be the same length as `types`.
     */
    ClassicScriptReport applyClassicScript(
        const std::vector<ClassicScriptDirective>& directives,
        ClassicPlan plan,
        const std::vector<ClassicScriptType>& types,
        std::vector<ClassicTypeEntry>& entries,
        bool* commandGate = nullptr);

    // MOD-AI-02: distinct owner-weight-lock and owner-limit-lock passes,
    // each enabling the command gate once, in case-insensitive unit-name order.
    ClassicScriptReport applyClassicDownloadableScripts(
        const std::vector<ClassicDownloadableScript>& scripts, ClassicPlan plan,
        const std::vector<ClassicScriptType>& types, std::vector<ClassicTypeEntry>& entries);
}
