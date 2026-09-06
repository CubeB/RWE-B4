#include "dump_util.h"

namespace rwe
{
    nlohmann::json dumpJson(float f)
    {
        return f;
    }
    nlohmann::json dumpJson(bool b)
    {
        return b;
    }
    nlohmann::json dumpJson(uint32_t i)
    {
        return i;
    }
    nlohmann::json dumpJson(int32_t i)
    {
        return i;
    }
    nlohmann::json dumpJson(const std::string& s)
    {
        return s;
    }
    nlohmann::json dumpJson(const char* s)
    {
        return s;
    }
    nlohmann::json dumpJson(const GamePlayerInfo& p)
    {
        return nlohmann::json{
            {"type", dumpJson(p.type)},
            {"color", dumpJson(p.color)},
            {"status", dumpJson(p.status)},
            {"side", dumpJson(p.side)},
            {"metal", dumpJson(p.metal)},
            {"maxMetal", dumpJson(p.maxMetal)},
            {"energy", dumpJson(p.energy)},
            {"maxEnergy", dumpJson(p.maxEnergy)},
            {"metalStalled", dumpJson(p.metalStalled)},
            {"energyStalled", dumpJson(p.energyStalled)},
            {"unitsKilled", dumpJson(p.unitsKilled)},
            {"unitsLost", dumpJson(p.unitsLost)},
            {"desiredMetalConsumptionBuffer", dumpJson(p.desiredMetalConsumptionBuffer)},
            {"desiredEnergyConsumptionBuffer", dumpJson(p.desiredEnergyConsumptionBuffer)},
            {"previousDesiredMetalConsumptionBuffer", dumpJson(p.previousDesiredMetalConsumptionBuffer)},
            {"previousDesiredEnergyConsumptionBuffer", dumpJson(p.previousDesiredEnergyConsumptionBuffer)},
            {"metalRequestBuffer", dumpJson(p.metalRequestBuffer)},
            {"energyRequestBuffer", dumpJson(p.energyRequestBuffer)},
            {"metalDebt", dumpJson(p.metalDebt)},
            {"energyDebt", dumpJson(p.energyDebt)},
            {"metalProductionBuffer", dumpJson(p.metalProductionBuffer)},
            {"energyProductionBuffer", dumpJson(p.energyProductionBuffer)},
            {"previousMetalProductionBuffer", dumpJson(p.previousMetalProductionBuffer)},
            {"previousEnergyProductionBuffer", dumpJson(p.previousEnergyProductionBuffer)}};
    }
    nlohmann::json dumpJson(const UnitState& u)
    {
        return nlohmann::json{
            {"unitType", dumpJson(u.unitType)},
            {"position", dumpJson(u.position)},
            {"owner", dumpJson(u.owner)},
            {"rotation", dumpJson(u.rotation)},
            {"physics", dumpJson(u.physics)},
            {"hitPoints", dumpJson(u.hitPoints)},
            {"lifeState", dumpJson(u.lifeState)},
            {"navigationState", dumpJson(u.navigationState)},
            {"behaviourState", dumpJson(u.behaviourState)},
            {"inBuildStance", dumpJson(u.inBuildStance)},
            {"yardOpen", dumpJson(u.yardOpen)},
            {"inCollision", dumpJson(u.inCollision)},
            {"fireOrders", dumpJson(u.fireOrders)},
            {"moveOrders", dumpJson(u.moveOrders)},
            {"armored", dumpJson(u.armored)},
            {"kills", dumpJson(u.kills)},
            {"sfxOccupyState", dumpJson(u.sfxOccupyState)},
            {"buildTimeCompleted", dumpJson(u.buildTimeCompleted)},
            {"nanoframeDecayTime", dumpJson(u.nanoframeDecayTime)},
            {"nanoframeWorkedOn", dumpJson(u.nanoframeWorkedOn)},
            {"nanoframeDecayRemainder", dumpJson(u.nanoframeDecayRemainder)},
            {"reclaimProgress", dumpJson(u.reclaimProgress)},
            {"selfDestructTime", dumpJson(u.selfDestructTime)},
            {"paralyzedUntil", dumpJson(u.paralyzedUntil)},
            {"moveRateBand", dumpJson(u.moveRateBand)},
            {"carriedBy", dumpJson(u.carriedBy)},
            {"airLoiter", dumpJson(u.airLoiter)},
            {"activated", dumpJson(u.activated)},
            {"isSufficientlyPowered", dumpJson(u.isSufficientlyPowered)},
            {"cloakRequested", dumpJson(u.cloakRequested)},
            {"cloaked", dumpJson(u.cloaked)},
            {"cloakSuppressedUntil", dumpJson(u.cloakSuppressedUntil)},
            {"energyProductionBuffer", dumpJson(u.energyProductionBuffer)},
            {"metalProductionBuffer", dumpJson(u.metalProductionBuffer)},
            {"previousEnergyConsumptionBuffer", dumpJson(u.previousEnergyConsumptionBuffer)},
            {"previousMetalConsumptionBuffer", dumpJson(u.previousMetalConsumptionBuffer)},
            {"energyConsumptionBuffer", dumpJson(u.energyConsumptionBuffer)},
            {"metalConsumptionBuffer", dumpJson(u.metalConsumptionBuffer)},
            {"weapons", dumpJson(u.weapons)},
            {"previousEnergyProductionBuffer", dumpJson(u.previousEnergyProductionBuffer)},
            {"previousMetalProductionBuffer", dumpJson(u.previousMetalProductionBuffer)},
            {"energyRequestBuffer", dumpJson(u.energyRequestBuffer)},
            {"metalRequestBuffer", dumpJson(u.metalRequestBuffer)},
            {"energyDebt", dumpJson(u.energyDebt)},
            {"metalDebt", dumpJson(u.metalDebt)}};
    }

