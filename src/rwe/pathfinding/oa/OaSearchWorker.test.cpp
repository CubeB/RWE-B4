#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <rwe/pathfinding/oa/OaSearchGoal.h>
#include <rwe/pathfinding/oa/OaSearchWorker.h>
#include <stdexcept>
#include <vector>

// Ported from OpenAnnihilation, src/sim/ground-orders/tests/search_worker_test.cpp.
// The original sampled a MovementMap; here the movement-map class arrives through
// SearchBegin's classify callback.

namespace rwe
{
    namespace
    {
        uint32_t classifyOpen(void*, int32_t, int32_t)
        {
            return 3;
        }

        uint32_t classifyBlocked(void*, int32_t, int32_t)
        {
            return 0;
        }

        uint32_t classifyTight(void*, int32_t, int32_t)
        {
            return 1;
        }

        struct GridClassifier
        {
            int32_t width{};
            int32_t height{};
            const std::vector<uint8_t>* cells{};
        };

        uint32_t classifyGrid(void* context, int32_t x, int32_t z)
        {
            const auto& grid = *static_cast<GridClassifier*>(context);
            if (x < 0 || z < 0 || x >= grid.width || z >= grid.height)
            {
                return 0;
            }
            return (*grid.cells)[static_cast<std::size_t>(z) * grid.width + x];
        }

        oa::SearchUnit makeUnit(int16_t cellX, int16_t cellZ)
        {
            oa::SearchUnit unit{};
            unit.cellX = cellX;
            unit.cellZ = cellZ;
            unit.footprintX = 1;
            unit.footprintZ = 1;
            return unit;
        }

        oa::SearchBegin beginOn(
            int32_t width, int32_t height, oa::SearchBegin::ClassifyFn classify, void* context = nullptr)
        {
            oa::SearchBegin begin{};
            begin.mapWidth = width;
            begin.mapHeight = height;
            begin.classify = classify;
            begin.classifyContext = context;
            return begin;
        }

        oa::SearchAdvance runToEnd(oa::SearchWorker& worker)
        {
            auto status = oa::SearchAdvance::in_progress;
            for (int step = 0; step < 1000 && status == oa::SearchAdvance::in_progress; ++step)
            {
                status = worker.advance_slice();
            }
            return status;
        }
    }

    TEST_CASE("the search cell grid rounds its allocation and clears reused cells", "[oa]")
    {
        oa::SearchCellGrid grid;
        grid.allocate(1, 1);
        REQUIRE(grid.width == 1);
        REQUIRE(grid.height == 1);
        REQUIRE(grid.count == 8);
        REQUIRE(grid.cells.size() == 8);
        REQUIRE(grid.cells[0].flags == 0);
        REQUIRE(grid.cells[7].predecessor == 0);

        grid.cells[0].flags = 3;
        grid.allocate(7, 1);
        REQUIRE(grid.count == 8);
        REQUIRE(grid.cells.size() == 8);
        REQUIRE(grid.cells[0].flags == 0);

        grid.allocate(3, 3);
        REQUIRE(grid.width == 3);
        REQUIRE(grid.height == 3);
        REQUIRE(grid.count == 16);
        REQUIRE(grid.cells.size() == 16);

        grid.allocate(8, 2);
        REQUIRE(grid.count == 16);
        REQUIRE(grid.cells.size() == 16);

        grid.allocate(0, 5);
        REQUIRE(grid.width == 0);
        REQUIRE(grid.height == 5);
        REQUIRE(grid.count == 0);
        REQUIRE(grid.cells.empty());

        grid.allocate(-1, 1);
        REQUIRE(grid.width == -1);
        REQUIRE(grid.height == 1);
        REQUIRE(grid.count == 0);
        REQUIRE(grid.cells.empty());

        grid.allocate(0x10000, 0x10000);
        REQUIRE(grid.width == 0x10000);
        REQUIRE(grid.height == 0x10000);
        REQUIRE(grid.count == 0);
        REQUIRE(grid.cells.empty());
    }

    TEST_CASE("a cell outside the search grid is rejected", "[oa]")
    {
        const auto unit = makeUnit(0, 0);
        oa::SearchGoal goal{};
        goal.cellX = 2;
        goal.cellZ = 0;
        const oa::SearchRecord record{&unit, &goal};

        oa::SearchWorker worker;
        REQUIRE(worker.begin(record, beginOn(4, 4, &classifyOpen)) == oa::SearchAdvance::in_progress);
        REQUIRE(worker.width() == 4);
        REQUIRE(worker.height() == 4);
        REQUIRE_NOTHROW(worker.cell(3, 3));
        REQUIRE_THROWS_AS(worker.cell(4, 0), std::out_of_range);
        REQUIRE_THROWS_AS(worker.cell(0, 4), std::out_of_range);
    }

