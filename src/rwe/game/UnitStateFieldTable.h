#pragma once

#include <nlohmann/json.hpp>
#include <rwe/game/SaveJson.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/save_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/UnitState.h>
#include <vector>

namespace rwe
{
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
     * A row carries the member pointer plus one step per walk. The common
     * shapes get their steps generated straight from the pointer -- the hash
     * step is precisely what the hand-written `combineHashes` list used to
     * evaluate per field, so the bytes cannot move. Types whose treatment is
     * genuinely special keep their specialised helpers and the row
     * references them: the weapon array serialises through the unit's own
     * COB environment, the order queue through id remapping, the behaviour
     * and navigation variants through their kinded encodings. A null step
     * says that one walk does not touch the field -- a hashed-then-saved
     * field has all three, and the pieces and the COB environment are
     * handled at unit emplacement on the load side.
     */
    struct UnitStateFieldTableEntry
    {
        /** The key under the save and the dump. */
        const char* name;

        /** Feeds the sync hash; null when that walk does not read the field. */
        GameHash (*hash)(const UnitState&);

        /** Serialises the field onto the save; null when the save leaves it out. */
        nlohmann::json (*save)(const UnitState&, const SaveContext&);

        /** Restores the field from its own sub-json value; null when the load handles it elsewhere. */
        void (*load)(const nlohmann::json& value, UnitState& unit, const LoadContext& ctx);

        /** Renders the field for the desync dump; null when that walk leaves it out. */
        nlohmann::json (*dump)(const UnitState&);

        /**
         * Saves older than this row's key may omit it. The load walk then
         * hands a null value instead of failing on the missing key, and the
         * row's own step decides what the absence stands for.
         */
        bool mayBeMissing{false};
    };

    /** Every persisted UnitState field, in the order the save writes them. */
    const std::vector<UnitStateFieldTableEntry>& unitStateFieldTable();
}
