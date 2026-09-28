// Ported from OpenAnnihilation, src/sim/ground-orders/src/search_heap.cpp.
// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "OaSearchHeap.h"

#include <bit>
#include <limits>
#include <stdexcept>

namespace rwe
{
    namespace oa
    {
        int32_t SearchHeap::score(const Node& value) noexcept
        {
            return std::bit_cast<int32_t>(value.value[2]);
        }

        SearchHeap::Node& SearchHeap::node(Handle handle)
        {
            if (handle >= nodes_.size())
            {
                throw std::out_of_range("search heap handle is outside allocated slots");
            }
            return nodes_[handle];
        }

        const SearchHeap::Node& SearchHeap::node(Handle handle) const
        {
            if (handle >= nodes_.size())
            {
                throw std::out_of_range("search heap handle is outside allocated slots");
            }
            return nodes_[handle];
        }

        SearchHeap::Payload& SearchHeap::payload(Handle handle)
        {
            return node(handle).value;
        }

        const SearchHeap::Payload& SearchHeap::payload(Handle handle) const
        {
            return node(handle).value;
        }

        std::size_t SearchHeap::position(Handle handle) const
        {
            const auto value = node(handle).heap_position_or_next_free;
            if (value < 0 || static_cast<std::size_t>(value) >= heap_.size() || heap_[static_cast<std::size_t>(value)] != handle)
            {
                throw std::out_of_range("search heap handle is not active");
            }
            return static_cast<std::size_t>(value);
        }

        void SearchHeap::reset_open_set()
        {
            nodes_.clear();
            heap_.clear();
            free_head_ = -1;
            deferred_pop_ = false;
        }

        void SearchHeap::reserve(int32_t requested_capacity)
        {
            std::size_t next{};
            if (requested_capacity < 0 || static_cast<std::size_t>(requested_capacity) < capacity_)
            {
                if (capacity_ > (std::numeric_limits<std::size_t>::max() - 16) / 3 * 2)
                {
                    throw std::length_error("search heap capacity overflow");
                }
                next = capacity_ + capacity_ / 2 + 16;
            }
            else
            {
                next = static_cast<std::size_t>(requested_capacity);
            }
            if (next < nodes_.size())
            {
                throw std::length_error("search heap capacity is below allocated slots");
            }
            nodes_.reserve(next);
            heap_.reserve(next);
            capacity_ = next;
        }

        SearchHeap::Handle SearchHeap::allocate_slot()
        {
            if (free_head_ == -1)
            {
                if (nodes_.size() >= std::numeric_limits<Handle>::max())
                {
                    throw std::length_error("search heap handle space exhausted");
                }
                const auto result = static_cast<Handle>(nodes_.size());
                nodes_.emplace_back();
                return result;
            }
            const auto result = static_cast<Handle>(free_head_);
            free_head_ = node(result).heap_position_or_next_free;
            return result;
        }

        void SearchHeap::sift_up(std::size_t position_value)
        {
            if (position_value == 0)
            {
                return;
            }
            const auto moving = heap_[position_value];
            auto parent = (position_value - 1) / 2;
            if (score(node(moving)) >= score(node(heap_[parent])))
            {
                return;
            }
            heap_[position_value] = heap_[parent];
            node(heap_[position_value]).heap_position_or_next_free = static_cast<int32_t>(position_value);
            while (parent != 0)
            {
                const auto next_parent = (parent - 1) / 2;
                if (score(node(moving)) >= score(node(heap_[next_parent])))
                {
                    break;
                }
                heap_[parent] = heap_[next_parent];
                node(heap_[parent]).heap_position_or_next_free = static_cast<int32_t>(parent);
                parent = next_parent;
            }
            heap_[parent] = moving;
            node(moving).heap_position_or_next_free = static_cast<int32_t>(parent);
        }

        void SearchHeap::sift_down(std::size_t position_value)
        {
            const auto moving = heap_[position_value];
            while (true)
            {
                const auto left = position_value * 2 + 1;
                if (left >= heap_.size())
                {
                    break;
                }
                const auto right = left + 1;
                auto selected = left;
                // The left child is picked when the scores compare equal.
                if (right < heap_.size() && score(node(heap_[right])) < score(node(heap_[left])))
                {
                    selected = right;
                }
                if (score(node(moving)) <= score(node(heap_[selected])))
                {
                    break;
                }
                heap_[position_value] = heap_[selected];
                node(heap_[position_value]).heap_position_or_next_free = static_cast<int32_t>(position_value);
                position_value = selected;
            }
            heap_[position_value] = moving;
            node(moving).heap_position_or_next_free = static_cast<int32_t>(position_value);
        }

        void SearchHeap::remove_slot(Handle handle)
        {
            const auto removed_position = position(handle);
            node(handle).heap_position_or_next_free = free_head_;
            free_head_ = static_cast<int32_t>(handle);
            const auto last = heap_.back();
            heap_.pop_back();
            if (removed_position < heap_.size())
            {
                heap_[removed_position] = last;
                node(last).heap_position_or_next_free = static_cast<int32_t>(removed_position);
                sift_down(removed_position);
            }
        }

        SearchHeap::Handle SearchHeap::insert(const Payload& value)
        {
            if (deferred_pop_)
            {
                if (heap_.empty())
                {
                    throw std::out_of_range("cannot replace an empty deferred search heap root");
                }
                const auto result = heap_.front();
                node(result).value = value;
                sift_down(0);
                deferred_pop_ = false;
                return result;
            }
            if (heap_.size() == capacity_)
            {
                reserve(-1);
            }
            const auto result = allocate_slot();
            const auto position_value = heap_.size();
            node(result).value = value;
            node(result).heap_position_or_next_free = static_cast<int32_t>(position_value);
            heap_.push_back(result);
            sift_up(position_value);
            return result;
        }

        void SearchHeap::decrease_key(Handle handle)
        {
            if (deferred_pop_)
            {
                if (heap_.empty())
                {
                    throw std::out_of_range(
                        "cannot decrease a key in an empty deferred search heap");
                }
                const auto deferred_root = heap_.front();
                sift_up(position(handle));
                if (node(deferred_root).heap_position_or_next_free != 0)
                {
                    deferred_pop_ = false;
                    remove_slot(deferred_root);
                }
                return;
            }
            sift_up(position(handle));
        }

        const SearchHeap::Payload& SearchHeap::peek()
        {
            if (deferred_pop_)
            {
                if (heap_.empty())
                {
                    throw std::out_of_range("cannot realize a pop on an empty search heap");
                }
                deferred_pop_ = false;
                remove_slot(heap_.front());
            }
            if (heap_.empty())
            {
                throw std::out_of_range("cannot peek an empty search heap");
            }
            return node(heap_.front()).value;
        }

        void SearchHeap::pop()
        {
            if (heap_.empty())
            {
                throw std::out_of_range("cannot pop an empty search heap");
            }
            if (!deferred_pop_)
            {
                deferred_pop_ = true;
                return;
            }
            remove_slot(heap_.front());
        }
    }
}
