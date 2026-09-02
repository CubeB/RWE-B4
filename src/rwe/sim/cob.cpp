#include "cob.h"
#include <rwe/util/SimpleLogger.h>
#include <optional>
#include <rwe/cob/CobAxis.h>
#include <rwe/cob/CobExecutionContext.h>
#include <rwe/cob/CobPosition.h>
#include <rwe/cob/CobTime.h>
#include <rwe/cob/cob_util.h>
#include <rwe/sim/SimAxis.h>
#include <rwe/sim/SimScalar.h>

namespace rwe
{
    float toFloat(CobSpeed speed)
    {
        return static_cast<float>(speed.value) / 65536.0f;
    }

    SimScalar toSimScalar(CobSpeed speed)
    {
        return SimScalar(toFloat(speed));
    }

    CobSpeed toCobSpeed(SimScalar speed)
    {
        return CobSpeed(static_cast<uint32_t>(speed.value * 65536.0f));
    }

    SimScalar toSimScalar(CobAngularSpeed angularSpeed)
    {
        return SimScalar(angularSpeed.value);
    }

    SimAngle toWorldAngle(CobAngle angle)
    {
        return SimAngle(angle.value);
    }

    CobAngle toCobAngle(SimAngle angle)
    {
        return CobAngle(angle.value);
    }

    CobTime toCobTime(GameTime gameTime)
    {
        return CobTime((gameTime.value * 1000) / 30);
    }

    CobTime addDuration(CobTime time, CobSleepDuration duration)
    {
        return CobTime(time.value + duration.value);
    }

    SimAxis toSimAxis(CobAxis axis)
    {
        switch (axis)
        {
            case CobAxis::X:
                return SimAxis::X;
            case CobAxis::Y:
                return SimAxis::Y;
            case CobAxis::Z:
                return SimAxis::Z;
            default:
                throw std::logic_error("Invalid CobAxis value");
        }
    }

    const std::string& getObjectName(const CobEnvironment& env, unsigned int objectId)
    {
        return env._script->pieces.at(objectId);
    }

    CobPosition simScalarToCobPosition(SimScalar v)
    {
        return CobPosition(static_cast<int32_t>(v.value * 65536.0f));
    }

    SimScalar cobPositionToSimScalar(CobPosition v)
    {
        return SimScalar(static_cast<float>(v.value) / 65536.0f);
    }

    int packCoords(SimScalar x, SimScalar z)
    {
        return static_cast<int>(cobPackCoords(simScalarToCobPosition(x), simScalarToCobPosition(z)));
    }

    using InterruptedReason = std::variant<CobEnvironment::PieceCommandStatus, CobEnvironment::QueryStatus, CobEnvironment::SetQueryStatus>;

    std::optional<InterruptedReason> executeThreads(CobEnvironment& env, UnitId unitId, GameTime gameTime)
    {
        while (!env.readyQueue.empty())
        {
            auto thread = env.readyQueue.front();

            CobExecutionContext context(&env, thread);

            auto result = match(
                context.execute(),
                [&](const CobEnvironment::BlockedStatus& status) {
                    env.readyQueue.pop_front();
                    env.blockedQueue.emplace_back(status, thread);
                    return std::optional<InterruptedReason>();
                },
                [&](const CobEnvironment::SleepStatus& status) {
                    auto wakeTime = addDuration(toCobTime(gameTime), status.duration);
                    env.readyQueue.pop_front();
                    env.sleepingQueue.emplace_back(wakeTime, thread);
                    return std::optional<InterruptedReason>();
                },
                [&](const CobEnvironment::FinishedStatus&) {
                    env.readyQueue.pop_front();
                    env.finishedQueue.emplace_back(thread);
                    return std::optional<InterruptedReason>();
                },
                [&](const CobEnvironment::SignalStatus& status) {
                    env.sendSignal(status.signal);
                    return std::optional<InterruptedReason>();
                },
                [&](const CobEnvironment::PieceCommandStatus& status) {
                    return std::optional<InterruptedReason>(status);
                },
                [&](const CobEnvironment::QueryStatus& status) {
                    return std::optional<InterruptedReason>(status);
                },
                [&](const CobEnvironment::SetQueryStatus& status) {
                    return std::optional<InterruptedReason>(status);
                });

            if (result)
            {
                return result;
            }
        }

        return std::nullopt;
    }

