#include "DefaultAction.h"
#include <rwe/sim/UnitBehaviorService_util.h>

namespace rwe
{
    namespace
    {
        /**
         * The preamble the original runs before either ladder
         * (0x43F0EC-0x43F12D): one byte out of the ordering player's ally
         * table decides ALLIED and ENEMY, and the two are complements.
         *
         * RWE has no separate ally relation at this layer -- GameScene's
         * isEnemy is "not mine", with a TODO on it -- so allied here means
         * the orderer's own player, which is also what makes the load rule
         * below an own-units rule rather than an allied-units one.
         */
        bool isAlliedTarget(const UnitState& orderer, const UnitState& target)
        {
            return target.isOwnedBy(orderer.owner);
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
         * own units only, stated here once rather than repeated at each of
         * the click handlers -- see S:88.
         */
        bool canLoad(const GameSimulation& sim, const UnitState& orderer, UnitId ordererId, const UnitState& target, UnitId targetId)
        {
            if (!isAlliedTarget(orderer, target))
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
         * The arms shared by Interface Type 1's default action and by the
         * MOVE button, which are the same ladder: 0x43FA00 is command 2's
         * chain (0x43F845) with the capture arm brought to the front, and the
         * cursor chooser mirrors it arm for arm at 0x43E8BB.
         */
        std::optional<DefaultAction> unitLadderRightClick(
            const GameSimulation& sim,
            UnitId ordererId,
            const UnitState& orderer,
            const UnitDefinition& ordererDefinition,
            UnitId targetId,
            const UnitState& target)
        {
            auto allied = isAlliedTarget(orderer, target);
            const auto& targetDefinition = sim.unitDefinitions.at(target.unitType);

            if (!allied && ordererDefinition.canCapture)
            {
                return issue(CaptureOrder(targetId), CursorType::Capture);
            }

            if (!allied && ordererDefinition.canReclamate)
            {
                return issue(ReclaimOrder(targetId), CursorType::Reclaim);
            }

            // Not in the decode. S:103's reading of 0x43FA00 lists no attack
            // arm, but right-clicking an enemy has always attacked it in RWE
            // and taking that away on the strength of an elided list would be
            // the worse mistake, so it is kept and recorded as unsettled.
            if (!allied && ordererDefinition.canAttack)
            {
                return issue(AttackOrder(targetId), CursorType::Attack);
            }

            if (allied && ordererDefinition.builder && isUnderConstruction(sim, target))
            {
                return issue(CompleteBuildOrder(targetId), CursorType::Repair);
            }

            if (allied && ordererDefinition.builder && isDamaged(sim, target))
            {
                return issue(RepairOrder(targetId), CursorType::Repair);
            }

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
            auto allied = isAlliedTarget(orderer, target);

            if (!allied && ordererDefinition.canAttack)
            {
                return issue(AttackOrder(targetId), CursorType::Attack);
            }

            if (!allied && ordererDefinition.canReclamate)
            {
                return issue(ReclaimOrder(targetId), CursorType::Reclaim);
            }

            if (allied && ordererDefinition.builder && isUnderConstruction(sim, target))
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
            if (allied && ordererDefinition.builder && isDamaged(sim, target))
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

        auto result = [&]() -> DefaultAction {
            if (targetRef)
            {
                const auto& target = targetRef->get();

                // Interface Type 1's cursor is feedback and nothing more:
                // 0x43EB02 answers 15 for one of your own, 17 for an enemy
                // and 18 for an ally, and the right button issues without
                // consulting it at all (0x4991D5). So in that scheme the
                // cursor and the order genuinely come apart, and they are
                // meant to.
                auto feedbackCursor = [&]() {
                    if (target.isSelectableBy(sim.unitDefinitions.at(target.unitType), orderer.owner))
                    {
                        return CursorType::Select;
                    }
                    return isAlliedTarget(orderer, target) ? CursorType::Green : CursorType::Red;
                };

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
                        auto a = unitLadderRightClick(sim, ordererId, orderer, ordererDefinition, *hoveredUnit, target);
                        if (a)
                        {
                            return DefaultAction{a->action, feedbackCursor()};
                        }
                        return DefaultAction{
                            ordererDefinition.canMove ? DefaultActionKind(DefaultActionMove()) : DefaultActionKind(DefaultActionNothing()),
                            feedbackCursor()};
                    }
                    case DefaultActionScheme::MoveButton:
                    {
                        if (auto a = unitLadderRightClick(sim, ordererId, orderer, ordererDefinition, *hoveredUnit, target); a)
                        {
                            return *a;
                        }
                        break;
                    }
                }
            }
            else if (hoveredFeature && scheme != DefaultActionScheme::MoveButton)
            {
                // A MOVE aimed at anything but a unit is a plain move
                // (0x43F873), so the feature arms belong to the default
                // action alone. RWE resolves a hovered unit and a hovered
                // feature separately and lets the unit win, where the
                // original probes the map cell only after its unit arms have
                // failed; the difference shows only for a unit standing on a
                // corpse.
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
                return DefaultAction{DefaultActionMove(), CursorType::Move};
            }

            return DefaultAction{DefaultActionNothing(), CursorType::Normal};
        }();

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
