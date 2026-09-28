// Ported from OpenAnnihilation, src/sim/ground-orders/src/search_worker.cpp.
// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "OaSearchWorker.h"

#include "OaSearchTrace.h"

#include <bit>
#include <limits>
#include <stdexcept>

namespace rwe
{
    namespace oa
    {
        namespace
        {
            int32_t wrap_neg(int32_t value) noexcept
            {
                return static_cast<int32_t>(0u - static_cast<uint32_t>(value));
            }

            SearchHeap::Payload pack_node(
                int16_t x, int16_t z, int32_t g, int32_t f, int16_t extra, int16_t consecutive)
            {
                SearchHeap::Payload value{};
                value[0] = static_cast<uint32_t>(static_cast<uint16_t>(x)) | (static_cast<uint32_t>(static_cast<uint16_t>(z)) << 16);
                value[1] = std::bit_cast<uint32_t>(g);
                value[2] = std::bit_cast<uint32_t>(f);
                value[3] = static_cast<uint32_t>(static_cast<uint16_t>(extra)) | (static_cast<uint32_t>(static_cast<uint16_t>(consecutive)) << 16);
                return value;
            }

            int16_t packed_x(const SearchHeap::Payload& value) noexcept
            {
                return std::bit_cast<int16_t>(static_cast<uint16_t>(value[0]));
            }

            int16_t packed_z(const SearchHeap::Payload& value) noexcept
            {
                return std::bit_cast<int16_t>(static_cast<uint16_t>(value[0] >> 16));
            }

            int32_t packed_g(const SearchHeap::Payload& value) noexcept
            {
                return std::bit_cast<int32_t>(value[1]);
            }

            int16_t packed_extra(const SearchHeap::Payload& value) noexcept
            {
                return std::bit_cast<int16_t>(static_cast<uint16_t>(value[3]));
            }

            int16_t packed_run(const SearchHeap::Payload& value) noexcept
            {
                return std::bit_cast<int16_t>(static_cast<uint16_t>(value[3] >> 16));
            }

            uint8_t heading_direction(uint16_t heading) noexcept
            {
                // The heading is zero-extended before the bias and the shift, so the
                // direction depends on the heading alone.
                const auto biased = static_cast<int32_t>(static_cast<uint32_t>(heading) + search_heading_bias);
                return static_cast<uint8_t>(
                           biased >> static_cast<int32_t>(search_heading_shift))
                    & 7u;
            }

            void clear_dirty_cells(std::span<SearchMapCell> cells, std::span<uint32_t> dirty)
            {
                const auto cell_count = static_cast<int32_t>(cells.size());
                auto groups = (cell_count + 0xff) >> 8;
                groups -= 1;
                int32_t group = 0;
                auto clear_eight = [&](int32_t index, bool bounded) {
                    for (int n = 8; n != 0; --n)
                    {
                        if (!bounded || index < cell_count)
                        {
                            cells[static_cast<std::size_t>(index)].flags = 0;
                        }
                        ++index;
                    }
                };
                if (groups > 0)
                {
                    do
                    {
                        auto bits = dirty[static_cast<std::size_t>(group)];
                        if (bits != 0)
                        {
                            dirty[static_cast<std::size_t>(group)] = 0;
                            auto index = group << 8;
                            for (; bits != 0; bits >>= 1)
                            {
                                if ((bits & 1u) != 0)
                                {
                                    clear_eight(index, false);
                                }
                                index += 8;
                            }
                        }
                        ++group;
                    } while (group < groups);
                }
                auto bits = dirty[static_cast<std::size_t>(group)];
                if (bits == 0)
                {
                    return;
                }
                dirty[static_cast<std::size_t>(group)] = 0;
                auto index = group << 8;
                for (; bits != 0; bits >>= 1)
                {
                    if ((bits & 1u) != 0)
                    {
                        clear_eight(index, true);
                    }
                    index += 8;
                }
            }
        }

        void SearchCellGrid::allocate(int32_t map_width, int32_t map_height)
        {
            width = map_width;
            height = map_height;
            // 32-bit multiply, then `product + 7` masked with ~7.
            const auto product = static_cast<uint32_t>(map_width) * static_cast<uint32_t>(map_height);
            const auto rounded = (product + uint32_t{7}) & ~uint32_t{7};
            count = rounded;
            // Every cell starts cleared, so a reused grid cannot keep old flags.
            cells.assign(static_cast<std::size_t>(rounded), {});
        }