    void handlePieceCommand(GameSimulation& simulation, const CobEnvironment& env, UnitId unitId, const CobEnvironment::PieceCommandStatus& result)
    {
        // attach-unit may name piece -1 (stowed inside the transport); other
        // commands always carry a real piece.
        static const std::string noPiece;
        const auto& objectName = result.piece < env._script->pieces.size() ? getObjectName(env, result.piece) : noPiece;
        match(
            result.command,
            [&](const CobEnvironment::PieceCommandStatus::Move& m) {
                // flip x-axis translations to match our right-handed coordinates
                auto position = m.axis == CobAxis::X ? -m.position : m.position;
                if (m.speed)
                {
                    simulation.moveObject(unitId, objectName, toSimAxis(m.axis), cobPositionToSimScalar(position), toSimScalar(*m.speed));
                }
                else
                {
                    simulation.moveObjectNow(unitId, objectName, toSimAxis(m.axis), cobPositionToSimScalar(position));
                }
            },
            [&](const CobEnvironment::PieceCommandStatus::Turn& t) {
                // flip z-axis rotations to match our right-handed coordinates
                auto angle = t.axis == CobAxis::Z ? -t.angle : t.angle;
                if (t.speed)
                {
                    simulation.turnObject(unitId, objectName, toSimAxis(t.axis), toWorldAngle(angle), toSimScalar(*t.speed));
                }
                else
                {
                    simulation.turnObjectNow(unitId, objectName, toSimAxis(t.axis), toWorldAngle(angle));
                }
            },
            [&](const CobEnvironment::PieceCommandStatus::Spin& s) {
                simulation.spinObject(unitId, objectName, toSimAxis(s.axis), toSimScalar(s.targetSpeed), toSimScalar(s.acceleration));
            },
            [&](const CobEnvironment::PieceCommandStatus::StopSpin& s) {
                simulation.stopSpinObject(unitId, objectName, toSimAxis(s.axis), toSimScalar(s.deceleration));
            },
            [&](const CobEnvironment::PieceCommandStatus::Show&) {
                simulation.showObject(unitId, objectName);
            },
            [&](const CobEnvironment::PieceCommandStatus::Hide&) {
                simulation.hideObject(unitId, objectName);
            },
            [&](const CobEnvironment::PieceCommandStatus::EnableShading&) {
                simulation.enableShading(unitId, objectName);
            },
            [&](const CobEnvironment::PieceCommandStatus::DisableShading&) {
                simulation.disableShading(unitId, objectName);
            },
            [&](const CobEnvironment::PieceCommandStatus::Explode& e) {
                const auto& unit = simulation.getUnitState(unitId);

                // Scripts routinely name pieces the model does not have. Those
                // still get their explosion sprite, at the unit's own position.
                auto pieceExists = unit.findPiece(objectName).has_value();
                auto position = pieceExists ? simulation.getUnitPiecePosition(unitId, objectName) : unit.position;

                simulation.events.push_back(PieceExplodedEvent{
                    unitId,
                    unit.unitType,
                    unit.owner,
                    pieceExists ? objectName : std::string(),
                    position,
                    unit.rotation,
                    e.flags});

                // BITMAPONLY shows an explosion but leaves the piece in place.
                const unsigned int bitmapOnly = 32u;
                if (pieceExists && (e.flags & bitmapOnly) == 0u)
                {
                    simulation.hideObject(unitId, objectName);
                }
            },
            [&](const CobEnvironment::PieceCommandStatus::AttachUnit& a) {
                // The transport's script has the unit on its crane or pad now.
                LOG_DEBUG << "COB attach-unit: transport " << unitId.value << " takes unit " << a.unit << " on piece " << objectName;
                simulation.attachUnitToTransportPiece(unitId, UnitId(a.unit), objectName);
            },
            [&](const CobEnvironment::PieceCommandStatus::DropUnit& d) {
                LOG_DEBUG << "COB drop-unit: transport " << unitId.value << " lets go of unit " << d.unit;
                simulation.dropUnitFromTransport(unitId, UnitId(d.unit));
            },
            [&](const CobEnvironment::PieceCommandStatus::EmitSfx& s) {
                switch (s.sfxType)
                {
                    case CobSfxType::WhiteSmoke:
                        simulation.events.push_back(EmitParticleFromPieceEvent{EmitParticleFromPieceEvent::SfxType::LightSmoke, unitId, objectName});
                        break;
                    case CobSfxType::BlackSmoke:
                        simulation.events.push_back(EmitParticleFromPieceEvent{EmitParticleFromPieceEvent::SfxType::BlackSmoke, unitId, objectName});
                        break;
                    case CobSfxType::Wake1:
                        simulation.events.push_back(EmitParticleFromPieceEvent{EmitParticleFromPieceEvent::SfxType::Wake1, unitId, objectName});
                        break;
                    case CobSfxType::Vtol:
                        simulation.events.push_back(EmitParticleFromPieceEvent{EmitParticleFromPieceEvent::SfxType::Vtol, unitId, objectName});
                        break;
                    case CobSfxType::Wake2:
                        simulation.events.push_back(EmitParticleFromPieceEvent{EmitParticleFromPieceEvent::SfxType::Wake2, unitId, objectName});
                        break;
                    case CobSfxType::ReverseWake1:
                        simulation.events.push_back(EmitParticleFromPieceEvent{EmitParticleFromPieceEvent::SfxType::ReverseWake1, unitId, objectName});
                        break;
                    case CobSfxType::ReverseWake2:
                        simulation.events.push_back(EmitParticleFromPieceEvent{EmitParticleFromPieceEvent::SfxType::ReverseWake2, unitId, objectName});
                        break;
                    case CobSfxType::Thrust:
                        simulation.events.push_back(EmitParticleFromPieceEvent{EmitParticleFromPieceEvent::SfxType::Thrust, unitId, objectName});
                        break;
                }
            });
    }

