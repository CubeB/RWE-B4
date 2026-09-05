#pragma once

#include <rwe/grid/DiscreteRect.h>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <optional>
#include <variant>

namespace rwe
{
    struct MoveOrder
    {
        SimVector destination;
        explicit MoveOrder(const SimVector& destination) : destination(destination) {}
    };

    using AttackTarget = std::variant<UnitId, SimVector>;

    /**
     * How far a unit will follow a target it picked for itself, and from
     * where. A unit that spots something while patrolling or standing by
     * chases `maneuverleashlength` from the spot it was standing on when it
     * saw it, and then gives up (0x43B330 anchors it, 0x4034D2 tests it).
     * An attack the player ordered carries none: 0x43ADC0 passes zero, which
     * is what lets an ordered unit chase across the map.
     */
    struct AttackLeash
    {
        SimVector anchor;
        SimScalar distance;
        AttackLeash(const SimVector& anchor, SimScalar distance) : anchor(anchor), distance(distance) {}
    };

    struct AttackOrder
    {
        AttackTarget target;
        std::optional<AttackLeash> leash;
        explicit AttackOrder(UnitId target) : target(target) {}
        explicit AttackOrder(const SimVector& target) : target(target) {}
        AttackOrder(UnitId target, const AttackLeash& leash) : target(target), leash(leash) {}
    };

    struct BuildOrder
    {
        std::string unitType;
        SimVector position;
        BuildOrder(const std::string& unitType, const SimVector& position) : unitType(unitType), position(position) {}
    };

    struct BuggerOffOrder
    {
        DiscreteRect rect;
        explicit BuggerOffOrder(const DiscreteRect& r) : rect(r) {}
    };

    /** Finish building an already in-progress unit */
    struct CompleteBuildOrder
    {
        UnitId target;
        explicit CompleteBuildOrder(const UnitId& target) : target(target) {}
    };

    struct GuardOrder
    {
        UnitId target;
        explicit GuardOrder(const UnitId& target) : target(target) {}
    };

    struct ReclaimOrder
    {
        std::variant<UnitId, FeatureId> target;
        explicit ReclaimOrder(const UnitId& target) : target(target) {}
        explicit ReclaimOrder(const FeatureId& target) : target(target) {}
    };

    /** Restore a damaged unit to full health, or finish it if it is still under construction. */
    struct RepairOrder
    {
        UnitId target;
        explicit RepairOrder(const UnitId& target) : target(target) {}
    };

    /**
     * Move to a point, engaging enemies met on the way. On arrival the order
     * is re-queued at the back, so a sequence of patrol orders loops forever.
     */
    struct PatrolOrder
    {
        SimVector destination;
        explicit PatrolOrder(const SimVector& destination) : destination(destination) {}
    };

    /** Take over an enemy unit. Only units whose definition has canCapture may do this. */
    struct CaptureOrder
    {
        UnitId target;
        explicit CaptureOrder(const UnitId& target) : target(target) {}
    };

    /** A transport picks up a friendly unit. */
    struct LoadOrder
    {
        UnitId target;
        explicit LoadOrder(const UnitId& target) : target(target) {}
    };

    /**
     * Fire a commandfire weapon -- in the shipped data the commander's
     * disintegrator, and the bombs and nukes -- at a unit or at a point.
     *
     * This is its own order rather than a flag on AttackOrder because it is
     * its own thing in the original: `candgun` is capability bit 14 and leads
     * to the ATTACKSPECIAL mission (0x43F7EE), separate from ATTACK. It has
     * to be separate, because a commandfire weapon is deliberately invisible
     * to every automatic path -- the auto-target scan skips it and so does
     * return fire (0x408A88) -- so an explicit order is the only thing that
     * can ever point it at anything.
     */
    struct DgunOrder
    {
        AttackTarget target;
        explicit DgunOrder(UnitId target) : target(target) {}
        explicit DgunOrder(const SimVector& target) : target(target) {}
    };

    /**
     * Fly to a friendly air repair pad, set down on it, and stay there until
     * whole. The original makes this a mission of its own -- `VTOL_LANDING`
     * carrying the pad as its target (0x4105B9) -- pushed to the *front* of
     * the aircraft's mission list (0x43ACB0), so whatever it was doing is
     * still underneath and resumes the moment the repair finishes. On arrival
     * the landing mission swaps itself for `SELFREPAIR` (0x411ECE), which is
     * ground mission 20 at 0x402430 and does nothing but wait for full health
     * before returning 5 and letting the mission underneath take over again.
     */
    struct LandOnAirBaseOrder
    {
        UnitId target;
        explicit LandOnAirBaseOrder(const UnitId& target) : target(target) {}
    };

    /** A transport sets down everything it carries at a point. */
    struct UnloadOrder
    {
        SimVector destination;
        explicit UnloadOrder(const SimVector& destination) : destination(destination) {}
    };

    using UnitOrder = std::variant<MoveOrder, AttackOrder, BuildOrder, BuggerOffOrder, CompleteBuildOrder, GuardOrder, ReclaimOrder, RepairOrder, PatrolOrder, CaptureOrder, LoadOrder, UnloadOrder, DgunOrder, LandOnAirBaseOrder>;
}
