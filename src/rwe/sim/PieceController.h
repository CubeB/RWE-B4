#pragma once

#include <rwe/sim/SimAxis.h>
#include <rwe/sim/UnitId.h>
#include <string>

namespace rwe
{
    struct GameSimulation;

    /**
     * The verbs a unit's COB script uses on its own pieces and state that
     * nothing else in the engine calls (issue #117): showing, hiding,
     * shading and caching a piece, asking whether a piece is still moving or
     * turning, a transport script's attach-unit and drop-unit, and the three
     * flags a script sets on its own unit.
     *
     * They were thirty-odd methods on GameSimulation by the first count,
     * `cob.cpp` being their only caller; measured, eighteen of those have
     * other callers too and stay where they are, and these thirteen are the
     * ones that were the VM's alone. A thin wrapper over the simulation it is
     * made with, and nothing more: it holds no state, and every write goes
     * straight to the unit, as it always did.
     */
    class PieceController
    {
    public:
        explicit PieceController(GameSimulation& simulation) : simulation(simulation) {}

        void showObject(UnitId unitId, const std::string& name);

        void hideObject(UnitId unitId, const std::string& name);

        void enableShading(UnitId unitId, const std::string& name);

        void disableShading(UnitId unitId, const std::string& name);

        /** The COB cache / dont-cache state of a piece; see UnitMesh::cached. */
        void enableCaching(UnitId unitId, const std::string& name);

        void disableCaching(UnitId unitId, const std::string& name);

        bool isPieceMoving(UnitId unitId, const std::string& name, SimAxis axis) const;

        bool isPieceTurning(UnitId unitId, const std::string& name, SimAxis axis) const;

        /**
         * A transport script's attach-unit: takes the unit aboard if it is not
         * yet carried, or moves it to another of the transport's pieces if it is.
         */
        void attachUnitToTransportPiece(UnitId transportId, UnitId unitId, const std::string& piece);

        /** A transport script's drop-unit: sets the unit down where it hangs right now. */
        void dropUnitFromTransport(UnitId transportId, UnitId unitId);

        void setBuildStance(UnitId unitId, bool value);

        void setYardOpen(UnitId unitId, bool value);

        void setBuggerOff(UnitId unitId, bool value);

    private:
        GameSimulation& simulation;
    };
}