    int handleQuery(GameSimulation& sim, const CobEnvironment& env, UnitId unitId, const CobEnvironment::QueryStatus& result)
    {
        return match(
            result.query,
            [&](const CobEnvironment::QueryStatus::Random& q) {
                // FIXME: probably not consistent across platforms
                std::uniform_int_distribution<int> dist(q.low, q.high);
                auto value = dist(sim.rng);
                return value;
            },
            [&](const CobEnvironment::QueryStatus::Activation&) {
                const auto& unit = sim.getUnitState(unitId);
                return static_cast<int>(unit.activated);
            },
            [&](const CobEnvironment::QueryStatus::StandingFireOrders&) {
                const auto& unit = sim.getUnitState(unitId);
                return static_cast<int>(unit.fireOrders);
            },
            [&](const CobEnvironment::QueryStatus::StandingMoveOrders&) {
                const auto& unit = sim.getUnitState(unitId);
                return static_cast<int>(unit.moveOrders);
            },
            [&](const CobEnvironment::QueryStatus::Health&) {
                const auto& unit = sim.getUnitState(unitId);
                const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
                return static_cast<int>((unit.hitPoints * 100) / unitDefinition.maxHitPoints);
            },
            [&](const CobEnvironment::QueryStatus::InBuildStance&) {
                const auto& unit = sim.getUnitState(unitId);
                return static_cast<int>(unit.inBuildStance);
            },
            [&](const CobEnvironment::QueryStatus::Busy&) {
                const auto& unit = sim.getUnitState(unitId);
                return static_cast<int>(unit.cobBusy);
            },
            [&](const CobEnvironment::QueryStatus::PieceXZ& q) {
                auto pieceId = q.piece;
                const auto& pieceName = getObjectName(env, pieceId);
                auto pos = sim.getUnitPiecePosition(unitId, pieceName);
                return packCoords(pos.x, pos.z);
            },
            [&](const CobEnvironment::QueryStatus::PieceY& q) {
                auto pieceId = q.piece;
                const auto& pieceName = getObjectName(env, pieceId);
                auto pos = sim.getUnitPiecePosition(unitId, pieceName);
                return simScalarToCobPosition(pos.y).value;
            },
            [&](const CobEnvironment::QueryStatus::UnitXZ& q) {
                auto targetUnitId = q.targetUnitId;
                auto targetUnitOption = sim.tryGetUnitState(targetUnitId);
                if (!targetUnitOption)
                {
                    // FIXME: not sure if correct return value when unit does not exist
                    return 0;
                }
                const auto& pos = targetUnitOption->get().position;
                return packCoords(pos.x, pos.z);
            },
            [&](const CobEnvironment::QueryStatus::UnitY& q) {
                auto targetUnitId = q.targetUnitId;
                auto targetUnitOption = sim.tryGetUnitState(targetUnitId);
                if (!targetUnitOption)
                {
                    // FIXME: not sure if correct return value when unit does not exist
                    return 0;
                }
                const auto& pos = targetUnitOption->get().position;
                return simScalarToCobPosition(pos.y).value;
            },
            [&](const CobEnvironment::QueryStatus::UnitHeight& q) {
                auto targetUnitId = q.targetUnitId;
                auto targetUnitOption = sim.tryGetUnitState(targetUnitId);
                if (!targetUnitOption)
                {
                    // FIXME: not sure if correct return value when unit does not exist
                    return 0;
                }
                const auto& unitDefinition = sim.unitDefinitions.at(targetUnitOption->get().unitType);
                const auto& modelDefinition = sim.unitModelDefinitions.at(unitDefinition.objectName);
                return simScalarToCobPosition(modelDefinition.height).value;
            },
            [&](const CobEnvironment::QueryStatus::XZAtan& q) {
                auto pair = cobUnpackCoords(q.coords);
                const auto& unit = sim.getUnitState(unitId);

                // Surprisingly, the result of XZAtan is offset by the unit's current rotation.
                // The other interesting thing is that in TA, at least for mobile units,
                // it appears that a unit with rotation 0 faces up, towards negative Z.
                // However, in RWE, a unit with rotation 0 faces down, towards positive z.
                // We therefore subtract a half turn to convert to what scripts expect.
                // TODO: test whether this is also the case for buildings
                auto correctedUnitRotation = unit.rotation - HalfTurn;
                auto result = atan2(cobPositionToSimScalar(pair.first), cobPositionToSimScalar(pair.second)) - correctedUnitRotation;
                return static_cast<int>(toCobAngle(result).value);
            },
            [&](const CobEnvironment::QueryStatus::GroundHeight& q) {
                auto pair = cobUnpackCoords(q.coords);
                auto result = sim.terrain.getHeightAt(cobPositionToSimScalar(pair.first), cobPositionToSimScalar(pair.second));
                return simScalarToCobPosition(result).value;
            },
            [&](const CobEnvironment::QueryStatus::BuildPercentLeft&) {
                const auto& unit = sim.getUnitState(unitId);
                const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
                return static_cast<int>(unit.getBuildPercentLeft(unitDefinition));
            },
            [&](const CobEnvironment::QueryStatus::YardOpen&) {
                const auto& unit = sim.getUnitState(unitId);
                return static_cast<int>(unit.yardOpen);
            },
            [&](const CobEnvironment::QueryStatus::BuggerOff&) {
                const auto& unit = sim.getUnitState(unitId);
                return static_cast<int>(unit.buggerOffActive);
            },
            [&](const CobEnvironment::QueryStatus::Armored&) {
                const auto& unit = sim.getUnitState(unitId);
                return static_cast<int>(unit.armored);
            },
            [&](const CobEnvironment::QueryStatus::VeteranLevel&) {
                // The original never answers this one: its GET dispatcher
                // (TotalA.exe 0x480770) only knows values 1 to 20 and returns
                // zero for anything else, VETERAN_LEVEL included. We answer
                // with the tier the engine itself uses for damage, five kills
                // apiece and capped at five (0x489BF3), on the grounds that a
                // script asking the question is better served by the number
                // the simulation actually acts on than by a constant zero.
                const auto& unit = sim.getUnitState(unitId);
                constexpr unsigned int VeteranKillsPerTier = 5;
                constexpr unsigned int VeteranMaxTier = 5;
                unsigned int tier = unit.kills / VeteranKillsPerTier;
                if (tier > VeteranMaxTier)
                {
                    tier = VeteranMaxTier;
                }
                return static_cast<int>(tier);
            },
            [&](const CobEnvironment::QueryStatus::MinId&) {
                std::optional<unsigned int> minId;
                for (const auto& [id, _] : sim.units)
                {
                    if (!minId || id.value < *minId)
                    {
                        minId = id.value;
                    }
                }
                return static_cast<int>(minId.value_or(0));
            },
            [&](const CobEnvironment::QueryStatus::MaxId&) {
                std::optional<unsigned int> maxId;
                for (const auto& [id, _] : sim.units)
                {
                    if (!maxId || id.value > *maxId)
                    {
                        maxId = id.value;
                    }
                }
                return static_cast<int>(maxId.value_or(0));
            },
            [&](const CobEnvironment::QueryStatus::MyId&) {
                return static_cast<int>(unitId.value);
            },
            [&](const CobEnvironment::QueryStatus::UnitTeam& q) {
                auto targetUnitId = q.targetUnitId;
                auto targetUnitOption = sim.tryGetUnitState(targetUnitId);
                if (!targetUnitOption)
                {
                    // FIXME: unsure if correct return value when unit does not exist
                    return 0;
                }
                // TODO: return player's team instead of player ID
                return static_cast<int>(targetUnitOption->get().owner.value);
            },
            [&](const CobEnvironment::QueryStatus::UnitBuildPercentLeft& q) {
                auto targetUnitOption = sim.tryGetUnitState(q.targetUnitId);
                if (!targetUnitOption)
                {
                    // FIXME: unsure if correct return value when unit does not exist
                    return 0;
                }
                const auto& unitDefinition = sim.unitDefinitions.at(targetUnitOption->get().unitType);
                return static_cast<int>(targetUnitOption->get().getBuildPercentLeft(unitDefinition));
            },
            [&](const CobEnvironment::QueryStatus::UnitAllied& q) {
                const auto& unit = sim.getUnitState(unitId);
                auto targetUnitOption = sim.tryGetUnitState(q.targetUnitId);
                if (!targetUnitOption)
                {
                    // FIXME: unsure if correct return value when unit does not exist
                    return 0;
                }
                // TODO: real allied check including teams/alliances
                return static_cast<int>(targetUnitOption->get().isOwnedBy(unit.owner));
            });
    }

