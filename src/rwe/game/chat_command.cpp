#include "chat_command.h"

#include <cctype>
#include <climits>
#include <vector>

namespace rwe
{
    namespace
    {
        /** The interpreter's token buffer: more than this many words are not read. */
        constexpr std::size_t MaxTokens = 20;

        bool isSpace(char c)
        {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
        }

        std::string lowered(const std::string& s)
        {
            std::string out = s;
            for (auto& c : out)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        std::vector<std::string> tokenise(const std::string& text)
        {
            std::vector<std::string> tokens;
            std::string current;
            for (auto c : text)
            {
                // A comment runs to the end of the line, even from the middle
                // of a word.
                if (c == '#')
                {
                    break;
                }

                if (isSpace(c))
                {
                    if (!current.empty())
                    {
                        tokens.push_back(std::move(current));
                        current.clear();
                    }
                    continue;
                }

                current.push_back(c);
            }

            if (!current.empty())
            {
                tokens.push_back(std::move(current));
            }

            if (tokens.size() > MaxTokens)
            {
                tokens.resize(MaxTokens);
            }

            return tokens;
        }

        /**
         * A poster side, read strictly: decimal digits only, no sign, no
         * trailing text, and positive. An explicit capture size is not the
         * original's unvalidated `atoi` grammar -- this is presentation, and a
         * typo should be refused rather than silently read as a prefix.
         */
        std::optional<unsigned int> parsePosterDimension(const std::string& token)
        {
            if (token.empty())
            {
                return std::nullopt;
            }

            unsigned long long value = 0;
            for (auto c : token)
            {
                if (c < '0' || c > '9')
                {
                    return std::nullopt;
                }
                value = (value * 10) + static_cast<unsigned long long>(c - '0');
                if (value > static_cast<unsigned long long>(INT_MAX))
                {
                    return std::nullopt;
                }
            }

            if (value == 0)
            {
                return std::nullopt;
            }
            return static_cast<unsigned int>(value);
        }
    }

    int chatAtoi(const std::string& token)
    {
        std::size_t i = 0;
        while (i < token.size() && isSpace(token[i]))
        {
            ++i;
        }

        bool negative = false;
        if (i < token.size() && (token[i] == '+' || token[i] == '-'))
        {
            negative = token[i] == '-';
            ++i;
        }

        // Accumulated negatively so that INT_MIN is reachable, and saturated.
        long long value = 0;
        for (; i < token.size() && token[i] >= '0' && token[i] <= '9'; ++i)
        {
            value = value * 10 + (token[i] - '0');
            if (value > static_cast<long long>(INT_MAX) + 1)
            {
                value = static_cast<long long>(INT_MAX) + 1;
            }
        }

        if (negative)
        {
            return value >= static_cast<long long>(INT_MAX) + 1 ? INT_MIN : static_cast<int>(-value);
        }
        return value > INT_MAX ? INT_MAX : static_cast<int>(value);
    }