    TEST_CASE("crossed start goal axis tests the axis aligned path", "[oa]")
    {
        CHECK(oa::crossed_start_goal_axis(0, 0, 4, 0, 2, 0));
        CHECK(oa::crossed_start_goal_axis(0, 0, 4, 0, 4, 0));
        CHECK(!oa::crossed_start_goal_axis(0, 0, 4, 0, 5, 0));
        CHECK(!oa::crossed_start_goal_axis(0, 0, 4, 0, 0, 0));
        CHECK(oa::crossed_start_goal_axis(0, 0, 0, 3, 0, 2));
        CHECK(!oa::crossed_start_goal_axis(0, 0, 0, 3, 0, 0));
        CHECK(!oa::crossed_start_goal_axis(0, 0, 4, 3, 2, 1));
        CHECK(oa::crossed_start_goal_axis(0, 0, 4, 3, 4, 1));
        CHECK(oa::crossed_start_goal_axis(5, 5, 1, 5, 3, 5));
        CHECK(oa::crossed_start_goal_axis(5, 5, 5, 1, 5, 3));
        CHECK(!oa::crossed_start_goal_axis(0, 0, 4, 0, 2, 1));
    }

    TEST_CASE("begin fails at once when the job is already over", "[oa]")
    {
        SECTION("the start is inside the goal")
        {
            const auto unit = makeUnit(0, 0);
            oa::SearchGoal goal{};
            goal.cellX = 0;
            goal.cellZ = 0;
            const oa::SearchRecord record{&unit, &goal};

            oa::SearchWorker worker;
            REQUIRE(worker.begin(record, beginOn(8, 8, &classifyOpen)) == oa::SearchAdvance::failed);
            REQUIRE((worker.order_flags() & oa::search_goal_contact_flag) != 0);
            REQUIRE(worker.route().empty());
        }

        SECTION("the start is off the map")
        {
            const auto unit = makeUnit(8, 0);
            oa::SearchGoal goal{};
            goal.cellX = 1;
            goal.cellZ = 0;
            const oa::SearchRecord record{&unit, &goal};

            oa::SearchWorker worker;
            REQUIRE(worker.begin(record, beginOn(4, 4, &classifyOpen)) == oa::SearchAdvance::failed);
            REQUIRE(
                (worker.order_flags() & oa::search_seed_unresolved_flag) == oa::search_seed_unresolved_flag);
        }

        SECTION("the start is walled in")
        {
            const auto unit = makeUnit(0, 0);
            oa::SearchGoal goal{};
            goal.cellX = 3;
            goal.cellZ = 0;
            const oa::SearchRecord record{&unit, &goal};

            oa::SearchWorker worker;
            REQUIRE(worker.begin(record, beginOn(4, 4, &classifyBlocked)) == oa::SearchAdvance::failed);
            REQUIRE(
                (worker.order_flags() & oa::search_seed_unresolved_flag) == oa::search_seed_unresolved_flag);
            REQUIRE(worker.route().empty());
        }
    }

    TEST_CASE("expanding the start opens the facing neighbour", "[oa]")
    {
        const auto unit = makeUnit(0, 0);
        oa::SearchGoal goal{};
        goal.cellX = 2;
        goal.cellZ = 0;
        const oa::SearchRecord record{&unit, &goal};

        oa::SearchWorker worker;
        REQUIRE(worker.begin(record, beginOn(4, 4, &classifyOpen)) == oa::SearchAdvance::in_progress);
        REQUIRE(worker.classify(1, 0) == 3);
        REQUIRE(worker.expand_best() == 0);
        REQUIRE(worker.fan() == oa::search_initial_fan);
        REQUIRE(worker.cell(1, 0).predecessor == 6);
        REQUIRE((worker.cell(1, 0).flags & 3) == 1);
    }

    TEST_CASE("an open grid search reaches the goal and reconstructs the route", "[oa]")
    {
        const auto unit = makeUnit(0, 0);
        oa::SearchGoal goal{};
        goal.cellX = 3;
        goal.cellZ = 0;
        const oa::SearchRecord record{&unit, &goal};

        oa::SearchWorker worker;
        REQUIRE(worker.begin(record, beginOn(8, 8, &classifyOpen)) == oa::SearchAdvance::in_progress);
        REQUIRE(worker.fan() == oa::search_initial_fan);
        // The wall-follow seed runs straight to the goal on open ground.
        REQUIRE((worker.order_flags() & oa::search_goal_contact_flag) != 0);
        REQUIRE((worker.cell(0, 0).flags & 3) == 1);

        REQUIRE(worker.expand_best() == 0);
        REQUIRE((worker.cell(0, 0).flags & 3) == 2);
        REQUIRE((worker.cell(1, 0).flags & 3) == 1);
        REQUIRE(worker.cell(1, 0).predecessor == 6);
        const auto firstCost = std::bit_cast<int32_t>(worker.heap().payload(worker.cell(1, 0).handle)[1]);
        REQUIRE(firstCost == oa::search_cardinal_cost + oa::search_turn_penalty[6]);

        oa::SearchWorker runner;
        REQUIRE(runner.begin(record, beginOn(8, 8, &classifyOpen)) == oa::SearchAdvance::in_progress);
        REQUIRE(runToEnd(runner) == oa::SearchAdvance::succeeded);
        REQUIRE(runner.fan() == oa::search_continue_fan);
        REQUIRE(!runner.route().empty());
        REQUIRE(runner.route().front()[0] == 8);
        REQUIRE(runner.route().front()[1] == 8);
        REQUIRE(runner.route().back()[0] == 56);
        REQUIRE(runner.route().back()[1] == 8);
    }

