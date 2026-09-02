#include <catch2/catch_test_macros.hpp>
#include <rwe/game/OrderButtons.h>
#include <rwe/sim/UnitDefinition.h>
#include <vector>

namespace rwe
{
    namespace
    {
        /** A Peewee, near enough: it walks, it stops, it patrols, it guards, it shoots. */
        UnitDefinition makeKbot()
        {
            UnitDefinition d{};
            d.canMove = true;
            d.canStop = true;
            d.canPatrol = true;
            d.canGuard = true;
            d.canAttack = true;
            d.mobileStandOrders = true;
            d.fireStandOrders = true;
            return d;
        }

        /** A solar collector: it sits there. Nothing in its FBI names a capability. */
        UnitDefinition makeCollector()
        {
            return UnitDefinition{};
        }

        /** An Atlas: the only four units in the shipped data that name CanLoad. */
        UnitDefinition makeTransport()
        {
            UnitDefinition d{};
            d.canMove = true;
            d.canStop = true;
            d.canPatrol = true;
            d.canGuard = true;
            d.canLoad = true;
            return d;
        }

        /** A commander: candgun, cancapture, canreclamate, and a cloak it pays for. */
        UnitDefinition makeCommander()
        {
            UnitDefinition d{};
            d.canMove = true;
            d.canStop = true;
            d.canPatrol = true;
            d.canGuard = true;
            d.canAttack = true;
            d.canCapture = true;
            d.canReclamate = true;
            d.canDgun = true;
            d.cloakable = true;
            d.onOffable = false;
            return d;
        }

        OrderButtonUnit unarmed(const UnitDefinition& d)
        {
            return OrderButtonUnit{&d, false};
        }

        OrderButtonUnit withDgun(const UnitDefinition& d)
        {
            return OrderButtonUnit{&d, true};
        }
    }

