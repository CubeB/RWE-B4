#pragma once

#include <algorithm>
#include <cassert>
#include <deque>
#include <functional>
#include <optional>
#include <rwe/util/OpaqueId.h>
#include <rwe/util/match.h>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace rwe
{
    template <typename T, typename IdTag>
    class VectorMap
    {
    private:
        using Id = OpaqueId<unsigned int, IdTag>;
        struct IndexTag;
        using Index = OpaqueId<unsigned int, IndexTag>;
        struct GenerationTag;
        using Generation = OpaqueId<unsigned int, GenerationTag>;

        struct FreeEntry
        {
            std::optional<Index> nextIndex;
            Id id;
            FreeEntry(Id id, const std::optional<Index>& nextIndex) : nextIndex(nextIndex), id(id) {}
        };

        using OccupiedEntry = std::pair<Id, T>;
        using Entry = std::variant<FreeEntry, OccupiedEntry>;

    public:
        template <typename Val, typename Ref, typename Ptr, typename UnderlyingIt>
        class VectorMapIter
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = Val;
            using reference = Ref;
            using pointer = Ptr;
            using difference_type = int; // not used

        private:
            UnderlyingIt it;
            UnderlyingIt end;

        public:
            VectorMapIter(
                UnderlyingIt it,
                UnderlyingIt end) : it(std::move(it)), end(std::move(end))
            {
            }

            VectorMapIter& operator++()
            {
                assert(it != end);
                it = firstValid(++it, end);
                return *this;
            }

            VectorMapIter operator++(int)
            {
                VectorMapIter tmp(*this);
                ++*this;
                return tmp;
            }

            reference operator*() const
            {
                return std::get<std::remove_const_t<value_type>>(*it);
            }

            pointer operator->() const
            {
                return &std::get<std::remove_const_t<value_type>>(*it);
            }

            bool operator==(const VectorMapIter& rhs) const
            {
                return it == rhs.it && end == rhs.end;
            }

            bool operator!=(const VectorMapIter& rhs) const
            {
                return !(rhs == *this);
            }
        };

        using iterator = VectorMapIter<OccupiedEntry, OccupiedEntry&, OccupiedEntry*, typename std::deque<Entry>::iterator>;
        using const_iterator = VectorMapIter<const OccupiedEntry, const OccupiedEntry&, const OccupiedEntry*, typename std::deque<Entry>::const_iterator>;

        /** See growCount. Cheap enough to assert against in a hot loop. */
        unsigned int generation() const
        {
            return growCount;
        }

        using key_type = Id;
        using mapped_type = T;
        using value_type = OccupiedEntry;

        /**
         * The shape of the map without its contents: one entry per slot,
         * live or freed, and the head of the free list.
         *
         * An id here is a slot number and a generation, the generation
         * advancing each time the slot is refilled, and `remove` keeps the
         * freed slot on a LIFO chain for `emplace` to hand out again. So two
         * maps holding the same members in the same order are not the same
         * map: the next `emplace` can land in different slots, which changes
         * the id it returns and where the member sits in iteration order. A
         * save that means to reproduce the game exactly has to carry this,
         * and this is the form it carries it in.
         */
        struct SlotLayout
        {
            /** The occupant's id, or for a freed slot the id its last occupant had. */
            unsigned int id;
            bool occupied;
            /** A freed slot's link to the next freed slot, in slot numbers. */
            std::optional<unsigned int> nextFreeIndex;
        };
        struct Layout
        {
            std::vector<SlotLayout> slots;
            std::optional<unsigned int> firstFreeIndex;
        };

        Layout layout() const
        {
            Layout l;
            l.slots.reserve(vec.size());
            for (const auto& entry : vec)
            {
                match(
                    entry,
                    [&](const FreeEntry& f) {
                        l.slots.push_back(SlotLayout{f.id.value, false, f.nextIndex ? std::make_optional(f.nextIndex->value) : std::nullopt});
                    },
                    [&](const OccupiedEntry& e) {
                        l.slots.push_back(SlotLayout{e.first.value, true, std::nullopt});
                    });
            }
            l.firstFreeIndex = firstFreeSlotIndex ? std::make_optional(firstFreeSlotIndex->value) : std::nullopt;
            return l;
        }

        /**
         * Throws away the contents and gives the map the given shape. Every
         * freed slot comes back freed, with its id and its place in the free
         * chain; every occupied slot comes back as an empty placeholder
         * waiting for emplaceInSlot, which is not on the free chain and so
         * cannot be handed out by emplace in the meantime.
         */
        void restoreLayout(const Layout& l)
        {
            vec.clear();
            for (const auto& slot : l.slots)
            {
                auto next = slot.nextFreeIndex ? std::make_optional(Index(*slot.nextFreeIndex)) : std::nullopt;
                vec.emplace_back(FreeEntry(Id(slot.id), slot.occupied ? std::nullopt : next));
            }
            firstFreeSlotIndex = l.firstFreeIndex ? std::make_optional(Index(*l.firstFreeIndex)) : std::nullopt;
            ++growCount;
        }

        /**
         * Fills a placeholder left by restoreLayout, under the id the layout
         * recorded for it -- not the next generation, which is what emplace
         * would hand out, because this is the same member coming back rather
         * than a new one moving in.
         */
        template <typename... Args>
        Id emplaceInSlot(unsigned int index, Args&&... args)
        {
            if (index >= vec.size())
            {
                throw std::runtime_error("emplaceInSlot: no such slot");
            }
            auto& entry = vec[index];
            const auto* freeEntry = std::get_if<FreeEntry>(&entry);
            if (freeEntry == nullptr)
            {
                throw std::runtime_error("emplaceInSlot: slot is already occupied");
            }
            auto id = freeEntry->id;
            entry = std::make_pair(id, T(std::forward<Args>(args)...));
            return id;
        }

    private:
        std::deque<Entry> vec;
        std::optional<Index> firstFreeSlotIndex;

        /**
         * How many times the deque behind this map has grown.
         *
         * Growing is the only thing here that invalidates an iterator, and
         * so the only thing a caller iterating the map has to care about.
         * Filling a free slot does not, and `remove` does not erase -- it
         * leaves a FreeEntry in place -- so neither of those disturbs a
         * walk in progress. Derived bookkeeping: never saved, hashed or
         * dumped, and it changes nothing about what the map contains.
         */
        unsigned int growCount{0};

    public:
        template <typename... Args>
        Id emplace(Args&&... args)
        {
            if (firstFreeSlotIndex)
            {
                auto index = *firstFreeSlotIndex;
                auto& entry = vec[index.value];
                const auto& freeEntry = std::get<FreeEntry>(vec[index.value]);
                firstFreeSlotIndex = freeEntry.nextIndex;
                auto newId = nextGeneration(freeEntry.id);
                entry = std::make_pair(newId, T(std::forward<Args>(args)...));
                return newId;
            }
            else
            {
                auto id = makeId(Index(vec.size()));
                vec.emplace_back(std::make_pair(id, T(std::forward<Args>(args)...)));
                ++growCount;
                return id;
            }
        }

        /**
         * Slots in use, live and freed alike. An upper bound on the number of
         * members rather than a count of them: freed slots are kept for reuse
         * and only an iteration could tell them apart. Cheap enough to read
         * from a crash handler, which is what it is for.
         */
        std::size_t slotCount() const
        {
            return vec.size();
        }

        void remove(Id id)
        {
            auto index = extractIndex(id);

            if (index.value >= vec.size())
            {
                throw std::runtime_error("Member with given ID does not exist");
            }

            auto& entry = vec[index.value];
            if (const auto f = std::get_if<OccupiedEntry>(&entry); f != nullptr && f->first == id)
            {
                entry = FreeEntry(id, firstFreeSlotIndex);
                firstFreeSlotIndex = index;
                return;
            }

            throw std::runtime_error("Member with given ID does not exist");
        }

        std::optional<std::reference_wrapper<T>> tryGet(Id id)
        {
            auto index = extractIndex(id);

            auto& entry = vec[index.value];
            return match(
                entry,
                [](const FreeEntry&) { return std::optional<std::reference_wrapper<T>>(); },
                [id](OccupiedEntry& e) {
                    return e.first == id
                        ? std::make_optional(std::ref(e.second))
                        : std::optional<std::reference_wrapper<T>>();
                });
        }

        std::optional<std::reference_wrapper<const T>> tryGet(Id id) const
        {
            auto index = extractIndex(id);

            const auto& entry = vec[index.value];
            return match(
                entry,
                [](const FreeEntry&) { return std::optional<std::reference_wrapper<const T>>(); },
                [id](const OccupiedEntry& e) {
                    return e.first == id
                        ? std::make_optional(std::ref(e.second))
                        : std::optional<std::reference_wrapper<const T>>();
                });
        }

        iterator begin()
        {
            return iterator(firstValid(vec.begin(), vec.end()), vec.end());
        }

        iterator end()
        {
            return iterator(vec.end(), vec.end());
        }

        const_iterator begin() const
        {
            return const_iterator(firstValid(vec.begin(), vec.end()), vec.end());
        }

        const_iterator end() const
        {
            return const_iterator(vec.end(), vec.end());
        }

        iterator erase(iterator it)
        {
            remove(it->first);
            return ++it;
        }

        const_iterator find(Id id) const
        {
            auto index = extractIndex(id);
            if (index.value >= vec.size())
            {
                return end();
            }

            auto it = vec.begin() + index.value;
            return match(
                *it,
                [&](const FreeEntry&) { return end(); },
                [&](const OccupiedEntry& e) { return e.first == id ? const_iterator(it, vec.end()) : end(); });
        }

        iterator find(Id id)
        {
            auto index = extractIndex(id);
            if (index.value >= vec.size())
            {
                return end();
            }

            auto it = vec.begin() + index.value;
            return match(
                *it,
                [&](const FreeEntry&) { return end(); },
                [&](OccupiedEntry& e) { return e.first == id ? iterator(it, vec.end()) : end(); });
        }

    private:
        static Index extractIndex(Id id)
        {
            return Index(id.value >> 8u);
        }

        static std::pair<Index, Generation> parseId(Id id)
        {
            return std::make_pair(extractIndex(id), Generation(id.value & 0xFFu));
        }

        static Id makeId(Index index)
        {
            return Id(index.value << 8u);
        }
        static Id makeId(Index index, Generation generation)
        {
            return Id((index.value << 8u) | generation.value);
        }

        static Id nextGeneration(Id id)
        {
            auto [index, generation] = parseId(id);
            return makeId(index, Generation((generation.value + 1) & 0xFFu));
        }

        template <typename It>
        static It firstValid(It it, It end)
        {
            return std::find_if(it, end, [](const auto& v) { return std::holds_alternative<OccupiedEntry>(v); });
        }
    };
}
