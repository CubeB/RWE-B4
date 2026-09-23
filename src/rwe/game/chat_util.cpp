#include "chat_util.h"

#include <iterator>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    std::string sanitizeChatText(const std::string& text)
    {
        // Before anything walks it by code point: a line that came off the
        // wire may be malformed, and the code point walk below throws on
        // malformed input where this does not.
        std::string valid;
        utf8::replace_invalid(text.begin(), text.end(), std::back_inserter(valid), '?');

        std::string out;
        std::size_t length = 0;
        for (auto it = cUtf8Begin(valid), end = cUtf8End(valid); it != end && length < MaxChatMessageLength; ++it)
        {
            auto ch = *it;
            if (ch < 0x20 || ch == 0x7f)
            {
                continue;
            }

            utf8::append(ch, std::back_inserter(out));
            ++length;
        }

        utf8Trim(out);
        return out;
    }

    void chatTextAppend(std::string& text, const std::string& addition)
    {
        std::string valid;
        utf8::replace_invalid(addition.begin(), addition.end(), std::back_inserter(valid), '?');

        auto length = static_cast<std::size_t>(std::distance(cUtf8Begin(text), cUtf8End(text)));
        for (auto it = cUtf8Begin(valid), end = cUtf8End(valid); it != end && length < MaxChatMessageLength; ++it)
        {
            auto ch = *it;
            if (ch < 0x20 || ch == 0x7f)
            {
                continue;
            }

            utf8::append(ch, std::back_inserter(text));
            ++length;
        }
    }

    void chatTextBackspace(std::string& text)
    {
        if (text.empty())
        {
            return;
        }

        auto it = text.end();
        utf8::unchecked::prior(it);
        text.erase(it, text.end());
    }

    std::string formatChatLine(const std::string& senderName, const std::string& text)
    {
        return senderName + ": " + text;
    }
}
