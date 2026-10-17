#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/ResourceSharing.h>
#include <rwe/sim/sim_test_util.h>

/**
 * Resource sharing between allies (TOTALA-EXE-COMMANDS.md section 124): the
 * toggles, the reserves, the every-two-seconds automatic pass and the manual
 * gift.
 *
 * Every player here has a base of storage of its own (`hasBaseStorage`), as a
 * mission player does, because the settle re-derives a player's capacity each
 * second from its finished units and a skirmish test with no units would have
 * its stock clamped to nothing on the first second boundary.
 */
namespace rwe
{
    namespace
    {
        /** A human player holding `metal` and `energy`, with room for a thousand of each. */
        PlayerId addSharer(GameSimulation& sim, float metal, float energy, std::optional<int> team, GamePlayerType type = GamePlayerType::Human)
        {
            GamePlayerInfo p{
                std::optional<std::string>("p"),
                type,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(metal),
                Energy(energy),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            p.teamId = team;
            p.hasBaseStorage = true;
            return sim.addPlayer(p);
        }

        GameSimulation makeSim()
        {
            GameSimulation sim(makeFlatTerrain(16, 16), 0u, 100, 3000);

            // A stranger on no team, never given a unit, keeps the game
            // undecided: players who are all allies are a won game, and a won
            // game freezes every settle after 150 ticks (BT-82), which is not
            // what these cases are about.
            addSharer(sim, 0.0f, 0.0f, std::nullopt);
            return sim;
        }

        PlayerShareCommand toggleMetal()
        {
            PlayerShareCommand c;
            c.kind = PlayerShareCommand::Kind::ToggleMetal;
            return c;
        }

        PlayerShareCommand toggleEnergy()
        {
            PlayerShareCommand c;
            c.kind = PlayerShareCommand::Kind::ToggleEnergy;
            return c;
        }

        PlayerShareCommand setReserve(bool metal, float amount)
        {
            PlayerShareCommand c;
            c.kind = metal ? PlayerShareCommand::Kind::SetMetalReserve : PlayerShareCommand::Kind::SetEnergyReserve;
            c.amount = amount;
            return c;
        }

        PlayerShareCommand give(PlayerId target, float amount, bool metal)
        {
            PlayerShareCommand c;
            c.kind = PlayerShareCommand::Kind::Give;
            c.amount = amount;
            c.target = target;
            c.giveMetal = metal;
            return c;
        }

        float metalOf(const GameSimulation& sim, PlayerId p)
        {
            return sim.getPlayer(p).metal.value;
        }

        float energyOf(const GameSimulation& sim, PlayerId p)
        {
            return sim.getPlayer(p).energy.value;
        }
    }

