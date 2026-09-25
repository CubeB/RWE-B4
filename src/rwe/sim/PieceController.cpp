#include "PieceController.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    void PieceController::attachUnitToTransportPiece(UnitId transportId, UnitId unitId, const std::string& piece)
    {
        auto unitRef = simulation.tryGetUnitState(unitId);
        if (!unitRef)
        {
            return;
        }
        auto& unit = unitRef->get();
        if (unit.carriedBy == transportId)
        {
            unit.carriedPiece = piece;
            return;
        }
        if (unit.carriedBy || !unit.isAlive() || !unit.isOwnedBy(simulation.getUnitState(transportId).owner))
        {
            return;
        }
        simulation.loadUnitIntoTransport(transportId, unitId, piece);
    }

    void PieceController::dropUnitFromTransport(UnitId transportId, UnitId unitId)
    {
        auto unitRef = simulation.tryGetUnitState(unitId);
        if (!unitRef || unitRef->get().carriedBy != transportId)
        {
            return;
        }
        simulation.unloadUnitFromTransport(transportId, unitId, unitRef->get().position);
    }

    void PieceController::showObject(UnitId unitId, const std::string& name)
    {
        auto mesh = simulation.getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().visible = true;
        }
    }

    void PieceController::hideObject(UnitId unitId, const std::string& name)
    {
        auto mesh = simulation.getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().visible = false;
        }
    }

    void PieceController::enableShading(UnitId unitId, const std::string& name)
    {
        auto mesh = simulation.getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().shaded = true;
        }
    }

    void PieceController::disableShading(UnitId unitId, const std::string& name)
    {
        auto mesh = simulation.getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().shaded = false;
        }
    }

    void PieceController::enableCaching(UnitId unitId, const std::string& name)
    {
        auto mesh = simulation.getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().cached = true;
        }
    }

    void PieceController::disableCaching(UnitId unitId, const std::string& name)
    {
        auto mesh = simulation.getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().cached = false;
        }
    }

    bool PieceController::isPieceMoving(UnitId unitId, const std::string& name, SimAxis axis) const
    {
        return simulation.getUnitState(unitId).isMoveInProgress(name, axis);
    }

    bool PieceController::isPieceTurning(UnitId unitId, const std::string& name, SimAxis axis) const
    {
        return simulation.getUnitState(unitId).isTurnInProgress(name, axis);
    }

    void PieceController::setBuildStance(UnitId unitId, bool value)
    {
        simulation.getUnitState(unitId).inBuildStance = value;
    }

    void PieceController::setYardOpen(UnitId unitId, bool value)
    {
        simulation.trySetYardOpen(unitId, value);
    }

    void PieceController::setBuggerOff(UnitId unitId, bool value)
    {
        simulation.getUnitState(unitId).buggerOffActive = value;
        if (value)
        {
            simulation.emitBuggerOff(unitId);
        }
    }
}
