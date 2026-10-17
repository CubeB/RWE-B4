#include "DefaultAction.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitBehaviorService_util.h>

namespace rwe
{
    namespace
    {
        /**
         * The ladder's own test, 0x43F0EC-0x43F12D: one byte out of the
         * ordering player's ally table decides ALLIED and ENEMY, and the two
         * are complements. RWE's ally table is the lobby team, which is what
         * shared vision runs on too, so a teammate's unit is allied here.
         */
        bool isAlliedTarget(const GameSimulation& sim, const UnitState& orderer, const UnitState& target)
        {
            return sim.arePlayersAllied(orderer.owner, target.owner);
        }

        bool isUnderConstruction(const GameSimulation& sim, const UnitState& target)
        {
            return target.isBeingBuilt(sim.unitDefinitions.at(target.unitType));
        }

        bool isDamaged(const GameSimulation& sim, const UnitState& target)
        {
            const auto& definition = sim.unitDefinitions.at(target.unitType);
            return target.isAlive() && !target.isBeingBuilt(definition) && target.hitPoints < definition.maxHitPoints;
        }

        bool featureIsReclaimable(const GameSimulation& sim, FeatureId featureId)
        {
            auto feature = sim.tryGetFeature(featureId);
            if (!feature)
            {
                return false;
            }
            return sim.getFeatureDefinition(feature->get().featureName).reclaimable;
        }

        /**
         * A corpse the raiser could turn back into a unit. The original tests
         * the feature's reclaimable bit and nothing else (0x404E0F) and then
         * resolves the type from the corpse's own name; a corpse whose name
         * does not resolve is not resurrectable, which in the shipped data is
         * all of them.
         */
        bool featureIsResurrectable(const GameSimulation& sim, FeatureId featureId)
        {
            auto feature = sim.tryGetFeature(featureId);
            if (!feature)
            {
                return false;
            }
            const auto& definition = sim.getFeatureDefinition(feature->get().featureName);
            if (!definition.reclaimable)
            {
                return false;
            }
            return sim.resurrectedUnitType(definition.name).has_value();
        }

        /**
         * The one arm where RWE deliberately differs from the original.
         *
         * Neither CanLoadUnit (0x489A90) nor any of its five call sites
         * applies an ownership or an alliance test, so the original will
         * happily aim a pickup at an enemy unit (S:103). RWE's house rule is
         * own units only -- not allied ones, a teammate's units are theirs
         * to move -- stated here once rather than repeated at each of the
         * click handlers; see S:88.
         */
        bool canLoad(const GameSimulation& sim, const UnitState& orderer, UnitId ordererId, const UnitState& target, UnitId targetId)
        {
            if (!target.isOwnedBy(orderer.owner))
            {
                return false;
            }
            return sim.canLoadUnitIntoTransport(ordererId, targetId);
        }

        DefaultAction issue(const UnitOrder& o, CursorType cursor)
        {
            return DefaultAction{DefaultActionOrder(o), cursor};
        }

        /**
         * The attack arm is the order builder's own recursion with command 3
         * and returns whatever that yields, an order or nothing, and the
         * ladder stops there (0x43FA00-0x43FA0D). Nothing is a unit that
         * cannot carry the attack out -- a ground gun against a submerged
         * submarine -- and a click that gives it leaves the queue alone.
         * BT-63.
         */
        DefaultAction attackArm(
            const GameSimulation& sim,
            const UnitState& orderer,
            const UnitDefinition& ordererDefinition,
            UnitId targetId,
            const UnitState& target)
        {
            if (!attackOrderIsAccepted(sim, ordererDefinition, orderer, target))
            {
                return DefaultAction{DefaultActionNothing(), CursorType::Normal};
            }
            return issue(AttackOrder(targetId), CursorType::Attack);
        }

