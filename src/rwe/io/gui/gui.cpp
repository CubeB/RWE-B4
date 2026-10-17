#include "gui.h"

#include <algorithm>
#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/optional_util.h>
#include <rwe/util/SimpleLogger.h>
#include <stdexcept>

namespace rwe
{
    GuiParseException::GuiParseException(const std::string& message) : runtime_error(message)
    {
    }

    GuiParseException::GuiParseException(const char* message) : runtime_error(message)
    {
    }

    std::optional<GuiElementType> toGuiElementType(int value)
    {
        switch (value)
        {
            case static_cast<int>(GuiElementType::Panel):
            case static_cast<int>(GuiElementType::Button):
            case static_cast<int>(GuiElementType::ListBox):
            case static_cast<int>(GuiElementType::TextBox):
            case static_cast<int>(GuiElementType::ScrollBar):
            case static_cast<int>(GuiElementType::Label):
            case static_cast<int>(GuiElementType::Surface):
            case static_cast<int>(GuiElementType::Font):
            case static_cast<int>(GuiElementType::PictureBox):
                return static_cast<GuiElementType>(value);
            default:
                return std::nullopt;
        }
    }

    std::optional<GuiElementType> extractGuiElementType(const TdfBlock& block, const std::string& key)
    {
        auto value = block.extractInt(key);
        if (!value)
        {
            return std::nullopt;
        }

        return toGuiElementType(*value);
    }

    GuiElementType expectGuiElementType(const TdfBlock& block, const std::string& key)
    {
        auto v = extractGuiElementType(block, key);
        if (!v)
        {
            throw GuiParseException("Failed to read GUI element type from key: " + key);
        }

        return *v;
    }

    namespace
    {
        /**
         * The original keeps a gadget's position and size in a short and
         * refuses a layout larger than its screen. Unchecked, a negative
         * size became about four billion in the component constructors and a
         * huge one was accepted, and every loop that tiles art across a gadget
         * ran that many times (#664). No shipped gadget is larger than 640 by
         * 480; 2048 leaves room for a mod's larger screen and still bounds
         * every such loop.
         */
        constexpr int MaxGadgetExtent = 2048;
        constexpr int MaxGadgetPosition = 32767;

        /** `stages` is a byte in the original; each stage is a label and a sprite. */
        constexpr int MaxGadgetStages = 255;

        int clampField(int value, int lo, int hi, const std::string& gadget, const char* key)
        {
            if (value < lo || value > hi)
            {
                auto clamped = std::clamp(value, lo, hi);
                LOG_WARN << "gui gadget '" << gadget << "': " << key << "=" << value << " is out of range; using " << clamped;
                return clamped;
            }
            return value;
        }
    }