    std::optional<ChatCommand> parseChatCommand(const std::string& line, bool communityPatch)
    {
        std::size_t start = 0;
        while (start < line.size() && isSpace(line[start]))
        {
            ++start;
        }

        if (start >= line.size() || line[start] != '+')
        {
            return std::nullopt;
        }

        // What follows the plus is handed to the interpreter, so "+ ShareMetal"
        // is the same as "+ShareMetal".
        auto tokens = tokenise(line.substr(start + 1));

        ChatCommand command;
        if (tokens.empty())
        {
            command.kind = ChatCommand::Kind::Empty;
            return command;
        }

        command.word = tokens[0];
        auto word = lowered(tokens[0]);
        auto argument = [&](std::size_t i) -> std::string {
            return i < tokens.size() ? tokens[i] : std::string();
        };

        if (word == "sharemetal")
        {
            command.kind = ChatCommand::Kind::ShareMetal;
        }
        else if (word == "shareenergy")
        {
            command.kind = ChatCommand::Kind::ShareEnergy;
        }
        else if (word == "shareall")
        {
            command.kind = ChatCommand::Kind::ShareAll;
        }
        else if (word == "sharemapping")
        {
            command.kind = ChatCommand::Kind::ShareMapping;
        }
        else if (word == "shareradar")
        {
            command.kind = ChatCommand::Kind::ShareRadar;
        }
        else if (word == "setsharemetal")
        {
            auto value = argument(1);
            command.kind = ChatCommand::Kind::SetShareMetal;
            command.number = chatAtoi(value);
            command.percent = !value.empty() && value.back() == '%';
        }
        else if (word == "setshareenergy")
        {
            auto value = argument(1);
            command.kind = ChatCommand::Kind::SetShareEnergy;
            command.number = chatAtoi(value);
            command.percent = !value.empty() && value.back() == '%';
        }
        else if (communityPatch && (word == "setreclaimmetal" || word == "setreclaimenergy"))
        {
            // TADR 2026.10.6: `<0-100>[%]`, and no value at all prints the
            // current level rather than meaning zero. `%` is read by `atoi`
            // as the first non-digit and simply ends the number.
            command.kind = word == "setreclaimmetal"
                ? ChatCommand::Kind::SetReclaimMetal
                : ChatCommand::Kind::SetReclaimEnergy;
            command.hasArgument = tokens.size() >= 2;
            command.number = chatAtoi(argument(1));
        }
        else if (word == "now")
        {
            // #906: exactly six tokens, the command word case-insensitive and
            // the five after it case-sensitive. Any other form clears the
            // unlock; that is still a successful parse, not an unknown word.
            command.kind = ChatCommand::Kind::Now;
            command.developerPhraseAccepted = tokens.size() == 6
                && tokens[1] == "Film" && tokens[2] == "Chris" && tokens[3] == "Include"
                && tokens[4] == "Reload" && tokens[5] == "Assert";
        }
        else if (word == "noshake")
        {
            // #907: UI-option bit 4 in the original; a preference, not saved.
            command.kind = ChatCommand::Kind::NoShake;
        }
        else if (word == "gamma")
        {
            // #907, BT-GAME-AUDIO-OPTION-COMMANDS-FINDINGS §2.2: argument 1,
            // absent 0; brightness is the raw integer x 0.1.
            command.kind = ChatCommand::Kind::Gamma;
            command.number = chatAtoi(argument(1));
        }
        else if (word == "scrollspeed")
        {
            // #907 §2.3: argument 1, absent 0, pixels per simulation tick.
            command.kind = ChatCommand::Kind::ScrollSpeed;
            command.number = chatAtoi(argument(1));
        }
        else if (word == "musicmode")
        {
            // #908 §1.4: the argument is a situation category, absent 0.
            command.kind = ChatCommand::Kind::MusicMode;
            command.number = chatAtoi(argument(1));
        }
        else if (word == "sing")
        {
            // #908 §1.5: a no-argument toggle of the unit-speech waveform.
            command.kind = ChatCommand::Kind::Sing;
        }
        else if (word == "antialias")
        {
            command.kind = ChatCommand::Kind::AntiAlias;
        }
        else if (word == "shadow")
        {
            command.kind = ChatCommand::Kind::Shadow;
        }
        else if (word == "shading")
        {
            command.kind = ChatCommand::Kind::Shading;
        }
        else if (word == "tshadow")
        {
            command.kind = ChatCommand::Kind::TShadow;
        }
        else if (word == "fshadow")
        {
            // #907 §2.8: visual bit 4, the feature/scenery sub-bit under
            // Shadow; a toggle with no argument.
            command.kind = ChatCommand::Kind::FShadow;
        }
        else if (word == "iface")
        {
            // #907 §2.4: argument 1, absent 0; 0 is the left-click interface.
            command.kind = ChatCommand::Kind::IFace;
            command.number = chatAtoi(argument(1));
        }
        else if (word == "nometal" || word == "noenergy")
        {
            // #910, BT-GAME-CONSOLE-COMMANDS-PARITY §5: the lone command token
            // targets the local player with amount zero; any extra token names
            // the player (zero-based slot) and a second is the amount.
            command.kind = word == "nometal" ? ChatCommand::Kind::NoMetal : ChatCommand::Kind::NoEnergy;
            if (tokens.size() >= 2)
            {
                command.playerNumber = chatAtoi(tokens[1]);
            }
            command.number = tokens.size() >= 3 ? chatAtoi(tokens[2]) : 0;
        }
        else if (word == "atm")
        {
            // #912, §6: a cheat-table grant of 1000 metal and energy to the
            // viewed player; RWE has no divergent view, so it is the local one.
            command.kind = ChatCommand::Kind::Atm;
        }
        else if (word == "makeposter")
        {
            // #915, BT-GAME-CONSOLE-COMMANDS-PARITY section 6: a cheat-table
            // command. No argument is the original's 3200x2400 and `all` is
            // the map's pixel size. The two-number form is the natural reading
            // of "each dimension"; the finding does not spell out the grammar,
            // so anything else is refused rather than guessed at.
            command.kind = ChatCommand::Kind::MakePoster;
            if (tokens.size() == 2 && lowered(tokens[1]) == "all")
            {
                command.posterAll = true;
            }
            else if (tokens.size() == 3)
            {
                auto width = parsePosterDimension(tokens[1]);
                auto height = parsePosterDimension(tokens[2]);
                if (!width || !height)
                {
                    command.kind = ChatCommand::Kind::MakePosterUsage;
                }
                else
                {
                    command.posterWidth = *width;
                    command.posterHeight = *height;
                }
            }
            else if (tokens.size() != 1)
            {
                command.kind = ChatCommand::Kind::MakePosterUsage;
            }
        }
        else if (word == "bps")
        {
            // #911, §6: a local diagnostic toggle, mask one; no message.
            command.kind = ChatCommand::Kind::Bps;
        }
        else if (word == "screenchat")
        {
            // #907 §2.5: a local display bit, XOR with one.
            command.kind = ChatCommand::Kind::ScreenChat;
        }
        else if (word == "clock")
        {
            // #907 §2.1: UI-option bit 6, toggled; the original saves it.
            command.kind = ChatCommand::Kind::Clock;
        }
        else if (word == "switchalt")
        {
            // #907 §2.7: no argument toggles the one bit and saves; with an
            // argument the bit is assigned and not saved. The presence of the
            // argument matters, so a bare `+SwitchAlt` is not `+SwitchAlt 0`.
            command.kind = ChatCommand::Kind::SwitchAlt;
            command.hasArgument = tokens.size() >= 2;
            command.number = chatAtoi(argument(1));
        }
        else if (word == "give")
        {
            // player amount metal|energy. The resource is the one argument
            // with no default to fall back on, so a line without one is
            // refused rather than guessed at.
            auto resource = lowered(argument(3));
            if (tokens.size() < 4 || (resource != "metal" && resource != "energy"))
            {
                command.kind = ChatCommand::Kind::GiveUsage;
            }
            else
            {
                command.kind = ChatCommand::Kind::Give;
                command.playerToken = tokens[1];
                command.number = chatAtoi(tokens[2]);
                command.giveMetal = resource == "metal";
            }
        }
        else
        {
            command.kind = ChatCommand::Kind::Unknown;
            // The original's default handler spawns a unit named by the
            // command word and takes the owner from argument one; keep the
            // token here so the handler can do that when debug is unlocked.
            if (tokens.size() >= 2)
            {
                command.playerNumber = chatAtoi(tokens[1]);
            }
        }

        return command;
    }
}

namespace rwe
{
    std::string shareNoticeMessage(ChatCommand::Kind kind)
    {
        if (kind == ChatCommand::Kind::ShareRadar)
        {
            return "ShareRadar: not available in RWE. Radar and sonar are not shared with allies.";
        }

        return "ShareMapping: there is no toggle in RWE. Allies always share what they see.";
    }

    unsigned int chatCommandMask(bool debugUnlocked, bool campaign, bool multiplayer, bool cheatCodesEnabled)
    {
        // The original computes this per submitted line: 7 once debug is
        // unlocked (which also grants the cheat bit), otherwise 1 in a
        // campaign and 3 in a skirmish, and in multiplayer the host's Cheat
        // Codes option decides between 1 and 3 (#906).
        if (debugUnlocked)
        {
            return ChatCommandMaskOrdinary | ChatCommandMaskCheat | ChatCommandMaskDebug;
        }
        if (campaign)
        {
            return ChatCommandMaskOrdinary;
        }
        if (multiplayer && !cheatCodesEnabled)
        {
            return ChatCommandMaskOrdinary;
        }
        return ChatCommandMaskOrdinary | ChatCommandMaskCheat;
    }
}