    TEST_CASE("sharing toggles and reserves", "[share]")
    {
        auto sim = makeSim();
        auto a = addSharer(sim, 500.0f, 500.0f, 0);

        SECTION("a toggle flips and flips back, and each resource has its own")
        {
            REQUIRE_FALSE(sim.getPlayer(a).shareMetal);
            REQUIRE(applyPlayerShareCommand(sim, a, toggleMetal()));
            REQUIRE(sim.getPlayer(a).shareMetal);
            REQUIRE_FALSE(sim.getPlayer(a).shareEnergy);

            REQUIRE(applyPlayerShareCommand(sim, a, toggleEnergy()));
            REQUIRE(sim.getPlayer(a).shareEnergy);

            REQUIRE(applyPlayerShareCommand(sim, a, toggleMetal()));
            REQUIRE_FALSE(sim.getPlayer(a).shareMetal);
            REQUIRE(sim.getPlayer(a).shareEnergy);
        }

        SECTION("a reserve is stored capped at storage and does not enable sharing")
        {
            REQUIRE(applyPlayerShareCommand(sim, a, setReserve(true, 400.0f)));
            REQUIRE(sim.getPlayer(a).metalReserve == Metal(400.0f));

            REQUIRE(applyPlayerShareCommand(sim, a, setReserve(true, 99999.0f)));
            REQUIRE(sim.getPlayer(a).metalReserve == Metal(1000.0f));

            REQUIRE(applyPlayerShareCommand(sim, a, setReserve(false, 250.0f)));
            REQUIRE(sim.getPlayer(a).energyReserve == Energy(250.0f));

            REQUIRE_FALSE(sim.getPlayer(a).shareMetal);
            REQUIRE_FALSE(sim.getPlayer(a).shareEnergy);
        }

        SECTION("a negative reserve is accepted, as the original does")
        {
            REQUIRE(applyPlayerShareCommand(sim, a, setReserve(true, -300.0f)));
            REQUIRE(sim.getPlayer(a).metalReserve == Metal(-300.0f));
        }

        SECTION("a reserve that is not a number is refused")
        {
            REQUIRE_FALSE(applyPlayerShareCommand(sim, a, setReserve(true, std::numeric_limits<float>::quiet_NaN())));
            REQUIRE(sim.getPlayer(a).metalReserve == Metal(0.0f));
        }

        SECTION("a command from a player the game does not have changes nothing")
        {
            REQUIRE_FALSE(applyPlayerShareCommand(sim, PlayerId(7), toggleMetal()));
        }
    }

    TEST_CASE("the automatic pass moves a third of the metal surplus every two seconds", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 900.0f, 100.0f, 0);
        auto ally = addSharer(sim, 100.0f, 100.0f, 0);
        applyPlayerShareCommand(sim, donor, toggleMetal());

        // Nothing moves on any tick but a multiple of sixty.
        tick(sim, 59);
        REQUIRE(metalOf(sim, donor) == 900.0f);
        REQUIRE(metalOf(sim, ally) == 100.0f);

        // A third of (900 - 0), and it settles in the same tick.
        tick(sim, 1);
        REQUIRE(sim.gameTime.value == 60u);
        REQUIRE(metalOf(sim, donor) == 600.0f);
        REQUIRE(metalOf(sim, ally) == 400.0f);

        tick(sim, 59);
        REQUIRE(metalOf(sim, donor) == 600.0f);
        REQUIRE(metalOf(sim, ally) == 400.0f);

        // And again at 120: a third of (600 - 0) is 200.
        tick(sim, 1);
        REQUIRE(metalOf(sim, donor) == 400.0f);
        REQUIRE(metalOf(sim, ally) == 600.0f);

        // Energy was never switched on.
        REQUIRE(energyOf(sim, donor) == 100.0f);
        REQUIRE(energyOf(sim, ally) == 100.0f);
    }

    TEST_CASE("the automatic pass moves half the energy surplus", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 100.0f, 800.0f, 0);
        auto ally = addSharer(sim, 100.0f, 0.0f, 0);
        applyPlayerShareCommand(sim, donor, toggleEnergy());

