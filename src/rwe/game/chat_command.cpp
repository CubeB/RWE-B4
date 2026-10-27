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

    std::optional<ChatCommand> parseChatCommand(const std::string& line)
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
            command.kind = ChatCommand::Kind::SetShareMetal;
            command.number = chatAtoi(argument(1));
        }
        else if (word == "setshareenergy")
        {
            command.kind = ChatCommand::Kind::SetShareEnergy;
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