        /**
         * The tail the two unit chains share once their repair arms have
         * declined: landing, pickup and follow. The repair arms are the
         * callers', because the two chains test differently there.
         */
        std::optional<DefaultAction> alliedAndLoadArms(
            const GameSimulation& sim,
            UnitId ordererId,
            const UnitState& orderer,
            const UnitDefinition& ordererDefinition,
            UnitId targetId,
            const UnitState& target,
            bool allied)
        {
            const auto& targetDefinition = sim.unitDefinitions.at(target.unitType);

            if (ordererDefinition.canFly && allied && targetId != ordererId && unitIsAnUsableAirBase(target, targetDefinition))
            {
                return issue(LandOnAirBaseOrder(targetId), CursorType::Unload);
            }

            if (canLoad(sim, orderer, ordererId, target, targetId))
            {
                // The only place in the whole cursor path where an air
                // transport and a crane differ (0x43E7F1).
                return issue(LoadOrder(targetId), ordererDefinition.canFly ? CursorType::Pickup : CursorType::Load);
            }

            if (allied && ordererDefinition.canGuard && targetId != ordererId)
            {
                return issue(GuardOrder(targetId), CursorType::Guard);
            }

            return std::nullopt;
        }

        /**
         * Interface Type 1's default action, the chain at 0x43FA00 (BT-61).
         * It opens with the attack arm, which returns whatever the attack
         * recursion yields, an order or nothing, so an enemy a unit cannot
         * attack ends the click there. It has no capture arm: what an
         * earlier reading called capture was the reclaim arm (`canreclamate`,
         * bit 10, not `cancapture`, bit 12), and capture is reachable only
         * from the armed CAPTURE button and from command 2's ladder. Its
         * repair arm has no hit-point test beyond the predicate's own.
         */
        std::optional<DefaultAction> unitLadderRightClick(
            const GameSimulation& sim,
            UnitId ordererId,
            const UnitState& orderer,
            const UnitDefinition& ordererDefinition,
            UnitId targetId,
            const UnitState& target)
        {
            auto allied = isAlliedTarget(sim, orderer, target);

            if (!allied && ordererDefinition.canAttack)
            {
                return attackArm(sim, orderer, ordererDefinition, targetId, target);
            }

            if (!allied && ordererDefinition.canReclamate)
            {
                return issue(ReclaimOrder(targetId), CursorType::Reclaim);
            }

            if (allied && canRepairUnit(sim, ordererDefinition, target))
            {
                if (isUnderConstruction(sim, target))
                {
                    return issue(CompleteBuildOrder(targetId), CursorType::Repair);
                }
                return issue(RepairOrder(targetId), CursorType::Repair);
            }

            return alliedAndLoadArms(sim, ordererId, orderer, ordererDefinition, targetId, target, allied);
        }

        /**
         * The MOVE button's ladder, command 2's chain (0x43F845), which is
         * the one place capture is an arm of a ladder: it is tried before
         * reclaim and before the attack arm RWE keeps.
         */
        std::optional<DefaultAction> unitLadderMoveButton(
            const GameSimulation& sim,
            UnitId ordererId,
            const UnitState& orderer,
            const UnitDefinition& ordererDefinition,
            UnitId targetId,
            const UnitState& target)
        {
            auto allied = isAlliedTarget(sim, orderer, target);

            if (!allied && ordererDefinition.canCapture)
            {
                return issue(CaptureOrder(targetId), CursorType::Capture);
            }

            if (!allied && ordererDefinition.canReclamate)
            {
                return issue(ReclaimOrder(targetId), CursorType::Reclaim);
            }

            // Not in the decode of this chain. Right-clicking an enemy has
            // always attacked it in RWE, and the MOVE button's ladder is
            // kept as it was while BT-61 settles the right-click chain only.
            if (!allied && ordererDefinition.canAttack)
            {
                return attackArm(sim, orderer, ordererDefinition, targetId, target);
            }

            if (allied && canRepairUnit(sim, ordererDefinition, target) && isUnderConstruction(sim, target))
            {
                return issue(CompleteBuildOrder(targetId), CursorType::Repair);
            }

            if (allied && canRepairUnit(sim, ordererDefinition, target) && isDamaged(sim, target))
            {
                return issue(RepairOrder(targetId), CursorType::Repair);
            }

            return alliedAndLoadArms(sim, ordererId, orderer, ordererDefinition, targetId, target, allied);
        }

