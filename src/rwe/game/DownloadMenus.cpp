#include "DownloadMenus.h"
#include <map>
#include <rwe/game/BuilderGuisDatabase.h>
#include <rwe/util/rwe_string.h>
#include <unordered_map>

namespace rwe
{
    std::vector<DownloadMenuEntry> parseDownloadMenuEntries(const std::vector<TdfBlock>& blocks)
    {
        std::vector<DownloadMenuEntry> entries;
        for (const auto& block : blocks)
        {
            auto builder = block.findValue("UNITMENU");
            auto unitName = block.findValue("UNITNAME");
            auto menu = block.extractInt("MENU");
            auto button = block.extractInt("BUTTON");
            if (!builder || !unitName || !menu || !button)
            {
                continue;
            }
            entries.push_back(DownloadMenuEntry{builder->get(), *menu - 2, *button, unitName->get()});
        }
        return entries;
    }

    bool isBuildSlot(const GuiEntry& entry)
    {
        if (entry.common.id != GuiElementType::Button)
        {
            return false;
        }
        if (toUpper(entry.common.name) == EmptyBuildSlotName)
        {
            return true;
        }
        return (entry.common.attribs & static_cast<int>(GuiButtonAttrib::BehaviorBuildButton)) != 0;
    }

    DownloadMenuResult applyDownloadMenuEntries(std::vector<std::vector<GuiEntry>>& pages, const std::vector<DownloadMenuEntry>& entries)
    {
        DownloadMenuResult result;
        if (pages.empty())
        {
            result.skipped = static_cast<int>(entries.size());
            return result;
        }

        // A page added past the last one the builder ships. Every builder
        // page in the shipped data has the one layout -- the header, two
        // columns of three 64x64 slots, PREV and NEXT, and the order strip
        // -- so the builder's own first page with its slots emptied is that
        // layout, in this builder's side's colours. Its controls and its
        // blank patches all draw from commongui.gaf, so a page with no GAF
        // of its own still draws them.
        auto blankPage = pages.front();
        for (auto& gadget : blankPage)
        {
            if (isBuildSlot(gadget))
            {
                gadget.common.name = EmptyBuildSlotName;
            }
        }

        for (const auto& entry : entries)
        {
            if (entry.page < 0 || entry.slot < 0)
            {
                ++result.skipped;
                continue;
            }
            while (pages.size() <= static_cast<std::size_t>(entry.page))
            {
                pages.push_back(blankPage);
            }

            GuiEntry* target = nullptr;
            int seen = 0;
            for (auto& gadget : pages[entry.page])
            {
                if (!isBuildSlot(gadget))
                {
                    continue;
                }
                if (seen == entry.slot)
                {
                    target = &gadget;
                    break;
                }
                ++seen;
            }
            if (target == nullptr || toUpper(target->common.name) != EmptyBuildSlotName)
            {
                ++result.skipped;
                continue;
            }

            target->common.name = entry.unitName;
            // CORACA2.GUI's empty slots lack the attribute, and it is what
            // draws a build button's queue count where the others draw it.
            target->common.attribs |= static_cast<int>(GuiButtonAttrib::BehaviorBuildButton);
            ++result.placed;
        }
        return result;
    }

    DownloadMenuResult applyDownloadMenuEntries(BuilderGuisDatabase& db, const std::vector<DownloadMenuEntry>& entries)
    {
        std::unordered_map<std::string, std::string> keyByUpperName;
        for (const auto& [name, _] : db.builderGuisMap)
        {
            keyByUpperName.emplace(toUpper(name), name);
        }

        // Grouped by builder, keeping each builder's entries in the order
        // they were read, so that of two entries naming one slot the first
        // keeps it every time.
        std::map<std::string, std::vector<DownloadMenuEntry>> byBuilder;
        for (const auto& entry : entries)
        {
            byBuilder[toUpper(entry.builder)].push_back(entry);
        }

        DownloadMenuResult result;
        for (const auto& [upperName, builderEntries] : byBuilder)
        {
            auto key = keyByUpperName.find(upperName);
            if (key == keyByUpperName.end())
            {
                result.skipped += static_cast<int>(builderEntries.size());
                continue;
            }
            auto r = applyDownloadMenuEntries(db.builderGuisMap.at(key->second), builderEntries);
            result.placed += r.placed;
            result.skipped += r.skipped;
        }
        return result;
    }
}