        bool crossed_start_goal_axis(
            int32_t start_x,
            int32_t start_z,
            int32_t goal_x,
            int32_t goal_z,
            int32_t cell_x,
            int32_t cell_z) noexcept
        {
            auto dx_goal = goal_x - start_x;
            auto dx_cell = cell_x - start_x;
            auto dz_cell = cell_z - start_z;
            auto dz_goal = goal_z - start_z;
            if (dx_goal < 0)
            {
                dx_goal = wrap_neg(dx_goal);
                dx_cell = wrap_neg(dx_cell);
            }
            if (dz_goal < 0)
            {
                dz_goal = wrap_neg(dz_goal);
                dz_cell = wrap_neg(dz_cell);
            }
            if (dz_cell == 0 && dx_cell > 0)
            {
                return dx_cell <= dx_goal;
            }
            if (dx_cell != dx_goal)
            {
                return false;
            }
            if (dz_cell <= 0)
            {
                return false;
            }
            return dz_cell <= dz_goal;
        }

        int32_t search_player_credit(int32_t tick_credit, uint16_t player_count) noexcept
        {
            if (player_count == 0)
            {
                return 0;
            }
            return tick_credit / static_cast<int32_t>(player_count);
        }

        int32_t search_rescaled_heuristic(int32_t usage_count, uint16_t units_per_player, int32_t base)
        {
            if (units_per_player == 0)
            {
                throw std::domain_error("path search units-per-player divisor is zero");
            }
            const auto usage = usage_count / static_cast<int32_t>(units_per_player);
            if (usage < 1)
            {
                return base * 6;
            }
            if (usage < 2)
            {
                return base * 3;
            }
            return base;
        }

        SearchMapCell SearchWorker::cell(uint32_t x, uint32_t z) const
        {
            if (!in_map(x, z))
            {
                throw std::out_of_range("search map cell is outside the allocated grid");
            }
            return at(x, z);
        }

        bool SearchWorker::in_map(uint32_t x, uint32_t z) const noexcept
        {
            return x < width_ && z < height_;
        }

        SearchMapCell& SearchWorker::at(uint32_t x, uint32_t z)
        {
            return cells_[static_cast<std::size_t>(z) * width_ + x];
        }

        const SearchMapCell& SearchWorker::at(uint32_t x, uint32_t z) const
        {
            return cells_[static_cast<std::size_t>(z) * width_ + x];
        }

        void SearchWorker::install_turn_tables()
        {
            turn_penalty_ = search_turn_penalty;
            for (std::size_t direction = 0; direction < move_cost_.size(); ++direction)
            {
                move_cost_[direction] = (direction & 1u) ? search_diagonal_cost : search_cardinal_cost;
            }
        }

        void SearchWorker::mark_dirty(uint32_t index)
        {
            dirty_[index >> 8] |= 1u << ((index >> 3) & 31u);
        }

        void SearchWorker::clear_dirty()
        {
            clear_dirty_cells(cells_, dirty_);
        }

        void SearchWorker::mark_goal(int32_t x, int32_t z)
        {
            const auto ux = static_cast<uint32_t>(x);
            const auto uz = static_cast<uint32_t>(z);
            if (!in_map(ux, uz))
            {
                return;
            }
            const auto index = uz * width_ + ux;
            at(ux, uz).flags = search_goal_bit;
            mark_dirty(index);
        }

        uint32_t SearchWorker::classify(int32_t x, int32_t z) const
        {
            if (x < 0 || z < 0 || static_cast<uint32_t>(x) >= width_ || static_cast<uint32_t>(z) >= height_)
            {
                return 0;
            }
            if (!classify_)
            {
                return 0;
            }
            return classify_(classifyContext_, x, z);
        }

        int32_t SearchWorker::heuristic(int32_t x, int32_t z) const
        {
            const auto cost = goalCost(*record_.goal, x, z);
            const auto product = int64_t(cost) * int64_t(heuristic_scale_);
            return static_cast<int32_t>(product >> 16);
        }

