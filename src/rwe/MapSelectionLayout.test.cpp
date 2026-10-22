#include <catch2/catch_test_macros.hpp>
#include <rwe/MapSelectionLayout.h>
#include <rwe/ui/UiLabel.h>
#include <rwe/ui/UiSurface.h>

namespace rwe
{
    namespace
    {
        std::shared_ptr<SpriteSeries> mapFont()
        {
            auto font = std::make_shared<SpriteSeries>();
            for (unsigned int ch = 0; ch < 256; ++ch)
            {
                font->sprites.push_back(std::make_shared<Sprite>(
                    Rectangle2f::fromTopLeft(0, -11, ch == ' ' ? 4 : ch == 'W' ? 11 : 7, 12),
                    SharedTextureHandle(), nullptr));
            }
            return font;
        }
    }

    TEST_CASE("map selector grows the description without hiding its size or controls", "[ui][map-layout]")
    {
        auto font = mapFont();
        UiPanel panel(20, 20, 600, 440);
        for (const auto& name : {"DESCRIPTION", "SIZE", "DESCRIPTION_CAPTION"})
        {
            auto label = std::make_unique<UiLabel>(60, 300, 235, 31, "", font);
            label->setName(name);
            panel.appendChild(std::move(label));
        }
        for (const auto& name : {"MAPNAMES", "SLIDER", "MAPPIC", "LOAD", "PREVMENU"})
        {
            auto control = std::make_unique<UiSurface>(0, 0, 96, 20);
            control->setName(name);
            panel.appendChild(std::move(control));
        }
        auto& description = panel.find<UiLabel>("DESCRIPTION")->get();
        auto& size = panel.find<UiLabel>("SIZE")->get();
        size.setText("64 mb  Players: 2");

        SECTION("real shipped Coast To Coast description")
        {
            // MAP-LAYOUT-02, read from totala2.hpi, maps/Coast To Coast.ota.
            description.setText("7 X 4  Water between two coasts. Weak tides.");
        }
        SECTION("a description much longer than the original two-row box")
        {
            std::string text;
            for (unsigned int i = 0; i < 12; ++i)
            {
                text += "Wide rivers divide the battlefield. Watch the approaches. ";
            }
            description.setText(text);
            size.setText("A memory and player summary that also needs room to wrap onto another row.");
        }
        SECTION("explicit breaks and long unbroken words")
        {
            description.setText("First line\r\nSecond line\n" + std::string(150, 'W'));
        }
        layoutMapSelectionPanel(panel);

        CHECK(description.getWidth() > 235);
        CHECK(description.getHeight() >= description.getTextHeight());
        CHECK(size.getHeight() >= size.getTextHeight());
        CHECK(description.getY() + static_cast<int>(description.getHeight()) < size.getY());
        CHECK(size.getY() + static_cast<int>(size.getHeight()) < panel.find<UiComponent>("LOAD")->get().getY());
        CHECK(panel.find<UiComponent>("MAPNAMES")->get().getY()
            + static_cast<int>(panel.find<UiComponent>("MAPNAMES")->get().getHeight()) < description.getY() - 20);

        // Check that allocating the measured box actually retains every glyph,
        // including the last word; a geometry-only check missed the old loss.
        const auto glyphs = layoutWrappedText(Rectangle2f::fromTopLeft(
            0, 0, description.getWidth(), description.getHeight()), description.getText(), *font);
        const auto full = layoutWrappedText(Rectangle2f::fromTopLeft(
            0, 0, description.getWidth(), 100000), description.getText(), *font);
        REQUIRE(glyphs.size() == full.size());
        CHECK(glyphs.back().glyph == full.back().glyph);
    }

    TEST_CASE("exceptional map descriptions keep the dialog inside its viewport", "[ui][map-layout]")
    {
        auto font = mapFont();
        UiPanel panel(20, 20, 600, 440);
        auto description = std::make_unique<UiLabel>(0, 0, 235, 31, std::string(10000, 'W'), font);
        description->setName("DESCRIPTION");
        panel.appendChild(std::move(description));
        auto size = std::make_unique<UiLabel>(0, 0, 235, 18, "64 mb  Players: 2", font);
        size->setName("SIZE");
        panel.appendChild(std::move(size));
        layoutMapSelectionPanel(panel);
        auto& d = panel.find<UiLabel>("DESCRIPTION")->get();
        CHECK(d.getY() >= 124);
        CHECK(d.getHeight() < d.getTextHeight());
        CHECK(d.getText().size() == 10000);
        CHECK(panel.getY() + static_cast<int>(panel.getHeight()) <= 480);
    }
}