    TEST_CASE("a blocked cell seeds a wall follow around it", "[oa]")
    {
        std::vector<uint8_t> cells(12, 3);
        cells[1] = 0; // (1, 0) blocks the straight line to the goal
        GridClassifier grid{4, 3, &cells};

        const auto unit = makeUnit(0, 0);
        oa::SearchGoal goal{};
        goal.cellX = 2;
        goal.cellZ = 0;
        const oa::SearchRecord record{&unit, &goal};

        oa::SearchWorker worker;
        REQUIRE(
            worker.begin(record, beginOn(4, 3, &classifyGrid, &grid)) == oa::SearchAdvance::in_progress);
        REQUIRE((worker.cell(0, 1).flags & oa::search_seed_bit) != 0);
        REQUIRE((worker.cell(1, 1).flags & oa::search_seed_bit) != 0);
        REQUIRE(worker.threshold() < worker.heuristic(0, 0));
        REQUIRE(
            (worker.order_flags() & (oa::search_goal_contact_flag | oa::search_seed_unresolved_flag)) != 0);
        REQUIRE(runToEnd(worker) == oa::SearchAdvance::succeeded);
        REQUIRE(!worker.route().empty());
    }

    TEST_CASE("a new job clears the previous goal marking", "[oa]")
    {
        const auto unit = makeUnit(0, 0);
        oa::SearchGoal first{};
        first.cellX = 2;
        first.cellZ = 0;
        oa::SearchGoal second{};
        second.cellX = 4;
        second.cellZ = 1;

        oa::SearchWorker worker;
        REQUIRE(
            worker.begin(oa::SearchRecord{&unit, &first}, beginOn(6, 6, &classifyOpen)) == oa::SearchAdvance::in_progress);
        (void)runToEnd(worker);
        REQUIRE(
            worker.begin(oa::SearchRecord{&unit, &second}, beginOn(6, 6, &classifyOpen)) == oa::SearchAdvance::in_progress);
        REQUIRE((worker.cell(2, 0).flags & oa::search_goal_bit) == 0);
        REQUIRE((worker.cell(4, 1).flags & oa::search_goal_bit) != 0);
    }

    TEST_CASE("a tight cell adds the difficult terrain extra", "[oa]")
    {
        const auto unit = makeUnit(0, 0);
        oa::SearchGoal goal{};
        goal.cellX = 2;
        goal.cellZ = 0;
        const oa::SearchRecord record{&unit, &goal};

        oa::SearchWorker worker;
        REQUIRE(worker.begin(record, beginOn(4, 4, &classifyTight)) == oa::SearchAdvance::in_progress);
        REQUIRE(worker.classify(1, 0) == 1);
        REQUIRE(worker.expand_best() == 0);
        const auto extra = std::bit_cast<int16_t>(
            static_cast<uint16_t>(worker.heap().payload(worker.cell(1, 0).handle)[3]));
        REQUIRE(extra == oa::search_difficult_extra);
    }

    TEST_CASE("search route points are footprint centres in world units", "[oa]")
    {
        const auto unit = makeUnit(1, 1);
        oa::SearchGoal goal{};
        goal.cellX = 12;
        goal.cellZ = 9;
        const oa::SearchRecord record{&unit, &goal};

        oa::SearchWorker worker;
        REQUIRE(worker.begin(record, beginOn(16, 16, &classifyOpen)) == oa::SearchAdvance::in_progress);
        REQUIRE(runToEnd(worker) == oa::SearchAdvance::succeeded);
        REQUIRE(worker.route().size() >= 2);
        REQUIRE(worker.route().front()[0] == 24);
        REQUIRE(worker.route().front()[1] == 24);
        REQUIRE(worker.route().back()[0] == 200);
        REQUIRE(worker.route().back()[1] == 152);
    }

    TEST_CASE("the player heuristic scales up when a player searches little", "[oa]")
    {
        const auto scale = oa::search_heuristic_scale;
        CHECK(oa::search_rescaled_heuristic(0, 1, scale) == scale * 6);
        CHECK(oa::search_rescaled_heuristic(1, 1, scale) == scale * 3);
        CHECK(oa::search_rescaled_heuristic(2, 1, scale) == scale);
        CHECK(oa::search_rescaled_heuristic(8, 4, scale) == scale);
        CHECK_THROWS_AS(oa::search_rescaled_heuristic(0, 0, scale), std::domain_error);
    }

    TEST_CASE("the search budget is shared between live players", "[oa]")
    {
        // OA's search_tick_credit (0x535) is not part of this port, so pass it directly.
        CHECK(oa::search_player_credit(0x535, 0) == 0);
        CHECK(oa::search_player_credit(0x535, 1) == 0x535);
        CHECK(oa::search_player_credit(0x535, 2) == 0x29a);
    }
}