        void SearchWorker::expand_neighbor(
            const SearchHeap::Payload& parent, uint8_t parent_direction, uint32_t relative_turn)
        {
            const auto direction = (static_cast<uint32_t>(parent_direction) + relative_turn) & 7u;
            const auto x = static_cast<int32_t>(packed_x(parent)) + search_direction_x[direction];
            const auto z = static_cast<int32_t>(packed_z(parent)) + search_direction_z[direction];
            const auto ux = static_cast<uint32_t>(x);
            const auto uz = static_cast<uint32_t>(z);
            if (!in_map(ux, uz))
            {
                return;
            }
            auto& cell = at(ux, uz);
            const auto visit = static_cast<SearchCellVisit>(cell.flags & 3u);
            if (visit == SearchCellVisit::unvisited)
            {
                const auto index = uz * width_ + ux;
                mark_dirty(index);
                const auto classification = classify(x, z);
                if (classification < 1 && (cell.flags & search_seed_bit) == 0)
                {
                    cell.flags = static_cast<uint8_t>(cell.flags | 3u);
                    return;
                }
                const auto estimate = heuristic(x, z);
                if (estimate > threshold_)
                {
                    cell.flags = static_cast<uint8_t>(cell.flags | 1u);
                }
                else
                {
                    cell.flags = static_cast<uint8_t>(cell.flags | 5u);
                }
                cell.predecessor = static_cast<uint8_t>(direction);
                const auto extra = static_cast<int16_t>(
                    (1 < static_cast<int32_t>(classification)) ? 0 : search_difficult_extra);
                auto g = static_cast<int32_t>(move_cost_[direction]) + static_cast<int32_t>(turn_penalty_[relative_turn]) + packed_g(parent) + extra;
                const auto run = relative_turn == 0 ? static_cast<int16_t>(packed_run(parent) + 1) : int16_t{1};
                if (relative_turn != 0 && packed_run(parent) < search_early_run_limit)
                {
                    g += search_early_turn_penalty;
                }
                const auto f = g + estimate;
                const auto handle = heap_.insert(
                    pack_node(static_cast<int16_t>(x), static_cast<int16_t>(z), g, f, extra, run));
                cell.handle = static_cast<uint16_t>(handle);
                return;
            }
            if (visit != SearchCellVisit::open)
            {
                return;
            }
            auto& node = heap_.payload(cell.handle);
            auto g = static_cast<int32_t>(move_cost_[direction]) + static_cast<int32_t>(turn_penalty_[relative_turn]) + packed_extra(node) + packed_g(parent);
            if (relative_turn != 0 && packed_run(parent) < search_early_run_limit)
            {
                g += search_early_turn_penalty;
            }
            if (g >= packed_g(node))
            {
                return;
            }
            cell.predecessor = static_cast<uint8_t>(direction);
            const auto delta = g - packed_g(node);
            node[1] = std::bit_cast<uint32_t>(g);
            node[2] = std::bit_cast<uint32_t>(std::bit_cast<int32_t>(node[2]) + delta);
            const auto run = relative_turn == 0 ? static_cast<int16_t>(packed_run(parent) + 1) : int16_t{1};
            node[3] = (node[3] & 0xffffu) | (static_cast<uint32_t>(static_cast<uint16_t>(run)) << 16);
            heap_.decrease_key(cell.handle);
        }

        uint32_t SearchWorker::expand_best()
        {
            const auto parent = heap_.peek();
            heap_.pop();
            const auto x = static_cast<uint32_t>(static_cast<int32_t>(packed_x(parent)));
            const auto z = static_cast<uint32_t>(static_cast<int32_t>(packed_z(parent)));
            auto& cell = at(x, z);
            if ((cell.flags & search_goal_bit) != 0)
            {
                finish_ = {packed_x(parent), packed_z(parent)};
                return 1;
            }
            cell.flags = 2;
            auto relative = -fan_;
            if (relative <= fan_)
            {
                do
                {
                    expand_neighbor(parent, cell.predecessor, static_cast<uint32_t>(relative) & 7u);
                    ++relative;
                } while (relative <= fan_);
            }
            return 0;
        }