        /**
         * Interface Type 0's default action, 0x43FE35 for the order and
         * 0x43E512 for the cursor. Shorter than the other, and the difference
         * that matters is the fourth arm: your own units answer with
         * cursorselect before a guard or a pickup could ever be reached,
         * which is why the shipped scheme has a LOAD button at all.
         */
        std::optional<DefaultAction> unitLadderLeftClick(
            const GameSimulation& sim,
            const UnitState& orderer,
            const UnitDefinition& ordererDefinition,
            UnitId targetId,
            const UnitState& target)
        {
            auto allied = isAlliedTarget(sim, orderer, target);

            if (!allied && ordererDefinition.canAttack)
            {
                return attackArm(sim, orderer, ordererDefinition, targetId, target);
            }

            if (!allied && ordererDefinition.canReclamate)
            {
                return issue(ReclaimOrder(targetId), CursorType::Reclaim);
            }

            if (allied && canRepairUnit(sim, ordererDefinition, target) && isUnderConstruction(sim, target))
            {
                return issue(CompleteBuildOrder(targetId), CursorType::Repair);
            }

            if (target.isSelectableBy(sim.unitDefinitions.at(target.unitType), orderer.owner))
            {
                return DefaultAction{DefaultActionSelect(), CursorType::Select};
            }

            // The order builder has a damaged-ally arm here (0x43F918) that
            // the cursor chooser does not, and the cursor is what gates the
            // click, so it can only be reached for a unit that is allied
            // without being your own. RWE has no such unit yet; the arm is
            // written the way the order builder has it so that it does the
            // right thing when it does.
            if (allied && canRepairUnit(sim, ordererDefinition, target) && isDamaged(sim, target))
            {
                return issue(RepairOrder(targetId), CursorType::Repair);
            }

            return std::nullopt;
        }
    }

    int originalCursorId(CursorType cursor)
    {
        switch (cursor)
        {
            case CursorType::Attack:
                return 1;
            case CursorType::Capture:
                return 4;
            case CursorType::Guard:
                return 5;
            case CursorType::Repair:
                return 6;
            case CursorType::Patrol:
                return 7;
            case CursorType::Pickup:
                return 8;
            case CursorType::Reclaim:
                return 11;
            case CursorType::Load:
                return 12;
            case CursorType::Unload:
                return 13;
            case CursorType::Move:
                return 14;
            case CursorType::Select:
                return 15;
            case CursorType::Red:
                return 17;
            case CursorType::Green:
                return 18;
            case CursorType::Normal:
                return 19;
            case CursorType::PathIcon:
                return 21;
            default:
                return 19;
        }
    }

    CursorType preferredCursor(CursorType a, CursorType b)
    {
        return originalCursorId(a) <= originalCursorId(b) ? a : b;
    }