        tick(sim, 60);
        REQUIRE(energyOf(sim, donor) == 400.0f);
        REQUIRE(energyOf(sim, ally) == 400.0f);
        REQUIRE(metalOf(sim, donor) == 100.0f);
    }

    TEST_CASE("a donor holding nothing above its reserve gives nothing", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 900.0f, 100.0f, 0);
        auto ally = addSharer(sim, 100.0f, 100.0f, 0);
        applyPlayerShareCommand(sim, donor, toggleMetal());

        SECTION("stock under the reserve")
        {
            applyPlayerShareCommand(sim, donor, setReserve(true, 950.0f));
            tick(sim, 60);
            REQUIRE(metalOf(sim, donor) == 900.0f);
            REQUIRE(metalOf(sim, ally) == 100.0f);
        }

        SECTION("only the part above it is offered")
        {
            // A third of (900 - 600) is 100.
            applyPlayerShareCommand(sim, donor, setReserve(true, 600.0f));
            tick(sim, 60);
            REQUIRE(metalOf(sim, donor) == 800.0f);
            REQUIRE(metalOf(sim, ally) == 200.0f);
        }
    }

    TEST_CASE("the offer is capped at the recipient's free storage", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 1000.0f, 100.0f, 0);
        auto ally = addSharer(sim, 900.0f, 100.0f, 0);
        applyPlayerShareCommand(sim, donor, toggleMetal());

        // A third of 1000 would be 333; there is room for 100.
        tick(sim, 60);
        REQUIRE(metalOf(sim, donor) == 900.0f);
        REQUIRE(metalOf(sim, ally) == 1000.0f);
    }

    TEST_CASE("a recipient with no room takes nothing", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 1000.0f, 100.0f, 0);
        auto ally = addSharer(sim, 999.5f, 100.0f, 0);
        // Poorer than the donor by half a unit, but full to the brim after
        // the cap below.
        sim.getPlayer(ally).maxMetal = Metal(999.5f);
        sim.getPlayer(ally).startingMetal = Metal(999.5f);
        applyPlayerShareCommand(sim, donor, toggleMetal());

        tick(sim, 60);
        REQUIRE(metalOf(sim, donor) == 1000.0f);
        REQUIRE(metalOf(sim, ally) == 999.5f);
    }

    TEST_CASE("the last qualifying slot gets the offer, not the poorest", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 900.0f, 100.0f, 0);
        auto poorer = addSharer(sim, 0.0f, 100.0f, 0);
        auto lessPoor = addSharer(sim, 600.0f, 100.0f, 0);
        applyPlayerShareCommand(sim, donor, toggleMetal());

        tick(sim, 60);

        // The earlier, poorer ally is passed over: there is no comparison
        // between candidates.
        REQUIRE(metalOf(sim, poorer) == 0.0f);
        REQUIRE(metalOf(sim, lessPoor) == 900.0f);
        REQUIRE(metalOf(sim, donor) == 600.0f);
    }

    TEST_CASE("a later ally that is not poorer than the donor does not qualify", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 500.0f, 100.0f, 0);
        auto poorer = addSharer(sim, 100.0f, 100.0f, 0);
        auto richer = addSharer(sim, 800.0f, 100.0f, 0);
        applyPlayerShareCommand(sim, donor, toggleMetal());

        tick(sim, 60);

        REQUIRE(metalOf(sim, poorer) == 100.0f + 500.0f / 3.0f);
        REQUIRE(metalOf(sim, richer) == 800.0f);
    }

    TEST_CASE("the automatic pass never sends to a computer player or an enemy", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 900.0f, 100.0f, 0);
        auto computerAlly = addSharer(sim, 0.0f, 100.0f, 0, GamePlayerType::Computer);
        auto enemy = addSharer(sim, 0.0f, 100.0f, 1);
        auto teamless = addSharer(sim, 0.0f, 100.0f, std::nullopt);
        applyPlayerShareCommand(sim, donor, toggleMetal());

        tick(sim, 60);

        REQUIRE(metalOf(sim, donor) == 900.0f);
        REQUIRE(metalOf(sim, computerAlly) == 0.0f);
        REQUIRE(metalOf(sim, enemy) == 0.0f);
        REQUIRE(metalOf(sim, teamless) == 0.0f);
    }

    TEST_CASE("the automatic pass skips the dead, in both directions", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 900.0f, 100.0f, 0);
        auto dead = addSharer(sim, 0.0f, 100.0f, 0);
        sim.getPlayer(dead).status = GamePlayerStatus::Dead;
        applyPlayerShareCommand(sim, donor, toggleMetal());

        SECTION("a dead player is not a recipient")
        {
            tick(sim, 60);
            REQUIRE(metalOf(sim, donor) == 900.0f);
            REQUIRE(metalOf(sim, dead) == 0.0f);
        }

        SECTION("a dead player does not give")
        {
            auto living = addSharer(sim, 0.0f, 100.0f, 0);
            applyPlayerShareCommand(sim, dead, toggleMetal());
            sim.getPlayer(dead).metal = Metal(900.0f);
            sim.getPlayer(donor).shareMetal = false;

            tick(sim, 60);
            REQUIRE(metalOf(sim, dead) == 900.0f);
            REQUIRE(metalOf(sim, living) == 0.0f);
        }
    }

    TEST_CASE("a computer player's own flag is never acted on", "[share]")
    {
        auto sim = makeSim();
        auto computer = addSharer(sim, 900.0f, 100.0f, 0, GamePlayerType::Computer);
        auto ally = addSharer(sim, 0.0f, 100.0f, 0);
        sim.getPlayer(computer).shareMetal = true;

        tick(sim, 60);
        REQUIRE(metalOf(sim, computer) == 900.0f);
        REQUIRE(metalOf(sim, ally) == 0.0f);
    }

    TEST_CASE("a player simulated elsewhere neither gives nor receives automatically", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 900.0f, 100.0f, 0);
        auto remote = addSharer(sim, 0.0f, 100.0f, 0);
        sim.getPlayer(remote).simulation = PlayerSimulation::Remote;
        applyPlayerShareCommand(sim, donor, toggleMetal());

        tick(sim, 60);
        REQUIRE(metalOf(sim, donor) == 900.0f);
        REQUIRE(metalOf(sim, remote) == 0.0f);
    }

    TEST_CASE("two donors both pay out, in player order", "[share]")
    {
        auto sim = makeSim();
        auto first = addSharer(sim, 900.0f, 100.0f, 0);
        auto second = addSharer(sim, 600.0f, 100.0f, 0);
        auto poor = addSharer(sim, 0.0f, 100.0f, 0);
        applyPlayerShareCommand(sim, first, toggleMetal());
        applyPlayerShareCommand(sim, second, toggleMetal());

        tick(sim, 60);

        // Each donor's last qualifying ally is the slot after it that is
        // poorer: for the first that is `poor`, for the second also `poor`.
        REQUIRE(metalOf(sim, first) == 600.0f);
        REQUIRE(metalOf(sim, second) == 400.0f);
        REQUIRE(metalOf(sim, poor) == 500.0f);
    }

    TEST_CASE("a gift moves stock to anyone, capped at what the donor holds", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 500.0f, 500.0f, 0);
        auto stranger = addSharer(sim, 100.0f, 100.0f, 1);

        SECTION("metal, to an enemy, settles at the next economy second")
        {
            REQUIRE(applyPlayerShareCommand(sim, donor, give(stranger, 300.0f, true)));

            // Booked as a spend and as income; neither is in the stock until
            // the settle at tick 30.
            REQUIRE(metalOf(sim, donor) == 500.0f);
            REQUIRE(metalOf(sim, stranger) == 100.0f);

            tick(sim, 30);
            REQUIRE(metalOf(sim, donor) == 200.0f);
            REQUIRE(metalOf(sim, stranger) == 400.0f);
            REQUIRE(energyOf(sim, donor) == 500.0f);
        }

        SECTION("energy is the other account")
        {
            REQUIRE(applyPlayerShareCommand(sim, donor, give(stranger, 200.0f, false)));
            tick(sim, 30);
            REQUIRE(energyOf(sim, donor) == 300.0f);
            REQUIRE(energyOf(sim, stranger) == 300.0f);
            REQUIRE(metalOf(sim, donor) == 500.0f);
        }

        SECTION("more than the donor holds is capped at what it holds")
        {
            REQUIRE(applyPlayerShareCommand(sim, donor, give(stranger, 5000.0f, true)));
            tick(sim, 30);
            REQUIRE(metalOf(sim, donor) == 0.0f);
            REQUIRE(metalOf(sim, stranger) == 600.0f);
        }

        SECTION("a gift is not consumption")
        {
            applyPlayerShareCommand(sim, donor, give(stranger, 300.0f, true));
            REQUIRE(sim.getPlayer(donor).desiredMetalConsumptionBuffer == Metal(0.0f));
            REQUIRE(sim.getPlayer(donor).metalRequestBuffer == Metal(300.0f));
            REQUIRE(sim.getPlayer(stranger).metalProductionBuffer == Metal(300.0f));
        }
    }

    TEST_CASE("a gift that makes no sense is refused", "[share]")
    {
        auto sim = makeSim();
        auto donor = addSharer(sim, 500.0f, 500.0f, 0);
        auto other = addSharer(sim, 100.0f, 100.0f, 0);

        auto nothingMoved = [&] {
            tick(sim, 30);
            REQUIRE(metalOf(sim, donor) == 500.0f);
            REQUIRE(metalOf(sim, other) == 100.0f);
        };

        SECTION("a negative amount does not reach into the other player's account")
        {
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, give(other, -300.0f, true)));
            nothingMoved();
        }

        SECTION("zero")
        {
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, give(other, 0.0f, true)));
            nothingMoved();
        }

        SECTION("not a number")
        {
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, give(other, std::numeric_limits<float>::quiet_NaN(), true)));
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, give(other, std::numeric_limits<float>::infinity(), true)));
            nothingMoved();
        }

        SECTION("to oneself")
        {
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, give(donor, 100.0f, true)));
            nothingMoved();
        }

        SECTION("to a player the game does not have")
        {
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, give(PlayerId(9), 100.0f, true)));
            nothingMoved();
        }

        SECTION("with no target at all")
        {
            PlayerShareCommand c;
            c.kind = PlayerShareCommand::Kind::Give;
            c.amount = 100.0f;
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, c));
            nothingMoved();
        }

        SECTION("from a dead player")
        {
            sim.getPlayer(donor).status = GamePlayerStatus::Dead;
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, give(other, 100.0f, true)));
            nothingMoved();
        }

        SECTION("to or from a player simulated elsewhere")
        {
            sim.getPlayer(other).simulation = PlayerSimulation::Remote;
            REQUIRE_FALSE(applyPlayerShareCommand(sim, donor, give(other, 100.0f, true)));
            REQUIRE_FALSE(applyPlayerShareCommand(sim, other, give(donor, 100.0f, true)));
            nothingMoved();
        }
    }

    TEST_CASE("sharing state moves the sync hash and a replay of the same commands agrees", "[share][hash]")
    {
        auto build = [] {
            auto sim = makeSim();
            addSharer(sim, 900.0f, 400.0f, 0);
            addSharer(sim, 100.0f, 100.0f, 0);
            addSharer(sim, 50.0f, 50.0f, 1);
            return sim;
        };

        auto untouched = build();
        auto toggled = build();
        applyPlayerShareCommand(toggled, PlayerId(0), toggleMetal());
        REQUIRE(computeHashOf(untouched) != computeHashOf(toggled));

        auto reserved = build();
        applyPlayerShareCommand(reserved, PlayerId(0), setReserve(false, 77.0f));
        REQUIRE(computeHashOf(untouched) != computeHashOf(reserved));
        REQUIRE(computeHashOf(toggled) != computeHashOf(reserved));

        // The same commands at the same ticks give the same game, which is
        // what lockstep (and a replay) needs of every peer.
        auto run = [&] {
            auto sim = build();
            applyPlayerShareCommand(sim, PlayerId(0), toggleMetal());
            applyPlayerShareCommand(sim, PlayerId(0), toggleEnergy());
            tick(sim, 90);
            applyPlayerShareCommand(sim, PlayerId(1), give(PlayerId(2), 40.0f, true));
            applyPlayerShareCommand(sim, PlayerId(0), setReserve(true, 100.0f));
            tick(sim, 100);
            return sim;
        };

        auto first = run();
        auto second = run();
        REQUIRE(computeHashOf(first) == computeHashOf(second));
        REQUIRE(metalOf(first, PlayerId(0)) < 900.0f);
    }
}