        int32_t SearchWorker::seed_wall_follow()
        {
            auto min_cost = heuristic(start_[0], start_[1]);
            if (classify(start_[0], start_[1]) < 1)
            {
                return min_cost;
            }
            auto current_x = static_cast<int32_t>(start_[0]);
            auto current_z = static_cast<int32_t>(start_[1]);
            const auto seed_limit = static_cast<int32_t>(width_ * height_ * 8u + 8u);
            while (true)
            {
                ++budget_;
                if (budget_ > seed_limit)
                {
                    return min_cost;
                }
                if (min_cost == 0)
                {
                    return 0;
                }
                uint8_t heading = 0;
                const auto dx = nearest_[0] - current_x;
                if (dx < 0)
                {
                    heading = 2;
                }
                else if (dx > 0)
                {
                    heading = 6;
                }
                else if (nearest_[1] - current_z > 0)
                {
                    heading = 4;
                }
                const auto next_x = current_x + search_direction_x[heading];
                const auto next_z = current_z + search_direction_z[heading];
                if (classify(next_x, next_z) >= 1)
                {
                    const auto ux = static_cast<uint32_t>(next_x);
                    const auto uz = static_cast<uint32_t>(next_z);
                    const auto index = uz * width_ + ux;
                    mark_dirty(index);
                    auto& cell = at(ux, uz);
                    cell.flags = static_cast<uint8_t>(cell.flags | search_seed_bit);
                    cell.predecessor = heading;
                    if ((cell.flags & search_goal_bit) != 0)
                    {
                        return 0;
                    }
                    const auto cost = heuristic(next_x, next_z);
                    current_x = next_x;
                    current_z = next_z;
                    if (cost < min_cost)
                    {
                        min_cost = cost;
                    }
                    continue;
                }
                auto left_x = current_x;
                auto left_z = current_z;
                auto right_x = current_x;
                auto right_z = current_z;
                heading = static_cast<uint8_t>(heading + 2) & 7u;
                auto seen = false;
                auto left_dir = heading;
                auto right_dir = heading;
                while (true)
                {
                    ++budget_;
                    if (budget_ > seed_limit)
                    {
                        return min_cost;
                    }
                    const auto left_stop = static_cast<uint8_t>(left_dir - 3) & 7u;
                    left_dir = static_cast<uint8_t>(left_dir - 2) & 7u;
                    auto nx = left_x + search_direction_x[left_dir];
                    auto nz = left_z + search_direction_z[left_dir];
                    while (classify(nx, nz) < 1)
                    {
                        if (left_dir == left_stop)
                        {
                            return min_cost;
                        }
                        left_dir = static_cast<uint8_t>(left_dir + 1) & 7u;
                        nx = left_x + search_direction_x[left_dir];
                        nz = left_z + search_direction_z[left_dir];
                    }
                    if (left_x == right_x && left_z == right_z && left_dir == right_dir && seen)
                    {
                        return min_cost;
                    }
                    left_x = nx;
                    left_z = nz;
                    seen = true;
                    const auto lux = static_cast<uint32_t>(nx);
                    const auto luz = static_cast<uint32_t>(nz);
                    const auto lindex = luz * width_ + lux;
                    mark_dirty(lindex);
                    auto& left_cell = at(lux, luz);
                    left_cell.flags = static_cast<uint8_t>(left_cell.flags | search_seed_bit);
                    left_cell.predecessor = left_dir;
                    if ((left_cell.flags & search_goal_bit) != 0)
                    {
                        return 0;
                    }
                    if (crossed_start_goal_axis(
                            current_x, current_z, nearest_[0], nearest_[1], nx, nz))
                    {
                        current_x = nx;
                        current_z = nz;
                        break;
                    }
                    const auto left_cost = heuristic(nx, nz);
                    if (left_cost < min_cost)
                    {
                        min_cost = left_cost;
                    }
                    const auto right_stop = static_cast<uint8_t>(right_dir + 3) & 7u;
                    right_dir = static_cast<uint8_t>(right_dir + 2) & 7u;
                    auto rx = right_x - search_direction_x[right_dir];
                    auto rz = right_z - search_direction_z[right_dir];
                    while (classify(rx, rz) < 1)
                    {
                        if (right_dir == right_stop)
                        {
                            return min_cost;
                        }
                        right_dir = static_cast<uint8_t>(right_dir - 1) & 7u;
                        rx = right_x - search_direction_x[right_dir];
                        rz = right_z - search_direction_z[right_dir];
                    }
                    if (left_x == right_x && left_z == right_z && right_dir == left_dir)
                    {
                        return min_cost;
                    }
                    right_x = rx;
                    right_z = rz;
                    const auto rux = static_cast<uint32_t>(rx);
                    const auto ruz = static_cast<uint32_t>(rz);
                    const auto rindex = ruz * width_ + rux;
                    mark_dirty(rindex);
                    auto& right_cell = at(rux, ruz);
                    right_cell.flags = static_cast<uint8_t>(right_cell.flags | search_seed_bit);
                    right_cell.predecessor = right_dir;
                    if ((right_cell.flags & search_goal_bit) != 0)
                    {
                        return 0;
                    }
                    if (crossed_start_goal_axis(
                            current_x, current_z, nearest_[0], nearest_[1], rx, rz))
                    {
                        current_x = rx;
                        current_z = rz;
                        break;
                    }
                    const auto right_cost = heuristic(rx, rz);
                    if (right_cost < min_cost)
                    {
                        min_cost = right_cost;
                    }
                }
            }
        }

        void SearchWorker::fail_empty()
        {
            route_.clear();
        }

        void SearchWorker::publish_route()
        {
            std::vector<uint8_t> predecessor(static_cast<std::size_t>(width_) * height_);
            for (std::size_t i = 0; i < cells_.size(); ++i)
            {
                predecessor[i] = cells_[i].predecessor;
            }
            route_ = reconstructSearchPath(
                width_,
                height_,
                start_,
                finish_,
                record_.unit->footprintX,
                record_.unit->footprintZ,
                predecessor);
        }

