// Ported from OpenAnnihilation, src/sim/ground-orders/include/oa/sim/ground_orders/search_heap.hpp.
// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rwe
{
    namespace oa
    {
        /**
         * Path search open-set heap, a binary min-heap on payload word 2.
         *
         * Handles are physical node slots. They remain valid across heap reordering and
         * storage growth, and are recycled last in, first out through a free-slot chain. A
         * pop is deferred: the root stays in place until the next insert, peek or
         * decrease_key settles it.
         */
        class SearchHeap
        {
        public:
            using Payload = std::array<uint32_t, 4>;
            using Handle = uint32_t;

            /**
             * Inserts a node, or reuses the root of a deferred pop for it.
             *
             * Grows the storage by the capacity + capacity/2 + 16 rule when full.
             *
             * @param value node payload; word 2 is the signed search score
             * @return the node's handle
             */
            [[nodiscard]] Handle insert(const Payload& value);

            /**
             * Moves a node up after its caller lowered its score.
             *
             * With a pop pending, the deferred root is removed once another node displaces
             * it.
             *
             * @param handle node whose score decreased
             */
            void decrease_key(Handle handle);

            /**
             * Returns the lowest-score payload, first completing a pending pop.
             *
             * Throws std::out_of_range for an empty heap.
             *
             * @return the root payload
             */
            [[nodiscard]] const Payload& peek();

            /**
             * Pops the root; the first pop is deferred until the next heap operation.
             *
             * Throws std::out_of_range for an empty heap.
             */
            void pop();

            /**
             * Returns a node's payload for update before decrease_key.
             *
             * @param handle node slot
             * @return the payload
             */
            [[nodiscard]] Payload& payload(Handle handle);

            /**
             * Returns a node's payload.
             *
             * @param handle node slot
             * @return the payload
             */
            [[nodiscard]] const Payload& payload(Handle handle) const;

            /** Returns the number of nodes in the heap, a deferred root included. */
            [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }

            /** Returns whether the heap holds no nodes. */
            [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }

            /** Returns the reserved node capacity. */
            [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

            /** Returns the number of node slots ever allocated. */
            [[nodiscard]] std::size_t allocated_slots() const noexcept { return nodes_.size(); }

            /** Returns the first free slot, or -1. */
            [[nodiscard]] int32_t free_head() const noexcept { return free_head_; }

            /** Returns whether a pop is pending on the root. */
            [[nodiscard]] bool deferred_pop() const noexcept { return deferred_pop_; }

            /**
             * Returns a live node's position in the heap array.
             *
             * Throws std::out_of_range for a free or unallocated slot.
             *
             * @param handle node slot
             * @return the heap position
             */
            [[nodiscard]] std::size_t position(Handle handle) const;

            /** Returns the heap array of node slots. */
            [[nodiscard]] const std::vector<Handle>& heap_order() const noexcept { return heap_; }

            /** Drops live nodes and the free-slot chain while keeping capacity. */
            void reset_open_set();

            /**
             * Tests whether no unpopped open nodes remain.
             *
             * The live node count equals the deferred-pop flag (0 or 1).
             *
             * @return true when the heap is empty or holds only a deferred root
             */
            [[nodiscard]] bool exhausted() const noexcept
            {
                return heap_.size() == static_cast<std::size_t>(deferred_pop_);
            }

            /**
             * Reserves node capacity.
             *
             * A request below the current capacity (or negative) selects the game's
             * capacity + capacity/2 + 16 growth rule.
             *
             * @param requested_capacity nodes to reserve
             */
            void reserve(int32_t requested_capacity);

        private:
            struct Node
            {
                int32_t heap_position_or_next_free{-1};
                Payload value{};
            };

            static_assert(sizeof(Node) == 20);

            /**
             * Takes a node slot from the free chain, or a new one.
             *
             * @return the slot
             */
            [[nodiscard]] Handle allocate_slot();

            /**
             * Removes a live node, pushes its slot on the free chain and refills its position
             * from the last node.
             *
             * @param handle node to remove
             */
            void remove_slot(Handle handle);

            /**
             * Moves the node at a heap position up while its score is lower than its parent's.
             *
             * @param position_value heap position
             */
            void sift_up(std::size_t position_value);

            /**
             * Moves the node at a heap position down while a child has a lower score.
             *
             * @param position_value heap position
             * @quirk The left child is taken when both children's scores are equal.
             */
            void sift_down(std::size_t position_value);

            /**
             * Returns a node's search score, payload word 2 read as signed.
             *
             * @param value node
             * @return the score
             */
            [[nodiscard]] static int32_t score(const Node& value) noexcept;

            /**
             * Returns the node in a slot.
             *
             * @param handle node slot
             * @return the node
             */
            [[nodiscard]] Node& node(Handle handle);

            /**
             * Returns the node in a slot.
             *
             * @param handle node slot
             * @return the node
             */
            [[nodiscard]] const Node& node(Handle handle) const;

            std::vector<Node> nodes_;
            std::vector<Handle> heap_;
            int32_t free_head_{-1};
            std::size_t capacity_{};
            bool deferred_pop_{};
        };
    }
}
