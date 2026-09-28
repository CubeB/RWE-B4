#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <rwe/pathfinding/oa/OaSearchHeap.h>
#include <vector>

// Ported from OpenAnnihilation, src/sim/ground-orders/tests/search_heap_test.cpp.

namespace rwe
{
    namespace
    {
        oa::SearchHeap::Payload item(uint32_t identity, int32_t score)
        {
            return {identity, identity ^ 0x55aa55aaU, std::bit_cast<uint32_t>(score), identity + 7};
        }
    }

    TEST_CASE("the search heap orders by signed score and defers a pop", "[oa]")
    {
        oa::SearchHeap heap;
        const auto first = heap.insert(item(1, 10));
        const auto equalA = heap.insert(item(2, 10));
        const auto equalB = heap.insert(item(3, 10));
        REQUIRE(first == 0);
        REQUIRE(equalA == 1);
        REQUIRE(equalB == 2);
        REQUIRE(heap.heap_order() == std::vector<oa::SearchHeap::Handle>{0, 1, 2});

        const auto low = heap.insert(item(4, 5));
        REQUIRE(low == 3);
        REQUIRE(heap.heap_order().front() == low);
        REQUIRE(heap.peek()[0] == 4);
        heap.pop();
        REQUIRE(heap.deferred_pop());
        REQUIRE(heap.size() == 4);

        // Insertion replaces the deferred root in its physical slot and repairs
        // downward; it does not allocate a fifth node.
        const auto replaced = heap.insert(item(5, 20));
        REQUIRE(replaced == low);
        REQUIRE(heap.allocated_slots() == 4);
        REQUIRE(!heap.deferred_pop());
        REQUIRE(heap.peek()[0] == 1);

        heap.pop();
        heap.payload(equalB)[2] = std::bit_cast<uint32_t>(-7);
        heap.decrease_key(equalB);
        REQUIRE(!heap.deferred_pop());
        REQUIRE(heap.size() == 3);
        REQUIRE(heap.peek()[0] == 3);
        REQUIRE(heap.free_head() == static_cast<int32_t>(first));

        const auto reused = heap.insert(item(6, 15));
        REQUIRE(reused == first);
    }

    TEST_CASE("the search heap keeps handles stable across reordering", "[oa]")
    {
        oa::SearchHeap heap;
        std::vector<oa::SearchHeap::Handle> handles;
        for (uint32_t i = 0; i < 8; ++i)
        {
            handles.push_back(heap.insert(item(i + 1, static_cast<int32_t>((i * 5) % 8))));
        }

        const auto& order = heap.heap_order();
        for (std::size_t position = 0; position < order.size(); ++position)
        {
            REQUIRE(heap.position(order[position]) == position);
        }
        for (auto handle : handles)
        {
            REQUIRE(heap.payload(handle)[0] == handle + 1);
        }
        // Scores are 0, 5, 2, 7, 4, 1, 6, 3, so the first handle holds the root.
        REQUIRE(order.front() == handles[0]);
    }

    TEST_CASE("the search heap reuses freed slots last in first out", "[oa]")
    {
        oa::SearchHeap heap;
        const auto first = heap.insert(item(1, 10));
        const auto second = heap.insert(item(2, 20));
        (void)heap.insert(item(3, 30));

        heap.pop();
        REQUIRE(heap.peek()[0] == 2);
        heap.pop();
        REQUIRE(heap.peek()[0] == 3);
        REQUIRE(heap.free_head() == static_cast<int32_t>(second));

        const auto reusedSecond = heap.insert(item(4, 5));
        const auto reusedFirst = heap.insert(item(5, 6));
        REQUIRE(reusedSecond == second);
        REQUIRE(reusedFirst == first);
    }

    TEST_CASE("the search heap exhaustion accounts for a deferred pop", "[oa]")
    {
        oa::SearchHeap heap;
        REQUIRE(heap.exhausted());
        (void)heap.insert(item(1, 1));
        REQUIRE(!heap.exhausted());
        heap.pop();
        REQUIRE(heap.exhausted());
    }

    TEST_CASE("the search heap grows and resets while keeping capacity", "[oa]")
    {
        oa::SearchHeap growth;
        for (uint32_t i = 0; i < 17; ++i)
        {
            (void)growth.insert(item(i, static_cast<int32_t>(i)));
        }
        REQUIRE(growth.capacity() == 40);
        REQUIRE(growth.allocated_slots() == 17);

        const auto keptCapacity = growth.capacity();
        growth.reset_open_set();
        REQUIRE(growth.exhausted());
        REQUIRE(growth.size() == 0);
        REQUIRE(growth.allocated_slots() == 0);
        REQUIRE(growth.free_head() == -1);
        REQUIRE(!growth.deferred_pop());
        REQUIRE(growth.capacity() == keptCapacity);

        const auto resetHandle = growth.insert(item(99, 1));
        REQUIRE(resetHandle == 0);
        REQUIRE(growth.allocated_slots() == 1);
    }
}