    nlohmann::json dumpJson(const UnitWeapon& w)
    {
        // Only the stockpile counters, to match what the hash covers.
        return nlohmann::json{
            {"stockedRounds", dumpJson(w.stockedRounds)},
            {"queuedRounds", dumpJson(w.queuedRounds)},
            {"stockpileProgress", dumpJson(w.stockpileProgress)},
            {"stockpileStepDelay", dumpJson(w.stockpileStepDelay)}};
    }

    nlohmann::json dumpJson(const UnitPhysicsInfoGround& p)
    {
        return nlohmann::json{
            {"steeringInfo", dumpJson(p.steeringInfo)},
            {"currentSpeed", dumpJson(p.currentSpeed)},
        };
    }

    nlohmann::json dumpJson(const UnitPhysicsInfoAir& p)
    {
        return nlohmann::json{
            {"movementState", dumpJson(p.movementState)},
        };
    }

    nlohmann::json dumpJson(const AirMovementStateTakingOff& p)
    {
        return nlohmann::json{};
    }

    nlohmann::json dumpJson(const AirMovementStateLanding& p)
    {
        return nlohmann::json{};
    }

    nlohmann::json dumpJson(const AirMovementStateFlying& p)
    {
        return nlohmann::json{};
    }

    nlohmann::json dumpJson(const AirMovementStateAttackRun& p)
    {
        const char* phaseName = "Approaching";
        switch (p.phase)
        {
            case AirMovementStateAttackRun::Phase::Approaching: phaseName = "Approaching"; break;
            case AirMovementStateAttackRun::Phase::Engaging:    phaseName = "Engaging";    break;
            case AirMovementStateAttackRun::Phase::Departing:   phaseName = "Departing";   break;
            case AirMovementStateAttackRun::Phase::Breaking:    phaseName = "Breaking";    break;
        }
        return nlohmann::json{
            {"phase", phaseName},
            {"lastKnownTargetPos", dumpJson(p.lastKnownTargetPos)},
            {"runOutDirection", dumpJson(p.runOutDirection)},
        };
    }

    nlohmann::json dumpJson(const AirMovementStateDogfight& p)
    {
        const char* phaseName = "Pursuing";
        switch (p.phase)
        {
            case AirMovementStateDogfight::Phase::Pursuing:     phaseName = "Pursuing";     break;
            case AirMovementStateDogfight::Phase::Extending:    phaseName = "Extending";    break;
            case AirMovementStateDogfight::Phase::BreakingOut:  phaseName = "BreakingOut";  break;
            case AirMovementStateDogfight::Phase::BreakingAway: phaseName = "BreakingAway"; break;
        }
        return nlohmann::json{
            {"phase", phaseName},
            {"goalPosition", dumpJson(p.goalPosition)},
            {"goalVelocity", dumpJson(p.goalVelocity)},
            {"nextDecision", p.nextDecision.value},
            {"offNoseCounter", p.offNoseCounter},
            {"breakLeft", p.breakLeft},
            {"breakWaypoint", dumpJson(p.breakWaypoint)},
        };
    }

    nlohmann::json dumpJson(const AirMovementStateHoverAttack& p)
    {
        const char* phaseName = "Closing";
        switch (p.phase)
        {
            case AirMovementStateHoverAttack::Phase::Closing:  phaseName = "Closing";  break;
            case AirMovementStateHoverAttack::Phase::Swinging: phaseName = "Swinging"; break;
        }
        return nlohmann::json{
            {"phase", phaseName},
            {"station", dumpJson(p.station)},
            {"targetPosition", dumpJson(p.targetPosition)},
            {"swingPositive", p.swingPositive},
            {"outOfRangeArrivals", p.outOfRangeArrivals},
        };
    }

    nlohmann::json dumpJson(const UnitState::AirLoiterState& s)
    {
        const char* reasonName = "AttackEnded";
        switch (s.reason)
        {
            case UnitState::AirLoiterState::Reason::AttackEnded: reasonName = "AttackEnded"; break;
            case UnitState::AirLoiterState::Reason::Guarding:    reasonName = "Guarding";    break;
        }
        return nlohmann::json{
            {"reason", reasonName},
            {"anchor", dumpJson(s.anchor)},
            {"bearing", dumpJson(s.bearing)},
        };
    }

