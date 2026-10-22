#include "MapSelectionLayout.h"

#include <rwe/ui/UiLabel.h>
#include <algorithm>

namespace rwe
{
    void layoutMapSelectionPanel(UiPanel& panel)
    {
        auto description = panel.find<UiLabel>("DESCRIPTION");
        auto size = panel.find<UiLabel>("SIZE");
        if (!description || !size)
        {
            return;
        }

        // MAP-DESCRIPTION-LAYOUT-FINDINGS MAP-LAYOUT-01/02: enlarge the
        // data-defined box, measuring the full text before allocating space.
        auto& d = description->get();
        auto& s = size->get();
        d.setX(24);
        s.setX(24);
        d.setWidth(panel.getWidth() - 48);
        s.setWidth(d.getWidth());
        d.setSingleLine(false);
        s.setSingleLine(false);
        s.setHeight(s.getTextHeight());
        s.setY(static_cast<int>(panel.getHeight() - s.getHeight()) - 48);
        const auto descriptionBottom = s.getY() - 10;
        const auto maximumHeight = static_cast<unsigned int>(std::max(75, descriptionBottom - 124));
        d.setHeight(std::clamp(d.getTextHeight(), 75u, maximumHeight));
        d.setY(descriptionBottom - static_cast<int>(d.getHeight()));
        d.setScrollOverflow(true);

        const auto listHeight = static_cast<unsigned int>(std::max(60, d.getY() - 66));
        for (const auto& name : {"MAPNAMES", "SLIDER"})
        {
            if (auto control = panel.find<UiComponent>(name))
            {
                control->get().setX(std::string(name) == "SLIDER" ? 328 : 24);
                control->get().setY(42);
                control->get().setHeight(listHeight);
                control->get().setWidth(std::string(name) == "SLIDER" ? 16 : 292);
            }
        }
        if (auto preview = panel.find<UiComponent>("MAPPIC"))
        {
            preview->get().setX(368);
            preview->get().setY(42);
            preview->get().setWidth(200);
            preview->get().setHeight(std::min(200u, listHeight));
        }
        if (auto caption = panel.find<UiLabel>("DESCRIPTION_CAPTION"))
        {
            caption->get().setY(d.getY() - 20);
            caption->get().setText(d.getTextHeight() > d.getHeight()
                ? "Description (click text below, then scroll for more)" : "Description");
        }
        for (const auto& name : {"LOAD", "PREVMENU"})
        {
            if (auto button = panel.find<UiComponent>(name))
            {
                button->get().setX(std::string(name) == "LOAD" ? 356 : 468);
                button->get().setY(static_cast<int>(panel.getHeight()) - 32);
            }
        }
    }
}