    void handleSetQuery(GameSimulation& sim, const CobEnvironment& env, UnitId unitId, const CobEnvironment::SetQueryStatus& result)
    {
        match(
            result.query,
            [&](const CobEnvironment::SetQueryStatus::Activation& q) {
                if (q.value)
                {
                    sim.activateUnit(unitId);
                }
                else
                {
                    sim.deactivateUnit(unitId);
                }
            },
            [&](const CobEnvironment::SetQueryStatus::StandingMoveOrders& q) {
                auto& unit = sim.getUnitState(unitId);
                switch (q.value)
                {
                    case 0:
                        unit.moveOrders = UnitMovementOrders::HoldPosition;
                        break;
                    case 1:
                        unit.moveOrders = UnitMovementOrders::Maneuver;
                        break;
                    case 2:
                        unit.moveOrders = UnitMovementOrders::Roam;
                        break;
                    default:
                        // ignore out-of-range
                        break;
                }
            },
            [&](const CobEnvironment::SetQueryStatus::StandingFireOrders& q) {
                auto& unit = sim.getUnitState(unitId);
                switch (q.value)
                {
                    case 0:
                        unit.setFireOrders(UnitFireOrders::HoldFire);
                        break;
                    case 1:
                        unit.setFireOrders(UnitFireOrders::ReturnFire);
                        break;
                    case 2:
                        unit.setFireOrders(UnitFireOrders::FireAtWill);
                        break;
                    default:
                        // ignore out-of-range
                        break;
                }
            },
            [&](const CobEnvironment::SetQueryStatus::InBuildStance& q) {
                sim.setBuildStance(unitId, q.value);
            },
            [&](const CobEnvironment::SetQueryStatus::Busy& q) {
                auto& unit = sim.getUnitState(unitId);
                unit.cobBusy = q.value;
            },
            [&](const CobEnvironment::SetQueryStatus::YardOpen& q) {
                sim.setYardOpen(unitId, q.value);
            },
            [&](const CobEnvironment::SetQueryStatus::BuggerOff& q) {
                sim.setBuggerOff(unitId, q.value);
            },
            [&](const CobEnvironment::SetQueryStatus::Armored& q) {
                auto& unit = sim.getUnitState(unitId);
                unit.armored = q.value;
            });
    }