    nlohmann::json dumpJson(const SteeringInfo& s)
    {
        return nlohmann::json{
            {"targetAngle", dumpJson(s.targetAngle)},
            {"targetSpeed", dumpJson(s.targetSpeed)},
        };
    }

    nlohmann::json dumpJson(const Vector3f& v)
    {
        return nlohmann::json{
            {"x", v.x},
            {"y", v.y},
            {"z", v.z},
        };
    }
    nlohmann::json dumpJson(const Projectile& projectile)
    {
        nlohmann::json j{
            {"owner", dumpJson(projectile.owner)},
            {"attacker", projectile.attacker ? dumpJson(*projectile.attacker) : nlohmann::json(nullptr)},
            {"position", dumpJson(projectile.position)},
            {"origin", dumpJson(projectile.origin)},
            {"velocity", dumpJson(projectile.velocity)},
            {"damageRadius", dumpJson(projectile.damageRadius)},
            {"heading", dumpJson(projectile.heading)},
            {"pitch", dumpJson(projectile.pitch)},
            {"speed", dumpJson(projectile.speed)},
            {"secondPhase", projectile.secondPhase},
            {"motorOut", projectile.motorOut},
            {"targetProjectile", projectile.targetProjectile ? dumpJson(*projectile.targetProjectile) : nlohmann::json(nullptr)}};

        for (const auto& [t, damage] : projectile.damage)
        {
            j["damage"][t] = dumpJson(damage);
        }

        return j;
    }
    nlohmann::json dumpJson(const UnitBehaviorStateIdle&)
    {
        return nlohmann::json();
    }
    nlohmann::json dumpJson(const UnitBehaviorStateBuilding& s)
    {
        return nlohmann::json{
            {"targetUnit", dumpJson(s.targetUnit)},
            {"nanoParticleOrigin", dumpJson(s.nanoParticleOrigin)}};
    }
    nlohmann::json dumpJson(const UnitBehaviorStateReclaiming& s)
    {
        return nlohmann::json{
            {"target", dumpJson(s.target)},
            {"nanoParticleOrigin", dumpJson(s.nanoParticleOrigin)}};
    }
    nlohmann::json dumpJson(const UnitBehaviorStateResurrecting& s)
    {
        return nlohmann::json{
            {"target", dumpJson(s.target)},
            {"nanoParticleOrigin", dumpJson(s.nanoParticleOrigin)}};
    }
    nlohmann::json dumpJson(const UnitBehaviorStateCreatingUnit& s)
    {
        return nlohmann::json{
            {"unitType", dumpJson(s.unitType)},
            {"owner", dumpJson(s.owner)},
            {"position", dumpJson(s.position)},
        };
    }
    nlohmann::json dumpJson(const UnitCreationStatusPending&)
    {
        return nlohmann::json();
    }
    nlohmann::json dumpJson(const UnitCreationStatusDone& s)
    {
        return nlohmann::json{{"unitId", dumpJson(s.unitId)}};
    }
    nlohmann::json dumpJson(const UnitCreationStatusFailed&)
    {
        return nlohmann::json();
    }

    nlohmann::json dumpJson(const UnitState::LifeStateAlive&)
    {
        return nlohmann::json();
    }
    nlohmann::json dumpJson(const UnitState::LifeStateDead&)
    {
        return nlohmann::json();
    }

    nlohmann::json dumpJson(const NavigationGoalLandingLocation&)
    {
        return nlohmann::json();
    }

    nlohmann::json dumpJson(const NavigationStateIdle& m)
    {
        return nlohmann::json();
    }

    nlohmann::json dumpJson(const NavigationStateMoving& m)
    {
        return nlohmann::json{
            {"movementGoal", dumpJson(m.movementGoal)},
            {"pathDestination", dumpJson(m.pathDestination)},
            {"pathRequested", m.pathRequested},
            {"reachableDestination", dumpJson(m.reachableDestination)}};
    }

    nlohmann::json dumpJson(const NavigationStateMovingToLandingSpot& m)
    {
        return nlohmann::json{
            {"landingLocation", dumpJson(m.landingLocation)}};
    }

    nlohmann::json dumpJson(const NavigationStateInfo& m)
    {
        return nlohmann::json{
            {"desiredDestination", dumpJson(m.desiredDestination)},
            {"state", dumpJson(m.state)}};
    }

    nlohmann::json dumpJson(const DiscreteRect& r)
    {
        return nlohmann::json{
            {"x", r.x},
            {"y", r.y},
            {"width", r.width},
            {"height", r.height}};
    }
    nlohmann::json dumpJson(const GameSimulation& simulation)
    {
        return nlohmann::json{
            {"gameTime", dumpJson(simulation.gameTime)},
            {"players", dumpJson(simulation.players)},
            {"units", dumpJson(simulation.units)},
            {"projectiles", dumpJson(simulation.projectiles)},
            {"featureRegrowthCursor", simulation.featureRegrowthCursor},
        };
    }
}