    DefaultAction computeDefaultAction(
        const GameSimulation& sim,
        DefaultActionScheme scheme,
        UnitId ordererId,
        std::optional<UnitId> hoveredUnit,
        std::optional<FeatureId> hoveredFeature)
    {
        auto ordererRef = sim.tryGetUnitState(ordererId);
        if (!ordererRef)
        {
            return DefaultAction{DefaultActionNothing(), CursorType::Normal};
        }
        const auto& orderer = ordererRef->get();
        const auto& ordererDefinition = sim.unitDefinitions.at(orderer.unitType);

        std::optional<std::reference_wrapper<const UnitState>> targetRef;
        if (hoveredUnit)
        {
            targetRef = sim.tryGetUnitState(*hoveredUnit);
        }

        // Interface Type 1's cursor is feedback and nothing more: 0x43EB02
        // answers 15 for one of your own, 17 for an enemy and 18 for an ally,
        // and the right button issues without consulting it at all
        // (0x4991D5). So in that scheme the cursor and the order genuinely
        // come apart, and they are meant to.
        auto feedbackCursor = [&](const UnitState& target) {
            if (target.isSelectableBy(sim.unitDefinitions.at(target.unitType), orderer.owner))
            {
                return CursorType::Select;
            }
            return isAlliedTarget(sim, orderer, target) ? CursorType::Green : CursorType::Red;
        };

        auto result = [&]() -> DefaultAction {
            if (targetRef)
            {
                const auto& target = targetRef->get();

                switch (scheme)
                {
                    case DefaultActionScheme::LeftClickDefault:
                    {
                        if (auto a = unitLadderLeftClick(sim, orderer, ordererDefinition, *hoveredUnit, target); a)
                        {
                            return *a;
                        }
                        break;
                    }
                    case DefaultActionScheme::RightClickDefault:
                    {
                        if (auto a = unitLadderRightClick(sim, ordererId, orderer, ordererDefinition, *hoveredUnit, target); a)
                        {
                            return DefaultAction{a->action, feedbackCursor(target)};
                        }
                        break;
                    }
                    case DefaultActionScheme::MoveButton:
                    {
                        if (auto a = unitLadderMoveButton(sim, ordererId, orderer, ordererDefinition, *hoveredUnit, target); a)
                        {
                            return *a;
                        }
                        break;
                    }
                }
            }

            // Reached with no hovered unit, or with one whose ladder had
            // nothing to offer: the original probes the map cell only after
            // its unit arms have failed (0x43F873), which is why this is not
            // an `else`. A hovered unit that gives no action -- someone
            // else's, standing over a wreck -- used to hide the feature
            // under it, so the wreck could not be reclaimed.
            if (hoveredFeature && scheme != DefaultActionScheme::MoveButton)
            {
                // A MOVE aimed at anything but a unit is a plain move
                // (0x43F873), so the feature arms belong to the default
                // action alone.
                if (ordererDefinition.canResurrect && featureIsResurrectable(sim, *hoveredFeature))
                {
                    // CURSORS.GAF ships cursorrevive only in rev31.gp3 and
                    // RWE has never loaded it, so the reclaim cursor stands
                    // in, as it already does for a resurrect order in flight.
                    return issue(ResurrectOrder(*hoveredFeature), CursorType::Reclaim);
                }

                if (ordererDefinition.canReclamate && featureIsReclaimable(sim, *hoveredFeature))
                {
                    return issue(ReclaimOrder(*hoveredFeature), CursorType::Reclaim);
                }
            }

            if (ordererDefinition.canMove)
            {
                // Both default-action ladders end at 0x44019D, which moves
                // only a unit with a mover, so a factory's rally point comes
                // from the MOVE button alone ("QMOVE", 0x43F854). The cursor
                // chooser does not look for the mover (0x43F07C), so the
                // original shows cursormove over the ground for a factory and
                // then does nothing with the click.
                auto moves = scheme == DefaultActionScheme::MoveButton || ordererDefinition.isMobile;
                return DefaultAction{moves ? DefaultActionKind(DefaultActionMove()) : DefaultActionKind(DefaultActionNothing()), CursorType::Move};
            }

            return DefaultAction{DefaultActionNothing(), CursorType::Normal};
        }();

        // Over a unit, Interface Type 1 shows the feedback cursor whatever the
        // chain went on to do after its unit arms (a feature under the unit,
        // a plain move).
        if (scheme == DefaultActionScheme::RightClickDefault && targetRef)
        {
            result.cursor = feedbackCursor(targetRef->get());
        }

        // The issue loop skips a selected unit that is also the thing clicked
        // (0x48D07B), so a unit never orders itself. It still contributes a
        // cursor: the chooser's own loop has no such skip.
        if (hoveredUnit && *hoveredUnit == ordererId)
        {
            result.action = DefaultActionNothing();
        }

        return result;
    }
}