    void runUnitCobScripts(GameSimulation& simulation, UnitId unitId)
    {
        auto& unit = simulation.getUnitState(unitId);
        auto& env = *unit.cobEnvironment;

        assert(env.isNotCorrupt());

        // clean up any finished threads that were not reaped last frame
        for (const auto& thread : env.finishedQueue)
        {
            env.deleteThread(thread);
        }
        env.finishedQueue.clear();

        assert(env.isNotCorrupt());

        // check if any blocked threads can be unblocked
        // and move them back into the ready queue
        for (auto it = env.blockedQueue.begin(); it != env.blockedQueue.end();)
        {
            const auto& pair = *it;
            const auto& status = pair.first;

            auto isUnblocked = match(
                status.condition,
                [&env, &simulation, unitId](const CobEnvironment::BlockedStatus::Move& condition) {
                    const auto& pieceName = env._script->pieces.at(condition.object);
                    return !simulation.isPieceMoving(unitId, pieceName, toSimAxis(condition.axis));
                },
                [&env, &simulation, unitId](const CobEnvironment::BlockedStatus::Turn& condition) {
                    const auto& pieceName = env._script->pieces.at(condition.object);
                    return !simulation.isPieceTurning(unitId, pieceName, toSimAxis(condition.axis));
                });

            if (isUnblocked)
            {
                env.readyQueue.push_back(pair.second);
                it = env.blockedQueue.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // check if any sleeping threads can be moved into the ready queue
        auto cobTime = toCobTime(simulation.gameTime);
        for (auto it = env.sleepingQueue.begin(); it != env.sleepingQueue.end();)
        {
            const auto& pair = *it;
            const auto& wakeTime = pair.first;
            if (cobTime >= wakeTime)
            {
                env.readyQueue.push_back(pair.second);
                it = env.sleepingQueue.erase(it);
            }
            else
            {
                ++it;
            }
        }

        assert(env.isNotCorrupt());

        // execute ready threads
        while (auto result = executeThreads(env, unitId, simulation.gameTime))
        {
            match(
                *result,
                [&](const CobEnvironment::PieceCommandStatus& s) {
                    handlePieceCommand(simulation, env, unitId, s);
                },
                [&](const CobEnvironment::QueryStatus& s) {
                    auto result = handleQuery(simulation, env, unitId, s);
                    env.pushResult(result);
                },
                [&](const CobEnvironment::SetQueryStatus& s) {
                    handleSetQuery(simulation, env, unitId, s);
                });
        }

        assert(env.isNotCorrupt());
    }
}
