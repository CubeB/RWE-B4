#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <rwe/game/SaveJson.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/save_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/UnitState.h>
#include <span>
#include <string_view>
#include <variant>

namespace rwe
{
    using UnitStateFieldSaveFn = nlohmann::json (*)(const UnitState&, const SaveContext&);
    using UnitStateFieldLoadFn = void (*)(const nlohmann::json&, UnitState&, const LoadContext&);

    /** Hashed and dumped, saved and loaded. */
    struct UnitStateFieldHashed
    {
        GameHash (*hash)(const UnitState&);
        nlohmann::json (*dump)(const UnitState&);
        nlohmann::json (*save)(const UnitState&, const SaveContext&);
        void (*load)(const nlohmann::json&, UnitState&, const LoadContext&);
    };

    /** Saved and loaded, with a named reason for staying out of the sync hash. */
    struct UnitStateFieldUnhashed
    {
        nlohmann::json (*save)(const UnitState&, const SaveContext&);
        void (*load)(const nlohmann::json&, UnitState&, const LoadContext&);
        const char* whyNotHashed;
    };

    /** Saved only, with named reasons for staying out of both the sync hash and the load walk. */
    struct UnitStateFieldSaveOnly
    {
        nlohmann::json (*save)(const UnitState&, const SaveContext&);
        const char* whyNotHashed;
        const char* whyNotLoaded;
    };

    /**
     * One row per persisted UnitState field.
     *
     * The sync hash, the saved game and the desync dump each walk the unit's
     * fields, and between them those are three lists edited by hand whenever
     * the unit grows a member. The nanoPoint desync class lived exactly in
     * that hand-weaving: a member one list reads that another forgot is a
     * desync, or a silently lost save, and nothing tells you. This table is
     * the one place a new field is declared; the three walks are derived
     * from it.
     *
     * A row's `walks` says exactly which walks touch the field, and an
     * omitted walk is a named reason rather than a null step. Hash and dump
     * move together -- the dump exists to bisect a hash mismatch
     * (RWE_HASH_LOG for the tick, RWE_STATE_DUMP for the field), so it must
     * show what the hash reads and nothing more. Save and load move together
     * except through UnitStateFieldSaveOnly, which says why not. Real fields
     * genuinely do not participate in every walk -- `pieces` is restored at
     * unit emplacement, `previousPosition` is an interpolation stand-in, the
     * COB VM is derived from hashed events -- and forcing every row through
     * every walk would mean fake steps that lie.
     */
    struct UnitStateFieldTableEntry
    {
        /** The key under the save and the dump. */
        const char* name;

        std::variant<UnitStateFieldHashed, UnitStateFieldUnhashed, UnitStateFieldSaveOnly> walks;

        /**
         * Saves older than this row's key may omit it. The load walk then
         * hands a null value instead of failing on the missing key, and the
         * row's own step decides what the absence stands for.
         */
        bool mayBeMissing{false};
    };

    template <typename T>
    struct UnitStateFieldMemberOf;

    template <typename C, typename M>
    struct UnitStateFieldMemberOf<M C::*>
    {
        using type = M;
    };

    template <auto Member>
    GameHash hashUnitStateField(const UnitState& u)
    {
        return computeHashOf(u.*Member);
    }

    template <auto Member>
    nlohmann::json dumpUnitStateField(const UnitState& u)
    {
        return dumpJson(u.*Member);
    }

    /** A member nlohmann likes as it stands: the numbers, bools and strings. */
    template <auto Member>
    nlohmann::json savePlainUnitStateField(const UnitState& u, const SaveContext&)
    {
        return u.*Member;
    }

    template <auto Member>
    void loadPlainUnitStateField(const nlohmann::json& j, UnitState& u, const LoadContext&)
    {
        u.*Member = j.get<typename UnitStateFieldMemberOf<decltype(Member)>::type>();
    }

    template <auto Member>
    constexpr UnitStateFieldHashed hashedPlain()
    {
        return UnitStateFieldHashed{
            &hashUnitStateField<Member>,
            &dumpUnitStateField<Member>,
            &savePlainUnitStateField<Member>,
            &loadPlainUnitStateField<Member>};
    }

    template <auto Member>
    constexpr UnitStateFieldHashed hashed(UnitStateFieldSaveFn save, UnitStateFieldLoadFn load)
    {
        return UnitStateFieldHashed{&hashUnitStateField<Member>, &dumpUnitStateField<Member>, save, load};
    }

    template <auto Member>
    constexpr UnitStateFieldUnhashed unhashedPlain(const char* whyNotHashed)
    {
        return UnitStateFieldUnhashed{&savePlainUnitStateField<Member>, &loadPlainUnitStateField<Member>, whyNotHashed};
    }

    template <auto Member>
    constexpr UnitStateFieldUnhashed unhashed(UnitStateFieldSaveFn save, UnitStateFieldLoadFn load, const char* whyNotHashed)
    {
        return UnitStateFieldUnhashed{save, load, whyNotHashed};
    }

    constexpr UnitStateFieldSaveOnly saveOnly(UnitStateFieldSaveFn save, const char* whyNotHashed, const char* whyNotLoaded)
    {
        return UnitStateFieldSaveOnly{save, whyNotHashed, whyNotLoaded};
    }

    constexpr bool unitStateFieldReasonNamed(const char* s)
    {
        return s != nullptr && s[0] != '\0';
    }

    constexpr bool unitStateFieldTableWellFormed(const UnitStateFieldTableEntry* table, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            const auto& e = table[i];
            if (e.name == nullptr || e.name[0] == '\0')
            {
                return false;
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                if (std::string_view(table[j].name) == std::string_view(e.name))
                {
                    return false;
                }
            }
            if (const auto* h = std::get_if<UnitStateFieldHashed>(&e.walks))
            {
                if (h->hash == nullptr || h->dump == nullptr || h->save == nullptr || h->load == nullptr)
                {
                    return false;
                }
            }
            else if (const auto* u = std::get_if<UnitStateFieldUnhashed>(&e.walks))
            {
                if (u->save == nullptr || u->load == nullptr || !unitStateFieldReasonNamed(u->whyNotHashed))
                {
                    return false;
                }
            }
            else
            {
                const auto* s = std::get_if<UnitStateFieldSaveOnly>(&e.walks);
                if (s->save == nullptr || !unitStateFieldReasonNamed(s->whyNotHashed) || !unitStateFieldReasonNamed(s->whyNotLoaded))
                {
                    return false;
                }
            }
        }
        return true;
    }

    template <std::size_t N>
    constexpr bool unitStateFieldTableWellFormed(const UnitStateFieldTableEntry (&table)[N])
    {
        return unitStateFieldTableWellFormed(table, N);
    }

    /** Every persisted UnitState field, in the order the save writes them. */
    std::span<const UnitStateFieldTableEntry> unitStateFieldTable();
}