        SearchAdvance SearchWorker::begin(SearchRecord record, const SearchBegin& begin)
        {
            if (!record.unit || !record.goal || !begin.classify)
            {
                throw std::invalid_argument("path search record requires its unit, goal and classifier");
            }
            record_ = record;
            classify_ = begin.classify;
            classifyContext_ = begin.classifyContext;
            heuristic_scale_ = begin.heuristicScale;
            order_flags_ = 0;
            route_.clear();
            budget_ = 0;
            const auto next_width = begin.mapWidth;
            const auto next_height = begin.mapHeight;
            if (next_width < 0 || next_height < 0)
            {
                throw std::length_error("search map dimensions are negative");
            }
            const auto unsigned_width = static_cast<std::size_t>(next_width);
            const auto unsigned_height = static_cast<std::size_t>(next_height);
            const auto count = unsigned_width * unsigned_height;
            if (unsigned_width != 0 && count / unsigned_width != unsigned_height)
            {
                throw std::length_error("search map dimensions overflow");
            }
            const auto dirty_words = static_cast<std::size_t>((static_cast<uint64_t>(count) + 0xff) >> 8);
            if (width_ != static_cast<uint32_t>(next_width) || height_ != static_cast<uint32_t>(next_height) || cells_.size() != count)
            {
                width_ = static_cast<uint32_t>(next_width);
                height_ = static_cast<uint32_t>(next_height);
                cells_.assign(count, {});
                dirty_.assign(dirty_words == 0 ? 1 : dirty_words, 0);
            }
            const auto& unit = *record_.unit;
            start_ = {unit.cellX, unit.cellZ};
            install_turn_tables();
            clear_dirty();
            nearest_ = {start_[0], start_[1]};
            auto best = std::numeric_limits<int32_t>::max();
            forEachGoalCell(*record_.goal, [&](int32_t x, int32_t z) {
                mark_goal(x, z);
                const auto dz = static_cast<int32_t>(start_[1]) - z;
                const auto dx = static_cast<int32_t>(start_[0]) - x;
                const auto distance = dz * dz + dx * dx;
                if (distance < best)
                {
                    nearest_ = {static_cast<int16_t>(x), static_cast<int16_t>(z)};
                    best = distance;
                }
            });
            if (goalContains(*record_.goal, start_[0], start_[1]))
            {
                order_flags_ |= search_goal_contact_flag;
                fail_empty();
                return SearchAdvance::failed;
            }
            const auto start_cost = heuristic(start_[0], start_[1]);
            if (static_cast<uint32_t>(start_[0]) >= width_ || static_cast<uint32_t>(start_[1]) >= height_)
            {
                order_flags_ |= search_seed_unresolved_flag;
                fail_empty();
                return SearchAdvance::failed;
            }
            const auto seed = seed_wall_follow();
            threshold_ = seed;
            if (seed == 0)
            {
                order_flags_ |= search_goal_contact_flag;
            }
            else
            {
                order_flags_ |= search_seed_unresolved_flag;
                if (seed >= start_cost)
                {
                    fail_empty();
                    return SearchAdvance::failed;
                }
            }
            heap_.reset_open_set();
            const auto index = static_cast<uint32_t>(start_[1]) * width_ + static_cast<uint32_t>(start_[0]);
            mark_dirty(index);
            auto& start_cell = at(static_cast<uint32_t>(start_[0]), static_cast<uint32_t>(start_[1]));
            start_cell.flags = static_cast<uint8_t>(start_cell.flags | 1u);
            start_cell.predecessor = heading_direction(unit.heading);
            // The start node's extra (payload word 3) is zero; it is never read, because
            // the start cell is closed on first pop and closed cells are not reopened.
            const auto handle = heap_.insert(pack_node(start_[0], start_[1], 0, start_cost, 0, search_start_run));
            start_cell.handle = static_cast<uint16_t>(handle);
            fan_ = search_initial_fan;
            return SearchAdvance::in_progress;
        }

        SearchAdvance SearchWorker::advance_slice()
        {
            budget_ = 0;
            if (heap_.exhausted())
            {
                fail_empty();
                return SearchAdvance::failed;
            }
            do
            {
                if (heap_.exhausted())
                {
                    return SearchAdvance::in_progress;
                }
                ++budget_;
                if (expand_best() != 0)
                {
                    publish_route();
                    return SearchAdvance::succeeded;
                }
                fan_ = search_continue_fan;
            } while (budget_ < search_slice_expansions);
            return SearchAdvance::in_progress;
        }
    }
}