    TEST_CASE("a unit is offered only the orders its definition names", "[orderbuttons]")
    {
        // The original reads one bit of def+0x245 per button. A Kbot names
        // canmove, canstop, canpatrol, canguard and canattack and nothing
        // else, so that is the whole of what its panel shows.
        auto kbot = makeKbot();
        std::vector<OrderButtonUnit> selection{unarmed(kbot)};

        REQUIRE(selectionOffersOrderButton(selection, OrderButton::Move));
        REQUIRE(selectionOffersOrderButton(selection, OrderButton::Stop));
        REQUIRE(selectionOffersOrderButton(selection, OrderButton::Patrol));
        REQUIRE(selectionOffersOrderButton(selection, OrderButton::Defend));
        REQUIRE(selectionOffersOrderButton(selection, OrderButton::Attack));
        REQUIRE(selectionOffersOrderButton(selection, OrderButton::MoveOrders));
        REQUIRE(selectionOffersOrderButton(selection, OrderButton::FireOrders));

        REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::Load));
        REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::Unload));
        REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::Cloak));
        REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::OnOff));
        REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::Blast));
        REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::Capture));
        REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::Reclaim));
        REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::Repair));
    }

    TEST_CASE("a building that names nothing is offered nothing", "[orderbuttons]")
    {
        // The capability flags default to zero -- the parser zeroes the
        // register it passes as every boolean's default -- and the sixty-seven
        // buildings that stay silent get an empty order panel because of it.
        auto collector = makeCollector();
        std::vector<OrderButtonUnit> selection{unarmed(collector)};

        for (auto button : {OrderButton::Attack, OrderButton::Move, OrderButton::Defend, OrderButton::Patrol, OrderButton::Stop, OrderButton::Reclaim, OrderButton::Repair, OrderButton::Capture, OrderButton::Load, OrderButton::Unload, OrderButton::Blast, OrderButton::Cloak, OrderButton::OnOff, OrderButton::FireOrders, OrderButton::MoveOrders})
        {
            REQUIRE_FALSE(selectionOffersOrderButton(selection, button));
        }
    }

    TEST_CASE("a mixed selection offers what any one of them can do", "[orderbuttons]")
    {
        // 0x41B49F-0x41B524: each capability bit is ORed into its slot and
        // nothing ever clears one, so the rule is ANY and not ALL. Dragging a
        // solar collector into a box selection of Peewees does not cost the
        // Peewees their move button.
        auto kbot = makeKbot();
        auto collector = makeCollector();
        auto transport = makeTransport();

        SECTION("a silent building does not take the others' buttons away")
        {
            std::vector<OrderButtonUnit> selection{unarmed(kbot), unarmed(collector)};

            REQUIRE(selectionOffersOrderButton(selection, OrderButton::Move));
            REQUIRE(selectionOffersOrderButton(selection, OrderButton::Attack));
        }

        SECTION("one transport puts LOAD up for the whole selection")
        {
            std::vector<OrderButtonUnit> selection{unarmed(kbot), unarmed(transport)};

            REQUIRE(selectionOffersOrderButton(selection, OrderButton::Load));
            REQUIRE(selectionOffersOrderButton(selection, OrderButton::Unload));
        }

        SECTION("and the Kbots alone still do not get it")
        {
            std::vector<OrderButtonUnit> selection{unarmed(kbot), unarmed(kbot)};

            REQUIRE_FALSE(selectionOffersOrderButton(selection, OrderButton::Load));
        }
    }

    TEST_CASE("the D-gun needs a weapon as well as the flag", "[orderbuttons]")
    {
        // candgun is what offers the button, but the D-gun itself is an
        // ordinary commandfire weapon: a commander with none has nothing to
        // put behind it.
        auto commander = makeCommander();

        REQUIRE(selectionOffersOrderButton({withDgun(commander)}, OrderButton::Blast));
        REQUIRE_FALSE(selectionOffersOrderButton({unarmed(commander)}, OrderButton::Blast));

        SECTION("and a unit with the weapon but not the flag does not get it either")
        {
            // A nuclear silo has a commandfire weapon too, and its button is a
            // stockpile order rather than a D-gun.
            auto silo = makeCollector();
            silo.onOffable = true;

            REQUIRE_FALSE(selectionOffersOrderButton({withDgun(silo)}, OrderButton::Blast));
        }
    }

    TEST_CASE("LOAD takes the D-gun's slot away from it", "[orderbuttons]")
    {
        // ARMLOAD and ARMBLAST occupy the same rectangle in ARMGEN.GUI, and
        // 0x41A471 resolves the clash by hiding BLAST outright the moment
        // anything in the selection can load. Picking a transport up alongside
        // the commander costs you the D-gun button until you drop it again.
        auto commander = makeCommander();
        auto transport = makeTransport();

        REQUIRE(selectionOffersOrderButton({withDgun(commander)}, OrderButton::Blast));

        std::vector<OrderButtonUnit> together{withDgun(commander), unarmed(transport)};
        REQUIRE(selectionOffersOrderButton(together, OrderButton::Load));
        REQUIRE_FALSE(selectionOffersOrderButton(together, OrderButton::Blast));
    }

    TEST_CASE("repair comes and goes with reclaim", "[orderbuttons]")
    {
        // There is no CanRepair key. The parser copies canreclamate into a
        // second bit of the same dword at 0x42CA3D and the repair predicate
        // 0x4899CC reads that copy, so the two buttons are the same flag twice.
        auto commander = makeCommander();
        REQUIRE(selectionOffersOrderButton({unarmed(commander)}, OrderButton::Reclaim));
        REQUIRE(selectionOffersOrderButton({unarmed(commander)}, OrderButton::Repair));

        auto kbot = makeKbot();
        REQUIRE_FALSE(selectionOffersOrderButton({unarmed(kbot)}, OrderButton::Reclaim));
        REQUIRE_FALSE(selectionOffersOrderButton({unarmed(kbot)}, OrderButton::Repair));
    }

    TEST_CASE("panel element names map onto the buttons they gate", "[orderbuttons]")
    {
        // The names come out of ARMGEN.GUI with the side prefix stripped.
        REQUIRE(orderButtonFromName("MOVE") == OrderButton::Move);
        REQUIRE(orderButtonFromName("MOVEORD") == OrderButton::MoveOrders);
        REQUIRE(orderButtonFromName("LOAD") == OrderButton::Load);
        REQUIRE(orderButtonFromName("UNLOAD") == OrderButton::Unload);
        REQUIRE(orderButtonFromName("BLAST") == OrderButton::Blast);

        // Anything the panel carries that this does not gate is left alone.
        REQUIRE_FALSE(orderButtonFromName("BUILD").has_value());
        REQUIRE_FALSE(orderButtonFromName("ORDERS").has_value());
        REQUIRE_FALSE(orderButtonFromName("HEADER").has_value());
    }
}
