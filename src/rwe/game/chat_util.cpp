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

    ChatLabel chatLabelFromWire(std::uint32_t value)
    {
        switch (value)
        {
            case 1:
                return ChatLabel::Allies;
            case 2:
                return ChatLabel::Enemies;
            default:
                return ChatLabel::None;
        }
    }

    const char* chatLabelName(ChatLabel label)
    {
        switch (label)
        {
            case ChatLabel::Allies:
                return "Allies";
            case ChatLabel::Enemies:
                return "Enemies";
            default:
                return "";
        }
    }

    void ChatSendMode::toggleSlot(unsigned int slot)
    {
        if (slot < 32)
        {
            customSlots ^= (std::uint32_t(1) << slot);
        }
    }

    ChatAddress ChatSendMode::address() const
    {
        ChatAddress a;
        switch (mode)
        {
            case 1:
                a.kind = ChatAddress::Kind::Allies;
                break;
            case 2:
                a.kind = ChatAddress::Kind::NonAllies;
                break;
            case 3:
                a.kind = ChatAddress::Kind::Players;
                a.slots = customSlots;
                break;
            default:
                break;
        }
        return a;
    }

    ChatLabel ChatSendMode::label() const
    {
        switch (mode)
        {
            case 1:
                return ChatLabel::Allies;
            case 2:
                return ChatLabel::Enemies;
            default:
                return ChatLabel::None;
        }
    }

    ChatRoute routeChatLine(const std::string& typed, const ChatSendMode& standing)
    {
        ChatRoute route;
        route.address = standing.address();
        route.label = standing.label();
        route.text = typed;

        // Leading spaces are skipped before the prefix is looked for.
        std::size_t start = 0;
        while (start < typed.size() && typed[start] == ' ')
        {
            ++start;
        }

        if (typed.size() >= start + 2)
        {
            auto first = typed[start];
            auto second = typed[start + 1];
            if (second == ',' || second == ':' || second == ';')
            {
                if (first >= '0' && first <= '9')
                {
                    route.address = ChatAddress{ChatAddress::Kind::Players, std::uint32_t(1) << (first - '0')};
                    route.label = ChatLabel::None;
                    route.text = typed.substr(start + 2);
                }
                else if (first == 'A' || first == 'a')
                {
                    route.address = ChatAddress{ChatAddress::Kind::Allies, 0};
                    route.label = ChatLabel::Allies;
                    route.text = typed.substr(start + 2);
                }
                else if (first == 'E' || first == 'e')
                {
                    route.address = ChatAddress{ChatAddress::Kind::NonAllies, 0};
                    route.label = ChatLabel::Enemies;
                    route.text = typed.substr(start + 2);
                }
            }
        }

        utf8Trim(route.text);
        return route;
    }

    std::vector<unsigned int> chatRecipients(
        const ChatAddress& address,
        const std::vector<unsigned int>& peers,
        unsigned int localSlot,
        const std::function<bool(unsigned int, unsigned int)>& allied)
    {
        std::vector<unsigned int> out;
        for (auto peer : peers)
        {
            if (peer == localSlot)
            {
                continue;
            }

            bool wanted = false;
            switch (address.kind)
            {
                case ChatAddress::Kind::Everyone:
                    wanted = true;
                    break;
                case ChatAddress::Kind::Allies:
                    wanted = allied(localSlot, peer);
                    break;
                case ChatAddress::Kind::NonAllies:
                    wanted = !allied(localSlot, peer);
                    break;
                case ChatAddress::Kind::Players:
                    wanted = peer < 32 && ((address.slots >> peer) & 1u) != 0;
                    break;
            }

            if (wanted)
            {
                out.push_back(peer);
            }
        }
        return out;
    }

    std::string capChatForWire(const std::string& senderName, ChatLabel label, const std::string& text)
    {
        std::string valid;
        utf8::replace_invalid(text.begin(), text.end(), std::back_inserter(valid), '?');

        std::string head = "<" + senderName;
        if (label != ChatLabel::None)
        {
            head += std::string("->") + chatLabelName(label);
        }
        head += "> ";

        auto headLength = static_cast<std::size_t>(std::distance(cUtf8Begin(head), cUtf8End(head)));
        if (headLength >= MaxChatWireLineLength)
        {
            return std::string();
        }

        auto budget = MaxChatWireLineLength - headLength;
        std::string out;
        std::size_t length = 0;
        for (auto it = cUtf8Begin(valid), end = cUtf8End(valid); it != end && length < budget; ++it)
        {
            utf8::append(*it, std::back_inserter(out));
            ++length;
        }

        utf8Trim(out);
        return out;
    }

    std::string formatChatLine(const std::string& senderName, ChatLabel label, const std::string& text)
    {
        if (label == ChatLabel::None)
        {
            return formatChatLine(senderName, text);
        }

        return senderName + "->" + chatLabelName(label) + ": " + text;
    }
}
