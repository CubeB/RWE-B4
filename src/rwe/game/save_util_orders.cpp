#include "save_util.h"

#include "SaveJson.h"

#include <rwe/util/match.h>
#include <stdexcept>
#include <vector>

namespace rwe
{
    using nlohmann::json;

    /**
     * Per-type save and load helpers for orders, behaviour, navigation, physics and life state.
     *
     * These sit outside any anonymous namespace: the field table that drives
     * the hash, save and dump walks holds pointers to them, and it is shared
     * by GameHash_util and dump_util.
     *
     * They live apart from save_util.cpp because the JSON machinery is
     * instantiated per type and each instantiation is four COFF sections at
     * -O0, so the file they came from had reached 87% of the 32767 an object
     * can describe. See CLAUDE.md, "A translation unit can outgrow what a
     * COFF object can describe".
     */
    // ---- orders -----------------------------------------------------

    json saveAttackTarget(const AttackTarget& t, const SaveContext& ctx)
    {
        return match(
            t,
            [&](const UnitId& id) { return json{{"kind", "unit"}, {"target", saveUnitIdRef(id, ctx)}}; },
            [&](const SimVector& v) { return json{{"kind", "position"}, {"target", saveSimVector(v)}}; });
    }

    AttackTarget loadAttackTarget(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "unit")
        {
            return loadUnitIdRef(j.at("target"), ctx);
        }
        if (kind == "position")
        {
            return loadSimVector(j.at("target"));
        }
        throw std::runtime_error("bad AttackTarget kind: " + kind);
    }

    json saveReclaimTarget(const std::variant<UnitId, FeatureId>& t, const SaveContext& ctx)
    {
        return match(
            t,
            [&](const UnitId& id) { return json{{"kind", "unit"}, {"target", saveUnitIdRef(id, ctx)}}; },
            [&](const FeatureId& id) { return json{{"kind", "feature"}, {"target", saveFeatureIdRef(id, ctx)}}; });
    }

    std::variant<UnitId, FeatureId> loadReclaimTarget(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "unit")
        {
            return loadUnitIdRef(j.at("target"), ctx);
        }
        if (kind == "feature")
        {
            return loadFeatureIdRef(j.at("target"), ctx);
        }
        throw std::runtime_error("bad reclaim target kind: " + kind);
    }

    json saveUnitOrder(const UnitOrder& o, const SaveContext& ctx)
    {
        return match(
            o,
            [](const MoveOrder& m) { return json{{"kind", "move"}, {"destination", saveSimVector(m.destination)}}; },
            [&](const AttackOrder& a) {
                auto j = json{{"kind", "attack"}, {"target", saveAttackTarget(a.target, ctx)}};
                if (a.leash)
                {
                    j["leashAnchor"] = saveSimVector(a.leash->anchor);
                    j["leashDistance"] = saveSimScalar(a.leash->distance);
                }
                if (a.lastSeenPosition)
                {
                    j["lastSeenPosition"] = saveSimVector(*a.lastSeenPosition);
                }
                return j;
            },
            [](const BuildOrder& b) { return json{{"kind", "build"}, {"unitType", b.unitType}, {"position", saveSimVector(b.position)}}; },
            [](const BuggerOffOrder& b) { return json{{"kind", "buggerOff"}, {"rect", saveDiscreteRect(b.rect)}}; },
            [&](const CompleteBuildOrder& c) { return json{{"kind", "completeBuild"}, {"target", saveUnitIdRef(c.target, ctx)}}; },
            [&](const GuardOrder& g) { return json{{"kind", "guard"}, {"target", saveUnitIdRef(g.target, ctx)}}; },
            [&](const ReclaimOrder& r) { return json{{"kind", "reclaim"}, {"target", saveReclaimTarget(r.target, ctx)}}; },
            [&](const RepairOrder& r) { return json{{"kind", "repair"}, {"target", saveUnitIdRef(r.target, ctx)}}; },
            [](const PatrolOrder& p) { return json{{"kind", "patrol"}, {"destination", saveSimVector(p.destination)}}; },
            [&](const ResurrectOrder& r) {
                // The countdown rides on the order, as capture's progress
                // does -- see TOTALA-EXE.md S:96 and S:98.
                auto j = json{{"kind", "resurrect"}, {"target", saveFeatureIdRef(r.target, ctx)}};
                if (r.remainingTicks)
                {
                    j["remainingTicks"] = *r.remainingTicks;
                }
                return j;
            },
            [&](const CaptureOrder& c) {
                // Capture progress rides on the order, not on the
                // target -- see CaptureOrder -- so it is saved here.
                auto j = json{{"kind", "capture"}, {"target", saveUnitIdRef(c.target, ctx)}, {"progress", c.progress}};
                if (c.totalWork)
                {
                    j["totalWork"] = *c.totalWork;
                }
                return j;
            },
            [&](const LoadOrder& l) { return json{{"kind", "load"}, {"target", saveUnitIdRef(l.target, ctx)}}; },
            [](const UnloadOrder& u) { return json{{"kind", "unload"}, {"destination", saveSimVector(u.destination)}, {"parkedUntil", saveGameTime(u.parkedUntil)}}; },
            [&](const DgunOrder& d) { return json{{"kind", "dgun"}, {"target", saveAttackTarget(d.target, ctx)}}; },
            [&](const LandOnAirBaseOrder& l) { return json{{"kind", "landOnAirBase"}, {"target", saveUnitIdRef(l.target, ctx)}}; });
    }

    UnitOrder loadUnitOrder(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "move")
        {
            return MoveOrder(loadSimVector(j.at("destination")));
        }
        if (kind == "attack")
        {
            auto order = match(
                loadAttackTarget(j.at("target"), ctx),
                [](const UnitId& id) { return AttackOrder(id); },
                [](const SimVector& v) { return AttackOrder(v); });
            if (j.contains("leashAnchor"))
            {
                order.leash = AttackLeash(loadSimVector(j.at("leashAnchor")), loadSimScalar(j.at("leashDistance")));
            }
            if (j.contains("lastSeenPosition"))
            {
                order.lastSeenPosition = loadSimVector(j.at("lastSeenPosition"));
            }
            return order;
        }
        if (kind == "build")
        {
            return BuildOrder(j.at("unitType").get<std::string>(), loadSimVector(j.at("position")));
        }
        if (kind == "buggerOff")
        {
            return BuggerOffOrder(loadDiscreteRect(j.at("rect")));
        }
        if (kind == "completeBuild")
        {
            return CompleteBuildOrder(loadUnitIdRef(j.at("target"), ctx));
        }
        if (kind == "guard")
        {
            return GuardOrder(loadUnitIdRef(j.at("target"), ctx));
        }
        if (kind == "resurrect")
        {
            auto order = ResurrectOrder(loadFeatureIdRef(j.at("target"), ctx));
            if (j.contains("remainingTicks"))
            {
                order.remainingTicks = j.at("remainingTicks").get<unsigned int>();
            }
            return order;
        }
        if (kind == "reclaim")
        {
            return match(
                loadReclaimTarget(j.at("target"), ctx),
                [](const UnitId& id) { return ReclaimOrder(id); },
                [](const FeatureId& id) { return ReclaimOrder(id); });
        }
        if (kind == "repair")
        {
            return RepairOrder(loadUnitIdRef(j.at("target"), ctx));
        }
        if (kind == "patrol")
        {
            return PatrolOrder(loadSimVector(j.at("destination")));
        }
        if (kind == "dgun")
        {
            return match(
                loadAttackTarget(j.at("target"), ctx),
                [](const UnitId& id) { return DgunOrder(id); },
                [](const SimVector& v) { return DgunOrder(v); });
        }
        if (kind == "capture")
        {
            auto order = CaptureOrder(loadUnitIdRef(j.at("target"), ctx));
            order.progress = j.value("progress", 0u);
            if (j.contains("totalWork"))
            {
                order.totalWork = j.at("totalWork").get<unsigned int>();
            }
            return order;
        }
        if (kind == "load")
        {
            return LoadOrder(loadUnitIdRef(j.at("target"), ctx));
        }
        if (kind == "unload")
        {
            auto order = UnloadOrder(loadSimVector(j.at("destination")));
            if (j.contains("parkedUntil"))
            {
                order.parkedUntil = loadGameTime(j.at("parkedUntil"));
            }
            return order;
        }
        if (kind == "landOnAirBase")
        {
            return LandOnAirBaseOrder(loadUnitIdRef(j.at("target"), ctx));
        }
        throw std::runtime_error("bad UnitOrder kind: " + kind);
    }

    // ---- behaviour and factory state --------------------------------

    json saveUnitCreationStatus(const UnitCreationStatus& s, const SaveContext& ctx)
    {
        return match(
            s,
            [](const UnitCreationStatusPending& p) { return json{{"kind", "pending"}, {"attempts", p.attempts}, {"nextAttempt", saveGameTime(p.nextAttempt)}}; },
            [](const UnitCreationStatusFailed&) { return json{{"kind", "failed"}}; },
            [&](const UnitCreationStatusDone& d) { return json{{"kind", "done"}, {"unitId", saveUnitIdRef(d.unitId, ctx)}}; });
    }

    UnitCreationStatus loadUnitCreationStatus(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "pending")
        {
            return UnitCreationStatusPending{j.at("attempts").get<unsigned int>(), loadGameTime(j.at("nextAttempt"))};
        }
        if (kind == "failed")
        {
            return UnitCreationStatusFailed();
        }
        if (kind == "done")
        {
            return UnitCreationStatusDone{loadUnitIdRef(j.at("unitId"), ctx)};
        }
        throw std::runtime_error("bad UnitCreationStatus kind: " + kind);
    }

    json saveUnitBehaviorState(const UnitBehaviorState& s, const SaveContext& ctx)
    {
        return match(
            s,
            [](const UnitBehaviorStateIdle&) { return json{{"kind", "idle"}}; },
            [&](const UnitBehaviorStateCreatingUnit& c) {
                return json{
                    {"kind", "creatingUnit"},
                    {"unitType", c.unitType},
                    {"owner", c.owner.value},
                    {"position", saveSimVector(c.position)},
                    {"status", saveUnitCreationStatus(c.status, ctx)}};
            },
            [&](const UnitBehaviorStateBuilding& b) {
                return json{
                    {"kind", "building"},
                    {"targetUnit", saveUnitIdRef(b.targetUnit, ctx)},
                    {"nanoParticleOrigin", saveOptional(b.nanoParticleOrigin, saveSimVector)}};
            },
            [&](const UnitBehaviorStateReclaiming& r) {
                return json{
                    {"kind", "reclaiming"},
                    {"target", saveReclaimTarget(r.target, ctx)},
                    {"nanoParticleOrigin", saveOptional(r.nanoParticleOrigin, saveSimVector)},
                    {"stepCounter", r.stepCounter}};
            },
            [&](const UnitBehaviorStateResurrecting& r) {
                return json{
                    {"kind", "resurrecting"},
                    {"target", saveFeatureIdRef(r.target, ctx)},
                    {"nanoParticleOrigin", saveOptional(r.nanoParticleOrigin, saveSimVector)}};
            });
    }

    UnitBehaviorState loadUnitBehaviorState(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "idle")
        {
            return UnitBehaviorStateIdle();
        }
        if (kind == "creatingUnit")
        {
            return UnitBehaviorStateCreatingUnit{
                j.at("unitType").get<std::string>(),
                PlayerId(j.at("owner").get<unsigned int>()),
                loadSimVector(j.at("position")),
                loadUnitCreationStatus(j.at("status"), ctx)};
        }
        if (kind == "building")
        {
            return UnitBehaviorStateBuilding{
                loadUnitIdRef(j.at("targetUnit"), ctx),
                loadOptional(j.at("nanoParticleOrigin"), loadSimVector)};
        }
        if (kind == "reclaiming")
        {
            UnitBehaviorStateReclaiming r{
                loadReclaimTarget(j.at("target"), ctx),
                loadOptional(j.at("nanoParticleOrigin"), loadSimVector)};
            // Hashed, so a save taken between two bites must bring the
            // count back; a save from before it existed starts at zero.
            if (j.contains("stepCounter"))
            {
                r.stepCounter = j.at("stepCounter").get<unsigned int>();
            }
            return r;
        }
        if (kind == "resurrecting")
        {
            return UnitBehaviorStateResurrecting{
                loadFeatureIdRef(j.at("target"), ctx),
                loadOptional(j.at("nanoParticleOrigin"), loadSimVector)};
        }
        throw std::runtime_error("bad UnitBehaviorState kind: " + kind);
    }

    json saveFactoryBehaviorState(const FactoryBehaviorState& s, const SaveContext& ctx)
    {
        return match(
            s,
            [](const FactoryBehaviorStateIdle&) { return json{{"kind", "idle"}}; },
            [&](const FactoryBehaviorStateCreatingUnit& c) {
                return json{
                    {"kind", "creatingUnit"},
                    {"unitType", c.unitType},
                    {"owner", c.owner.value},
                    {"position", saveSimVector(c.position)},
                    {"rotation", saveSimAngle(c.rotation)},
                    {"status", saveUnitCreationStatus(c.status, ctx)}};
            },
            [&](const FactoryBehaviorStateBuilding& b) {
                return json{
                    {"kind", "building"},
                    {"targetUnit", saveOptional(b.targetUnit, [&](const std::pair<UnitId, std::optional<SimVector>>& p) {
                         return json{
                             {"unit", saveUnitIdRef(p.first, ctx)},
                             {"nanoParticleOrigin", saveOptional(p.second, saveSimVector)}};
                     })}};
            });
    }

    FactoryBehaviorState loadFactoryBehaviorState(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "idle")
        {
            return FactoryBehaviorStateIdle();
        }
        if (kind == "creatingUnit")
        {
            return FactoryBehaviorStateCreatingUnit{
                j.at("unitType").get<std::string>(),
                PlayerId(j.at("owner").get<unsigned int>()),
                loadSimVector(j.at("position")),
                loadSimAngle(j.at("rotation")),
                loadUnitCreationStatus(j.at("status"), ctx)};
        }
        if (kind == "building")
        {
            return FactoryBehaviorStateBuilding{
                loadOptional(j.at("targetUnit"), [&](const json& p) {
                    return std::make_pair(
                        loadUnitIdRef(p.at("unit"), ctx),
                        loadOptional(p.at("nanoParticleOrigin"), loadSimVector));
                })};
        }
        throw std::runtime_error("bad FactoryBehaviorState kind: " + kind);
    }

    // ---- navigation -------------------------------------------------

    json saveNavigationGoal(const NavigationGoal& g, const SaveContext& ctx)
    {
        return match(
            g,
            [&](const UnitId& id) { return json{{"kind", "unit"}, {"goal", saveUnitIdRef(id, ctx)}}; },
            [&](const FeatureId& id) { return json{{"kind", "feature"}, {"goal", saveFeatureIdRef(id, ctx)}}; },
            [](const SimVector& v) { return json{{"kind", "position"}, {"goal", saveSimVector(v)}}; },
            [](const DiscreteRect& r) { return json{{"kind", "rect"}, {"goal", saveDiscreteRect(r)}}; },
            [](const NavigationGoalLandingLocation&) { return json{{"kind", "landingLocation"}}; });
    }

    NavigationGoal loadNavigationGoal(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "unit")
        {
            return loadUnitIdRef(j.at("goal"), ctx);
        }
        if (kind == "feature")
        {
            return loadFeatureIdRef(j.at("goal"), ctx);
        }
        if (kind == "position")
        {
            return loadSimVector(j.at("goal"));
        }
        if (kind == "rect")
        {
            return loadDiscreteRect(j.at("goal"));
        }
        if (kind == "landingLocation")
        {
            return NavigationGoalLandingLocation();
        }
        throw std::runtime_error("bad NavigationGoal kind: " + kind);
    }

    json saveMovingStateGoal(const MovingStateGoal& g, const SaveContext& ctx)
    {
        return match(
            g,
            [&](const UnitId& id) { return json{{"kind", "unit"}, {"goal", saveUnitIdRef(id, ctx)}}; },
            [](const SimVector& v) { return json{{"kind", "position"}, {"goal", saveSimVector(v)}}; },
            [](const DiscreteRect& r) { return json{{"kind", "rect"}, {"goal", saveDiscreteRect(r)}}; });
    }

    MovingStateGoal loadMovingStateGoal(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "unit")
        {
            return loadUnitIdRef(j.at("goal"), ctx);
        }
        if (kind == "position")
        {
            return loadSimVector(j.at("goal"));
        }
        if (kind == "rect")
        {
            return loadDiscreteRect(j.at("goal"));
        }
        throw std::runtime_error("bad MovingStateGoal kind: " + kind);
    }

    json savePathDestination(const PathDestination& d)
    {
        return match(
            d,
            [](const SimVector& v) { return json{{"kind", "position"}, {"destination", saveSimVector(v)}}; },
            [](const DiscreteRect& r) { return json{{"kind", "rect"}, {"destination", saveDiscreteRect(r)}}; });
    }

    PathDestination loadPathDestination(const json& j)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "position")
        {
            return loadSimVector(j.at("destination"));
        }
        if (kind == "rect")
        {
            return loadDiscreteRect(j.at("destination"));
        }
        throw std::runtime_error("bad PathDestination kind: " + kind);
    }

    json savePathFollowingInfo(const PathFollowingInfo& p)
    {
        json waypoints = json::array();
        for (const auto& w : p.path.waypoints)
        {
            waypoints.push_back(saveSimVector(w));
        }
        return json{
            {"waypoints", waypoints},
            {"destinationUnreachable", p.path.destinationUnreachable},
            {"pathCreationTime", saveGameTime(p.pathCreationTime)},
            // Stored as an index; the iterator itself is meaningless
            // outside the vector it points into.
            {"currentWaypoint", static_cast<std::size_t>(p.currentWaypoint - p.path.waypoints.begin())}};
    }

    PathFollowingInfo loadPathFollowingInfo(const json& j)
    {
        UnitPath path;
        for (const auto& w : j.at("waypoints"))
        {
            path.waypoints.push_back(loadSimVector(w));
        }
        path.destinationUnreachable = j.at("destinationUnreachable").get<bool>();
        PathFollowingInfo info(std::move(path), loadGameTime(j.at("pathCreationTime")));
        info.currentWaypoint = info.path.waypoints.begin() + j.at("currentWaypoint").get<std::size_t>();
        return info;
    }

    json saveNavigationState(const NavigationState& s, const SaveContext& ctx)
    {
        return match(
            s,
            [](const NavigationStateIdle&) { return json{{"kind", "idle"}}; },
            [&](const NavigationStateMoving& m) {
                return json{
                    {"kind", "moving"},
                    {"movementGoal", saveMovingStateGoal(m.movementGoal, ctx)},
                    {"pathDestination", savePathDestination(m.pathDestination)},
                    {"path", saveOptional(m.path, savePathFollowingInfo)},
                    {"pathRequested", m.pathRequested},
                    {"reachableDestination", saveOptional(m.reachableDestination, saveSimVector)}};
            },
            [](const NavigationStateMovingToLandingSpot& m) {
                return json{
                    {"kind", "movingToLandingSpot"},
                    {"landingLocation", saveSimVector(m.landingLocation)}};
            });
    }

    NavigationState loadNavigationState(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "idle")
        {
            return NavigationStateIdle();
        }
        if (kind == "moving")
        {
            return NavigationStateMoving{
                loadMovingStateGoal(j.at("movementGoal"), ctx),
                loadPathDestination(j.at("pathDestination")),
                loadOptional(j.at("path"), loadPathFollowingInfo),
                j.at("pathRequested").get<bool>(),
                loadOptional(j.at("reachableDestination"), loadSimVector)};
        }
        if (kind == "movingToLandingSpot")
        {
            return NavigationStateMovingToLandingSpot{loadSimVector(j.at("landingLocation"))};
        }
        throw std::runtime_error("bad NavigationState kind: " + kind);
    }

    json saveNavigationStateInfo(const NavigationStateInfo& i, const SaveContext& ctx)
    {
        return json{
            {"desiredDestination", saveOptional(i.desiredDestination, [&](const NavigationGoal& g) { return saveNavigationGoal(g, ctx); })},
            {"unitPositionCache", saveOptional(i.unitPositionCache, [&](const UnitPositionCache& c) {
                 return json{
                     {"unitId", saveUnitIdRef(c.unitId, ctx)},
                     {"position", saveSimVector(c.position)},
                     {"cachedAtTime", saveGameTime(c.cachedAtTime)}};
             })},
            {"attackApproachCache", saveOptional(i.attackApproachCache, [&](const UnitPositionCache& c) {
                 return json{
                     {"unitId", saveUnitIdRef(c.unitId, ctx)},
                     {"position", saveSimVector(c.position)},
                     {"cachedAtTime", saveGameTime(c.cachedAtTime)}};
             })},
            {"state", saveNavigationState(i.state, ctx)}};
    }

    NavigationStateInfo loadNavigationStateInfo(const json& j, const LoadContext& ctx)
    {
        return NavigationStateInfo{
            loadOptional(j.at("desiredDestination"), [&](const json& g) { return loadNavigationGoal(g, ctx); }),
            loadOptional(j.at("unitPositionCache"), [&](const json& c) {
                return UnitPositionCache{
                    loadUnitIdRef(c.at("unitId"), ctx),
                    loadSimVector(c.at("position")),
                    loadGameTime(c.at("cachedAtTime"))};
            }),
            loadOptional(j.at("attackApproachCache"), [&](const json& c) {
                return UnitPositionCache{
                    loadUnitIdRef(c.at("unitId"), ctx),
                    loadSimVector(c.at("position")),
                    loadGameTime(c.at("cachedAtTime"))};
            }),
            loadNavigationState(j.at("state"), ctx)};
    }

    // ---- physics ----------------------------------------------------

    json saveSteeringInfo(const SteeringInfo& s)
    {
        return json{
            {"targetAngle", saveSimAngle(s.targetAngle)},
            {"targetSpeed", saveSimScalar(s.targetSpeed)},
            {"shouldTakeOff", s.shouldTakeOff}};
    }

    SteeringInfo loadSteeringInfo(const json& j)
    {
        return SteeringInfo{
            loadSimAngle(j.at("targetAngle")),
            loadSimScalar(j.at("targetSpeed")),
            j.at("shouldTakeOff").get<bool>()};
    }

    json saveAirMovementState(const AirMovementState& s, const SaveContext& ctx)
    {
        return match(
            s,
            [](const AirMovementStateTakingOff& t) {
                return json{
                    {"kind", "takingOff"},
                    {"targetPosition", saveOptional(t.targetPosition, saveSimVector)},
                    {"currentVelocity", saveSimVector(t.currentVelocity)}};
            },
            [](const AirMovementStateFlying& f) {
                return json{
                    {"kind", "flying"},
                    {"targetPosition", saveOptional(f.targetPosition, saveSimVector)},
                    {"shouldLand", f.shouldLand},
                    {"currentVelocity", saveSimVector(f.currentVelocity)}};
            },
            [](const AirMovementStateLanding& l) {
                return json{
                    {"kind", "landing"},
                    {"landingFailed", l.landingFailed},
                    {"shouldAbort", l.shouldAbort}};
            },
            [&](const AirMovementStateAttackRun& a) {
                return json{
                    {"kind", "attackRun"},
                    {"target", saveAttackTarget(a.target, ctx)},
                    {"lastKnownTargetPos", saveSimVector(a.lastKnownTargetPos)},
                    {"runOutDirection", saveSimVector(a.runOutDirection)},
                    {"runOutDistance", saveSimScalar(a.runOutDistance)},
                    {"phase", saveEnum(a.phase)},
                    {"bombsDroppedThisPass", a.bombsDroppedThisPass},
                    {"strafingPass", a.strafingPass},
                    {"breakWaypoint", saveSimVector(a.breakWaypoint)},
                    {"currentVelocity", saveSimVector(a.currentVelocity)}};
            },
            [&](const AirMovementStateHoverAttack& h) {
                return json{
                    {"kind", "hoverAttack"},
                    {"target", saveAttackTarget(h.target, ctx)},
                    {"station", saveSimVector(h.station)},
                    {"targetPosition", saveSimVector(h.targetPosition)},
                    {"swingPositive", h.swingPositive},
                    {"outOfRangeArrivals", h.outOfRangeArrivals},
                    {"phase", saveEnum(h.phase)},
                    {"currentVelocity", saveSimVector(h.currentVelocity)}};
            },
            [&](const AirMovementStateDogfight& d) {
                return json{
                    {"kind", "dogfight"},
                    {"target", saveAttackTarget(d.target, ctx)},
                    {"phase", saveEnum(d.phase)},
                    {"goalPosition", saveSimVector(d.goalPosition)},
                    {"goalVelocity", saveSimVector(d.goalVelocity)},
                    {"nextDecision", d.nextDecision.value},
                    {"offNoseCounter", d.offNoseCounter},
                    {"breakLeft", d.breakLeft},
                    {"breakWaypoint", saveSimVector(d.breakWaypoint)},
                    {"currentVelocity", saveSimVector(d.currentVelocity)}};
            });
    }

    AirMovementState loadAirMovementState(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "takingOff")
        {
            AirMovementStateTakingOff t;
            t.targetPosition = loadOptional(j.at("targetPosition"), loadSimVector);
            t.currentVelocity = loadSimVector(j.at("currentVelocity"));
            return t;
        }
        if (kind == "flying")
        {
            AirMovementStateFlying f;
            f.targetPosition = loadOptional(j.at("targetPosition"), loadSimVector);
            f.shouldLand = j.at("shouldLand").get<bool>();
            f.currentVelocity = loadSimVector(j.at("currentVelocity"));
            return f;
        }
        if (kind == "landing")
        {
            AirMovementStateLanding l;
            l.landingFailed = j.at("landingFailed").get<bool>();
            l.shouldAbort = j.at("shouldAbort").get<bool>();
            return l;
        }
        if (kind == "attackRun")
        {
            AirMovementStateAttackRun a(loadAttackTarget(j.at("target"), ctx));
            a.lastKnownTargetPos = loadSimVector(j.at("lastKnownTargetPos"));
            a.runOutDirection = loadSimVector(j.at("runOutDirection"));
            a.runOutDistance = loadSimScalar(j.at("runOutDistance"));
            a.phase = loadEnum<AirMovementStateAttackRun::Phase>(j.at("phase"));
            a.bombsDroppedThisPass = j.at("bombsDroppedThisPass").get<unsigned int>();
            a.strafingPass = j.at("strafingPass").get<bool>();
            a.breakWaypoint = loadSimVector(j.at("breakWaypoint"));
            a.currentVelocity = loadSimVector(j.at("currentVelocity"));
            return a;
        }
        if (kind == "hoverAttack")
        {
            AirMovementStateHoverAttack h(loadAttackTarget(j.at("target"), ctx));
            h.station = loadSimVector(j.at("station"));
            h.targetPosition = loadSimVector(j.at("targetPosition"));
            h.swingPositive = j.at("swingPositive").get<bool>();
            h.outOfRangeArrivals = j.at("outOfRangeArrivals").get<unsigned int>();
            h.phase = loadEnum<AirMovementStateHoverAttack::Phase>(j.at("phase"));
            h.currentVelocity = loadSimVector(j.at("currentVelocity"));
            return h;
        }
        if (kind == "dogfight")
        {
            AirMovementStateDogfight d(loadAttackTarget(j.at("target"), ctx));
            d.phase = loadEnum<AirMovementStateDogfight::Phase>(j.at("phase"));
            d.goalPosition = loadSimVector(j.at("goalPosition"));
            d.goalVelocity = loadSimVector(j.at("goalVelocity"));
            d.nextDecision = GameTime(j.at("nextDecision").get<unsigned int>());
            d.offNoseCounter = j.at("offNoseCounter").get<unsigned int>();
            d.breakLeft = j.at("breakLeft").get<bool>();
            d.breakWaypoint = loadSimVector(j.at("breakWaypoint"));
            d.currentVelocity = loadSimVector(j.at("currentVelocity"));
            return d;
        }
        throw std::runtime_error("bad AirMovementState kind: " + kind);
    }

    json saveUnitPhysicsInfo(const UnitPhysicsInfo& p, const SaveContext& ctx)
    {
        return match(
            p,
            [](const UnitPhysicsInfoGround& g) {
                return json{
                    {"kind", "ground"},
                    {"steeringInfo", saveSteeringInfo(g.steeringInfo)},
                    {"currentSpeed", saveSimScalar(g.currentSpeed)}};
            },
            [&](const UnitPhysicsInfoAir& a) {
                return json{
                    {"kind", "air"},
                    {"movementState", saveAirMovementState(a.movementState, ctx)},
                    {"roll", saveSimScalar(a.roll)},
                    {"previousRoll", saveSimScalar(a.previousRoll)},
                    {"bankAccum", saveSimVector(a.bankAccum)}};
            });
    }

    UnitPhysicsInfo loadUnitPhysicsInfo(const json& j, const LoadContext& ctx)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "ground")
        {
            UnitPhysicsInfoGround g;
            g.steeringInfo = loadSteeringInfo(j.at("steeringInfo"));
            g.currentSpeed = loadSimScalar(j.at("currentSpeed"));
            return g;
        }
        if (kind == "air")
        {
            UnitPhysicsInfoAir a;
            a.movementState = loadAirMovementState(j.at("movementState"), ctx);
            a.roll = loadSimScalar(j.at("roll"));
            a.previousRoll = loadSimScalar(j.at("previousRoll"));
            a.bankAccum = loadSimVector(j.at("bankAccum"));
            return a;
        }
        throw std::runtime_error("bad UnitPhysicsInfo kind: " + kind);
    }

    // ---- unit state -------------------------------------------------

    json saveLifeState(const UnitState::LifeState& s)
    {
        return match(
            s,
            [](const UnitState::LifeStateAlive&) { return json{{"kind", "alive"}}; },
            [](const UnitState::LifeStateDead& d) { return json{{"kind", "dead"}, {"leaveCorpse", d.leaveCorpse}, {"corpseLevel", d.corpseLevel}}; });
    }

    UnitState::LifeState loadLifeState(const json& j)
    {
        const auto& kind = j.at("kind").get_ref<const std::string&>();
        if (kind == "alive")
        {
            return UnitState::LifeStateAlive();
        }
        if (kind == "dead")
        {
            return UnitState::LifeStateDead{j.at("leaveCorpse").get<bool>(), j.value("corpseLevel", 1u)};
        }
        throw std::runtime_error("bad LifeState kind: " + kind);
    }

    json saveAirWorkOrbitState(const UnitState::AirWorkOrbitState& s)
    {
        return json{
            {"workPosition", saveSimVector(s.workPosition)},
            {"bearing", saveSimAngle(s.bearing)},
            {"started", s.started}};
    }

    UnitState::AirWorkOrbitState loadAirWorkOrbitState(const json& j)
    {
        return UnitState::AirWorkOrbitState{
            loadSimVector(j.at("workPosition")),
            loadSimAngle(j.at("bearing")),
            j.at("started").get<bool>()};
    }

    json saveAirLoiterState(const UnitState::AirLoiterState& s)
    {
        return json{
            {"reason", saveEnum(s.reason)},
            {"anchor", saveSimVector(s.anchor)},
            {"bearing", saveSimAngle(s.bearing)}};
    }

    UnitState::AirLoiterState loadAirLoiterState(const json& j)
    {
        return UnitState::AirLoiterState{
            loadEnum<UnitState::AirLoiterState::Reason>(j.at("reason")),
            loadSimVector(j.at("anchor")),
            loadSimAngle(j.at("bearing"))};
    }
}