    std::optional<GuiEntry> parseGuiEntry(const TdfBlock& e)
    {
        GuiEntry g;

        auto commonOption = e.findBlock("COMMON");
        if (!commonOption)
        {
            LOG_WARN << "gui: a block has no COMMON section; the file is refused";
            return std::nullopt;
        }

        auto common = &commonOption->get();

        // A gadget with no readable type cannot be built, so the file is
        // refused; every other missing key takes the default the original
        // gives it (zero, or empty) -- see #663.
        auto id = extractGuiElementType(*common, "id");
        if (!id)
        {
            LOG_WARN << "gui: a gadget has a missing or unknown type (id); the file is refused";
            return std::nullopt;
        }
        g.common.id = *id;
        g.common.assoc = common->extractInt("assoc").value_or(0);
        g.common.name = refToCopy(common->findValue("name")).value_or("");
        g.common.xpos = clampField(common->extractInt("xpos").value_or(0), -MaxGadgetPosition - 1, MaxGadgetPosition, g.common.name, "xpos");
        g.common.ypos = clampField(common->extractInt("ypos").value_or(0), -MaxGadgetPosition - 1, MaxGadgetPosition, g.common.name, "ypos");
        g.common.width = clampField(common->extractInt("width").value_or(0), 0, MaxGadgetExtent, g.common.name, "width");
        g.common.height = clampField(common->extractInt("height").value_or(0), 0, MaxGadgetExtent, g.common.name, "height");

        // "attribs" and "colorf" should be present in all gadgets,
        // but ARMGEN.GUI contains a gadget whose "attribs" property
        // is missing a semicolon.
        // This causes the parser to consume the next property, "colorf",
        // as the value of "attribs", making both invalid.
        // We explicitly provide defaults here to get around that issue.
        // Probably we should provide default values
        // for all the keys here, but I don't want to go through the effort
        // of deciding on reasonable defaults.
        g.common.attribs = common->extractInt("attribs").value_or(0);
        g.common.colorf = common->extractInt("colorf").value_or(0);

        g.common.colorb = common->extractInt("colorb").value_or(0);
        g.common.textureNumber = common->extractInt("texturenumber").value_or(0);
        g.common.fontNumber = common->extractInt("fontnumber").value_or(0);
        g.common.active = common->extractBool("active").value_or(false);
        g.common.commonAttribs = common->extractInt("commonattribs").value_or(0);
        g.common.help = refToCopy(common->findValue("help"));

        g.panel = refToCopy(e.findValue("panel"));
        g.crDefault = refToCopy(e.findValue("crdefault"));
        g.escdefault = refToCopy(e.findValue("escdefault"));
        g.defaultFocus = refToCopy(e.findValue("defaultfocus"));

        g.totalGadgets = e.extractInt("totalgadgets");

        auto versionOption = e.findBlock("VERSION");
        if (versionOption)
        {
            auto version = &versionOption->get();

            auto major = version->extractInt("major").value_or(0);
            auto minor = version->extractInt("minor").value_or(0);
            auto revision = version->extractInt("revision").value_or(0);
            g.version = GuiVersion(major, minor, revision);
        }

        g.status = e.extractInt("status");
        g.text = refToCopy(e.findValue("text"));
        g.quickKey = e.extractInt("quickkey");
        g.grayedOut = e.extractBool("grayedout");
        g.stages = e.extractInt("stages");
        if (g.stages)
        {
            g.stages = clampField(*g.stages, 0, MaxGadgetStages, g.common.name, "stages");
        }
        g.maxChars = e.extractInt("maxchars");

        return g;
    }

    std::optional<std::vector<GuiEntry>> parseGui(const std::vector<TdfBlock>& tdf)
    {
        std::vector<GuiEntry> entries;

        // The original allocates 200 records and the largest shipped file has
        // 44. A 16 MB file of minimal records otherwise builds millions of
        // components (#666).
        if (tdf.size() > MaxGuiRecords)
        {
            LOG_WARN << "gui: " << tdf.size() << " records; keeping the first " << MaxGuiRecords;
        }

        // Nothing here may throw: this returns an optional so that a bad file
        // costs the panel built from it and not the game (#663).
        try
        {
            for (const auto& block : tdf)
            {
                if (entries.size() >= MaxGuiRecords)
                {
                    break;
                }

                auto guiEntry = parseGuiEntry(block);
                if (!guiEntry)
                {
                    return std::nullopt;
                }

                entries.push_back(*guiEntry);
            }
        }
        catch (const std::exception& e)
        {
            LOG_WARN << "gui: unreadable record (" << e.what() << "); the file is refused";
            return std::nullopt;
        }

        // The first record is the panel; every caller indexes it (#662).
        if (entries.empty())
        {
            LOG_WARN << "gui: the file holds no records; it is refused";
            return std::nullopt;
        }

        return entries;
    }

    std::optional<std::vector<GuiEntry>> parseGuiFromBytes(const std::vector<char>& bytes)
    {
        try
        {
            return parseGui(parseListTdfFromBytes(bytes));
        }
        catch (const std::exception& e)
        {
            LOG_WARN << "gui: unreadable file (" << e.what() << ")";
            return std::nullopt;
        }
    }

    bool GuiEntry::operator==(const GuiEntry& rhs) const
    {
        return common == rhs.common
            && panel == rhs.panel
            && crDefault == rhs.crDefault
            && escdefault == rhs.escdefault
            && defaultFocus == rhs.defaultFocus
            && totalGadgets == rhs.totalGadgets
            && version == rhs.version
            && status == rhs.status
            && text == rhs.text
            && quickKey == rhs.quickKey
            && grayedOut == rhs.grayedOut
            && stages == rhs.stages
            && maxChars == rhs.maxChars;
    }

    bool GuiEntry::operator!=(const GuiEntry& rhs) const
    {
        return !(rhs == *this);
    }
}
