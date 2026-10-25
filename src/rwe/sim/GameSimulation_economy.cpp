#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MissionRules.h>
#include <rwe/sim/SimRandom.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/util/rwe_string.h>
#include <algorithm>
#include <optional>

namespace rwe
{
    namespace
    {
        /** `push 0xf` before the timer call at 0x4028A2. */
        constexpr unsigned int FactoryPadRetryTicks = 15;

        constexpr unsigned int BlockedSiteRetryTicks = 30;
        constexpr unsigned int BlockedSiteAttempts = 10;
    }

    void GameSimulation::updateResources()
    {
        // The settle is held back for every player while a mission's
        // countdown runs (0x46554F).
        if (missionRules && missionRules->economyFrozen())
        {
            return;
        }

        // And outside a mission, from the first second the win test answers
        // until the end of the game (BT-82, 0x401360 runs only while the word
        // is negative and the latch clear).
        if (!missionRules && isEndCountdownFreezingEconomy())
        {
            return;
        }

        // run resource updates once per second
        if (gameTime % GameTime(SimTicksPerSecond) == GameTime(0))
        {
            // Which players settle (BT-28, the gate in 0x464F80): one this
            // machine simulates, that has units or has never had any. A player
            // that has created units and now has none is out of the game, and
            // its economy stops where it stood.
            std::vector<bool> hasLivingUnit(players.size(), false);
            for (const auto& entry : units)
            {
                if (!entry.second.isDead() && entry.second.owner.value < players.size())
                {
                    hasLivingUnit[entry.second.owner.value] = true;
                }
            }
            std::vector<bool> settles(players.size(), false);
            for (Index i = 0; i < getSize(players); ++i)
            {
                settles[i] = simulatesLocally(*this, PlayerId(i)) && players[i].defeatCountdown < 0
                    && (hasLivingUnit[i] || !players[i].hasCreatedUnits);
            }

            for (Index i = 0; i < getSize(players); ++i)
            {
                if (!settles[i])
                {
                    continue;
                }

                auto& player = players[i];
                player.maxEnergy = player.hasBaseStorage ? player.startingEnergy : Energy(0);
                player.maxMetal = player.hasBaseStorage ? player.startingMetal : Metal(0);
                if (player.hasBaseStorage && player.respawnCount > 0)
                {
                    // 0x496E90: unscaled base storage, at least 200 of each;
                    // the host's starting amounts remain the credit source.
                    player.maxEnergy = Energy(std::max(200.0f, player.startingEnergy.value));
                    player.maxMetal = Metal(std::max(200.0f, player.startingMetal.value));
                }
            }

            for (auto& entry : units)
            {
                auto& unit = entry.second;
                if (unit.owner.value >= players.size() || !settles[unit.owner.value])
                {
                    continue;
                }

                const auto& unitDefinition = unitDefinitions.at(unit.unitType);
                if (!unit.isBeingBuilt(unitDefinition))
                {
                    auto& playerInfo = getPlayer(unit.owner);
                    if (unitDefinition.commander && !playerInfo.hasBaseStorage)
                    {
                        playerInfo.maxMetal += playerInfo.startingMetal;
                        playerInfo.maxEnergy += playerInfo.startingEnergy;
                    }
                    else
                    {
                        playerInfo.maxMetal += unitDefinition.metalStorage;
                        playerInfo.maxEnergy += unitDefinition.energyStorage;
                    }
                }
            }

            // The make-and-use pass comes before the settle, so a generator's
            // output is available to the same second that pays for the work
            // beside it. The order inside a unit matters too: the original
            // decides whether the unit is powered from its energy draw and then
            // gates the metal it makes on that same answer, rather than on last
            // second's.
            for (auto& entry : units)
            {
                const auto& unitId = entry.first;
                auto& unit = entry.second;
                if (unit.owner.value >= players.size() || !settles[unit.owner.value])
                {
                    continue;
                }

                const auto& unitDefinition = unitDefinitions.at(unit.unitType);

                if (unit.activated)
                {
                    // The settle sweep's gate reads the unit's energy owed and
                    // never its metal (0x4013F9, 0x40164F), unlike the request
                    // routine a builder goes through (0x4011C0), which tests
                    // both. So a unit that owes metal but has the energy stays
                    // powered. TOTALA-EXE.md section 111.
                    unit.isSufficientlyPowered = addResourceDelta(unitId, -unitDefinition.energyUse, -unitDefinition.metalUse, ResourceDebtGate::EnergyOnly);

                    if (unit.isSufficientlyPowered)
                    {
                        if (unitDefinition.extractsMetal != Metal(0))
                        {
                            auto footprint = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
                            // The original caches rating * N at creation
                            // (unit+0x58); the grid is constant afterwards in
                            // the reference, so reading it here is the same.
                            // float(double(rating) * N), rounded once.
                            auto work = extractorMetalWork(footprint);
                            addResourceDelta(unitId, Energy(0), Metal(static_cast<float>(static_cast<double>(unitDefinition.extractsMetal.value) * static_cast<double>(work))));
                        }

                        if (unitDefinition.makesMetal != Metal(0))
                        {
                            addResourceDelta(unitId, Energy(0), unitDefinition.makesMetal);
                        }
                    }

                    // Wind and tidal are not gated on being powered: they are
                    // what makes the power.
                    if (unitDefinition.windGenerator != Energy(0))
                    {
                        addResourceDelta(unitId, unitDefinition.windGenerator * currentWindGenerationFactor, Metal(0));
                    }

                    if (unitDefinition.tidalGenerator != Energy(0))
                    {
                        addResourceDelta(unitId, unitDefinition.tidalGenerator * SimScalar(static_cast<float>(tidalStrength)), Metal(0));
                    }
                }

                // Cloak is paid for out of the same second's energy as
                // everything else. It is all or nothing: the original compares
                // the whole cost against the stockpile and, when it does not
                // cover it, takes nothing and leaves the unit visible rather
                // than running the player into a stall.
                if (unitDefinition.cloakable && unit.cloakRequested && !unit.isBeingBuilt(unitDefinition) && gameTime >= unit.cloakSuppressedUntil)
                {
                    // Moving costs the other number. The original reads the
                    // unit's move-rate band, which is zero exactly when it is
                    // standing still, so this is the same test the COB
                    // StartMoving and StopMoving callbacks use.
                    auto moving = !areCloserThan(unit.previousPosition, unit.position, 0.1_ssf);
                    // Truncated to a whole number first, which is what the
                    // original compares and takes (0x4017CB, 0x40182F): a
                    // cost of 200.7 against a stock of 200.5 cloaks and costs
                    // 200.
                    auto cost = Energy(std::trunc((moving ? unitDefinition.cloakCostMoving : unitDefinition.cloakCost).value));

                    // Cloak does not go through the ordinary request-and-settle
                    // path, which pays every consumer a share of whatever there
                    // is and carries the rest as debt. The original checks the
                    // whole cost against the stock on the spot and, when it
                    // will not cover it, takes nothing and leaves the unit
                    // visible. Half a cloak is not a thing, and a unit that
                    // cannot afford one should not be dragging the player into
                    // debt for it either; nor does owing for something else
                    // stop a unit that can pay from cloaking.
                    unit.cloaked = chargeStockpile(unitId, cost, Metal(0));
                }
                else
                {
                    unit.cloaked = false;
                }
                if (!unit.isBeingBuilt(unitDefinition))
                {
                    addResourceDelta(unitId, unitDefinition.energyMake, unitDefinition.metalMake);
                }
            }

            // Now settle. Everything asked for this second is pooled, and one
            // fraction per resource decides what share of it every consumer
            // gets, so a shortfall slows the whole player down evenly instead of
            // starving whoever happens to be last in the list. Debt already owed
            // is paid before anything new.
            for (Index i = 0; i < getSize(players); ++i)
            {
                if (!settles[i])
                {
                    continue;
                }

                auto& player = players[i];
                // Brutal computer players get a little extra for every unit of income.
                auto bonus = resourceBonusFor(PlayerId(i));

                // Pool the player's own block with every unit it owns, in that
                // order, which is the order the original adds them up in. The
                // sum sizes the fractions; each unit rolls its own debt against
                // them afterwards.
                auto metalDebt = player.metalDebt;
                auto energyDebt = player.energyDebt;
                auto metalRequested = player.metalRequestBuffer;
                auto energyRequested = player.energyRequestBuffer;
                for (const auto& entry : units)
                {
                    const auto& unit = entry.second;
                    if (!unit.isOwnedBy(PlayerId(i)))
                    {
                        continue;
                    }
                    metalDebt += unit.metalDebt;
                    energyDebt += unit.energyDebt;
                    metalRequested += unit.metalRequestBuffer;
                    energyRequested += unit.energyRequestBuffer;
                }

                ResourceAccount metalAccount{
                    player.metal.value,
                    player.metalProductionBuffer.value,
                    player.metalProduced.value,
                    player.metalExcess.value,
                    player.maxMetal.value,
                    player.metalDebt.value,
                    player.metalRequestBuffer.value,
                    metalDebt.value,
                    metalRequested.value,
                };
                ResourceAccount energyAccount{
                    player.energy.value,
                    player.energyProductionBuffer.value,
                    player.energyProduced.value,
                    player.energyExcess.value,
                    player.maxEnergy.value,
                    player.energyDebt.value,
                    player.energyRequestBuffer.value,
                    energyDebt.value,
                    energyRequested.value,
                };

                auto metalSettlement = settleResourceAccount(metalAccount, bonus);
                auto energySettlement = settleResourceAccount(energyAccount, bonus);

                player.metal = Metal(metalSettlement.stockpile);
                player.energy = Energy(energySettlement.stockpile);
                player.metalStalled = metalSettlement.stalled;
                player.energyStalled = energySettlement.stalled;

                // The chart's "produced" and "excess" columns: income counted
                // as it arrives and after the difficulty bonus, and what the
                // cap threw away at the end of the second.
                player.metalProduced = Metal(metalSettlement.lifetimeProduced);
                player.energyProduced = Energy(energySettlement.lifetimeProduced);
                player.metalExcess = Metal(metalSettlement.lifetimeExcess);
                player.energyExcess = Energy(energySettlement.lifetimeExcess);

                for (auto& entry : units)
                {
                    auto& unit = entry.second;
                    if (!unit.isOwnedBy(PlayerId(i)))
                    {
                        continue;
                    }
                    unit.settleResources(
                        energySettlement.requestFraction,
                        energySettlement.debtFraction,
                        metalSettlement.requestFraction,
                        metalSettlement.debtFraction);
                }

                player.metalDebt = Metal(metalSettlement.debt);
                player.energyDebt = Energy(energySettlement.debt);
                player.metalRequestBuffer = Metal(0);
                player.energyRequestBuffer = Energy(0);

                // The display copies are this second's income after the
                // handicap, which is the figure the chart was given.
                player.previousMetalProductionBuffer = Metal(metalSettlement.produced);
                player.previousEnergyProductionBuffer = Energy(energySettlement.produced);
                player.metalProductionBuffer = Metal(0);
                player.energyProductionBuffer = Energy(0);

                player.previousDesiredMetalConsumptionBuffer = player.desiredMetalConsumptionBuffer;
                player.previousDesiredEnergyConsumptionBuffer = player.desiredEnergyConsumptionBuffer;
                player.desiredMetalConsumptionBuffer = Metal(0);
                player.desiredEnergyConsumptionBuffer = Energy(0);
            }
        }
    }

    struct CorpseSpawnInfo
    {
        std::string featureName;
        SimVector position;
        SimAngle rotation;
        /** The dead unit's `isfeature`: its wreck stays where it fell. */
        bool isFeature;
    };

    /**
     * A wreck sinks at a fixed 0.175 world units a tick -- 5.25 a second --
     * rather than accelerating: 0x486416 writes the constant -11468 in 16.16
     * straight into the feature's velocity, and 0x42428C re-asserts it every
     * tick the wreck is under the surface. Deep water on the shipped naval
     * maps is 55 to 85 units, so an open-ocean wreck takes ten to sixteen
     * seconds to reach the bottom; in shallows it is down in under half a
     * second.
     */
    static const SimScalar WreckSinkSpeed(0.174988f);

    void GameSimulation::trySpawnFeature(const std::string& featureType, const SimVector& position, SimAngle rotation, bool isFeature)
    {
        auto featureId = tryGetFeatureDefinitionId(featureType);
        if (!featureId)
        {
            // A unit whose corpse feature is missing from the game data simply
            // leaves no wreck; that is not worth crashing over.
            return;
        }
        auto feature = MapFeature{*featureId, position, rotation};

        // The wreck is placed at the height the unit died at, on land and at
        // sea alike -- there is no clamp anywhere in the original's corpse
        // spawner. What decides whether it sinks is the ground BENEATH it,
        // not its own height, and the test is against the terrain rather than
        // the water's surface, so ground exactly at sea level counts as wet
        // (0x4863E9 jumps on greater-than only).
        //
        // A unit flagged `isfeature` is exempt. Six units set it; the two that
        // matter are the floating dragon's teeth, whose wreck is meant to stay
        // on the surface -- the original carving out exactly the one thing
        // built to float is the clearest evidence the sink rule is real.
        auto ground = terrain.getHeightAt(position.x, position.z);
        auto wet = ground <= terrain.getSeaLevel() && !isFeature;
        if (wet)
        {
            feature.velocity = SimVector(0_ss, -WreckSinkSpeed, 0_ss);
        }

        addFeature(std::move(feature));

        // The same test decides the plume: the wet branch zeroes mayBurn
        // (0x486420) and cause 7 clears it for an isfeature unit, and the
        // scene lights the thirty-second plume off whatever is left. The
        // original fires it even when the placement itself was refused (the
        // flag is cleared inside the feature != null test), which is why this
        // does not wait on addFeature succeeding.
        events.push_back(WreckSpawnedEvent{position, !wet && !isFeature});
    }

    void GameSimulation::updateFallingFeatures()
    {
        // Only wreckage dropped into water is ever moving, so the common case
        // is one comparison per feature. The original retires a feature from
        // its active list the moment the velocity reaches zero and never looks
        // at it again (0x42421A); this is the same, without the list.
        for (auto& [featureId, feature] : features)
        {
            if (feature.velocity == SimVector(0_ss, 0_ss, 0_ss))
            {
                continue;
            }

            feature.position += feature.velocity;

            auto ground = terrain.getHeightAt(feature.position.x, feature.position.z);
            if (feature.position.y <= ground)
            {
                // Landed. 0x424262.
                feature.position.y = ground;
                feature.velocity = SimVector(0_ss, 0_ss, 0_ss);
            }
            else if (feature.position.y < terrain.getSeaLevel())
            {
                // Under the surface: a terminal speed re-asserted every tick,
                // with any sideways drift killed. 0x42428C-0x424299.
                feature.velocity = SimVector(0_ss, -WreckSinkSpeed, 0_ss);
            }
            else
            {
                // Still in the air above the water: the map's gravity.
                // 0x4242A5.
                feature.velocity.y -= gravityPerTick();
            }
        }
    }

    void GameSimulation::deleteDeadUnits()
    {
        std::vector<CorpseSpawnInfo> corpsesToSpawn;

        for (auto it = units.begin(); it != units.end();)
        {
            const auto& unit = it->second;
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            auto deadState = std::get_if<UnitState::LifeStateDead>(&unit.lifeState);
            if (deadState == nullptr)
            {
                ++it;
                continue;
            }

            if (deadState->leaveCorpse && !unitDefinition.corpse.empty())
            {
                // Walk one step down the featuredead chain for each level
                // above the first (0x4863A7). Running off the end is not an
                // error: it is how a hard enough death leaves nothing.
                auto corpse = tryGetFeatureDefinitionId(unitDefinition.corpse);
                for (unsigned int level = 1; corpse && level < deadState->corpseLevel; ++level)
                {
                    corpse = getFeatureDefinition(*corpse).featureDead;
                }

                if (corpse)
                {
                    corpsesToSpawn.push_back(CorpseSpawnInfo{
                        getFeatureDefinition(*corpse).name,
                        unit.position,
                        unit.rotation,
                        unitDefinition.isFeature});
                }
            }

            auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            auto footprintRegion = occupiedGrid.tryToRegion(footprintRect);

            // A unit that died off the map holds no ground to give back. That
            // is only ever an aircraft -- nothing on the ground can leave --
            // and it is a real case rather than a defensive one: an attack
            // run carries an edge check for the same reason, and a dogfight
            // breaks two weapon ranges out, which off a corner is over the
            // edge. A unit that died in a transport's grip holds no ground
            // either, and its last position can be anywhere the carrier was.
            // The assertion stands where it is still an invariant.
            assert(!!footprintRegion || isFlying(unit.physics) || unit.carriedBy);

            // Out of the flying set first, and whatever else is true of it:
            // the projectile pass walks that set and asks for each unit by
            // id, so an entry left behind by a dead aircraft is a lookup for
            // a unit that is not there any more.
            if (unitDefinition.isMobile && isFlying(unit.physics))
            {
                flyingUnitsSet.erase(it->first);
            }

            if (unit.carriedBy || !footprintRegion)
            {
                // It died in a transport's grip, or off the map entirely:
                // either way it is holding no ground to give back.
            }
            else if (unitDefinition.isMobile)
            {
                if (!isFlying(unit.physics))
                {
                    occupiedGrid.forEach(*footprintRegion, [](auto& cell) { cell.mobileUnitId = std::nullopt; });
                }
            }
            else
            {
                occupiedGrid.forEach(*footprintRegion, [&](auto& cell) {
                  if (cell.buildingInfo && cell.buildingInfo->unit == it->first)
                  {
                      cell.buildingInfo = std::nullopt;
                  } });
            }

            // Removing a unit frees its slot for the next one to be built,
            // so an index that still names it could hand a search an id that
            // now belongs to somebody else.
            invalidateUnitSpatialIndex();

            // The recorder has to hear about the removal before the slot is
            // reused, and it is reused inside this same tick: spawnNewUnits
            // runs a few phases further down. A recorder that only noticed at
            // the end of the tick would still name the old unit for a new one
            // standing in its id.
            if (demoRecorder)
            {
                demoRecorder->unitRemoved(it->first);
            }

            if (taLiveSender.attached())
            {
                taLiveSender.unitRemoved(taLiveSender.context, it->first);
            }

            it = units.erase(it);
        }

        for (const auto& spawnInfo : corpsesToSpawn)
        {
            trySpawnFeature(spawnInfo.featureName, spawnInfo.position, spawnInfo.rotation, spawnInfo.isFeature);
        }
    }

    void GameSimulation::deleteDeadProjectiles()
    {
        for (auto it = projectiles.begin(); it != projectiles.end();)
        {
            const auto& projectile = it->second;
            if (projectile.isDead)
            {
                it = projectiles.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::optional<std::string> GameSimulation::resurrectedUnitType(const std::string& featureName) const
    {
        auto underscore = featureName.find('_');
        if (underscore == std::string::npos || underscore == 0)
        {
            return std::nullopt;
        }

        auto candidate = toUpper(featureName.substr(0, underscore));
        if (unitDefinitions.find(candidate) == unitDefinitions.end())
        {
            return std::nullopt;
        }

        return candidate;
    }

    UnitCreationStatus GameSimulation::retryBlockedSite(UnitId unitId, const UnitCreationStatusPending& pending)
    {
        // Said once, at the front of the run of tries, and once more when the
        // tries run out. Between the two the builder simply waits: the
        // original's site check refuses quietly on the intervening attempts.
        if (pending.attempts == 0)
        {
            events.push_back(UnitCannotComplyEvent{unitId, "Waiting for target area to clear"});
        }

        auto attempts = pending.attempts + 1;
        if (attempts >= BlockedSiteAttempts)
        {
            events.push_back(UnitCannotComplyEvent{unitId, "Target area was blocked"});
            return UnitCreationStatusFailed();
        }

        return UnitCreationStatusPending{attempts, gameTime + GameTime(BlockedSiteRetryTicks)};
    }

    void GameSimulation::spawnNewUnits()
    {
        for (const auto& unitId : unitCreationRequests)
        {
            auto unit = tryGetUnitState(unitId);
            if (!unit)
            {
                continue;
            }

            if (auto s = std::get_if<UnitBehaviorStateCreatingUnit>(&unit->get().behaviourState); s != nullptr)
            {

                if (!std::holds_alternative<UnitCreationStatusPending>(s->status))
                {
                    continue;
                }

                // Waiting out the gap between two tries at a blocked site.
                if (gameTime < std::get<UnitCreationStatusPending>(s->status).nextAttempt)
                {
                    continue;
                }

                // A building goes up somewhere inside the arc its own FBI
                // names, so a base looks placed by hand rather than stamped
                // out on a grid. The arc is per-unit and ranges over a factor
                // of thirty-two, from a vehicle plant that barely moves to a
                // light laser tower that can end up facing any quarter, and
                // the yards and aircraft plants name none at all, which is
                // what keeps their roll-off square. The original spreads the
                // draw uniformly over the whole arc and offsets it by half,
                // and it leaves the facing alone for an arc narrower than
                // two, which is why the fortification walls that ask for
                // zero come out in a dead straight line.
                std::optional<SimAngle> spawnRotation;
                const auto& newUnitDefinition = unitDefinitions.at(s->unitType);
                if (!newUnitDefinition.isMobile && newUnitDefinition.buildAngle.value >= 2)
                {
                    auto twist = randomBelow(rng, newUnitDefinition.buildAngle.value);
                    spawnRotation = SimAngle(static_cast<uint16_t>(twist)) - SimAngle(newUnitDefinition.buildAngle.value / 2);
                }

                auto newUnitId = trySpawnUnit(s->unitType, s->owner, s->position, spawnRotation);
                if (!newUnitId)
                {
                    // Occupied. Wait for it to clear and say so, rather than
                    // dropping the order on the spot with only a log line --
                    // which is what this did, and why a builder whose site was
                    // briefly straddled by a passing unit silently gave up.
                    // The site is the new unit's footprint, not the builder's
                    // own: a mobile unit parked where the frame is to go is
                    // outside the builder's cells and would never hear about
                    // it otherwise.
                    emitBuggerOff(computeFootprintRegion(s->position, newUnitDefinition.movementCollisionInfo));
                    s->status = retryBlockedSite(unitId, std::get<UnitCreationStatusPending>(s->status));
                    continue;
                }

                if (demoRecorder)
                {
                    demoRecorder->buildStarted(*this, unitId, *newUnitId);
                }

                if (taLiveSender.attached())
                {
                    taLiveSender.buildStarted(taLiveSender.context, *this, unitId, *newUnitId);
                }

                events.push_back(UnitStartedBuildingEvent{unitId});

                s->status = UnitCreationStatusDone{*newUnitId};
            }

            if (auto s = std::get_if<FactoryBehaviorStateCreatingUnit>(&unit->get().factoryState); s != nullptr)
            {
                if (!std::holds_alternative<UnitCreationStatusPending>(s->status))
                {
                    continue;
                }

                // As above: the yard waits out the gap between tries.
                if (gameTime < std::get<UnitCreationStatusPending>(s->status).nextAttempt)
                {
                    continue;
                }

                auto newUnitId = trySpawnUnit(s->unitType, s->owner, s->position, s->rotation);
                if (!newUnitId)
                {
                    // The original's yard (BuildingBuild, 0x402899) has none of
                    // the constructors' ladder: no caption, no count, no giving
                    // up. It looks again every fifteen ticks until the pad is
                    // clear, which is normally the unit it just built rolling
                    // off to its move location. The sweep is RWE's
                    // (TOTALA-EXE.md 112): a hull parked on the pad is nobody's
                    // order, and would otherwise stall the yard for good.
                    const auto& blockedDefinition = unitDefinitions.at(s->unitType);
                    emitBuggerOff(computeFootprintRegion(s->position, blockedDefinition.movementCollisionInfo));
                    s->status = UnitCreationStatusPending{0, gameTime + GameTime(FactoryPadRetryTicks)};
                    continue;
                }

                // TOTALA-EXE-INTERFACE S:133: posture is stamped before its first tick.
                auto& product = getUnitState(*newUnitId);
                product.moveOrders = unit->get().moveOrders;
                product.fireOrders = unit->get().fireOrders;

                // An unfinished product is attached to the yard's build piece
                // and follows it in height (the yard's building pass does so
                // from the next tick on), so its first appearance must not be
                // the seabed trySpawnUnit settled it on: a submarine would be
                // drawn on the bottom for a tick or two and then jump to the
                // pad. A floater is held to sea level minus its WaterLine, as
                // the building pass does; hovers keep the sea-level floor
                // trySpawnUnit gave them. Aircraft were never settled.
                // NAVAL-CONSTRUCTION-DGUN-FINDINGS.md, "submarine under
                // construction"; NAVAL-SYSTEMS-PARITY.md section 1.
                {
                    const auto& productDefinition = unitDefinitions.at(s->unitType);
                    if (productDefinition.isMobile && !productDefinition.canFly && !productDefinition.canHover)
                    {
                        auto attachedHeight = s->position.y;
                        if (productDefinition.floater)
                        {
                            attachedHeight = rweMax(attachedHeight, terrain.getSeaLevel() - intToSimScalar(static_cast<int>(productDefinition.waterLine)));
                        }
                        product.position.y = attachedHeight;
                        product.previousPosition.y = attachedHeight;
                    }
                }

                // A factory's frame is a build start like a builder's: the
                // 0x09 names the frame and the 0x12 names the yard that
                // finished it.
                if (demoRecorder)
                {
                    demoRecorder->buildStarted(*this, unitId, *newUnitId);
                }

                if (taLiveSender.attached())
                {
                    taLiveSender.buildStarted(taLiveSender.context, *this, unitId, *newUnitId);
                }

                s->status = UnitCreationStatusDone{*newUnitId};
            }

            // A resurrection asks for its unit from here for the same reason
            // the two above do: the behaviour pass that finished the job was
            // iterating `units`, and creating one appends to the deque behind
            // it. Unlike those two the corpse is still standing, so the work
            // the handler used to do inline happens here instead -- the type
            // off the corpse's name, the position and facing off the corpse,
            // then the corpse, then the unit. The corpse has to go first: it
            // is `blocking`, and a unit will not be placed on an occupied
            // footprint.
            if (auto s = std::get_if<UnitBehaviorStateResurrecting>(&unit->get().behaviourState); s != nullptr)
            {
                auto featureRef = tryGetFeature(s->target);
                if (!featureRef)
                {
                    continue;
                }

                const auto& feature = featureRef->get();
                auto unitType = resurrectedUnitType(getFeatureDefinition(feature.featureName).name);
                if (!unitType)
                {
                    continue;
                }

                auto position = feature.position;
                auto rotation = feature.rotation;
                auto owner = unit->get().owner;
                deleteFeature(s->target);

                auto newUnitId = trySpawnUnit(*unitType, owner, position, rotation);
                if (!newUnitId)
                {
                    // The original prints "Unable to create any more units"
                    // and tries again in 300 ticks, but it still has its
                    // corpse to try with. Ours is spent, so the job ends and
                    // the order is dropped on the next tick, when the handler
                    // finds no feature.
                    continue;
                }

                // 0x405219/0x405226: complete rather than a nanoframe, and on
                // exactly one hit point -- it has to be repaired afterwards or
                // a stiff breeze finishes it.
                auto& newUnit = getUnitState(*newUnitId);
                const auto& newUnitDefinition = unitDefinitions.at(newUnit.unitType);
                newUnit.buildTimeCompleted = newUnitDefinition.buildTime;
                newUnit.hitPoints = 1;

                // BT-93 (state 6 of 0x404DB0, 0x405243): the resurrector goes
                // straight on to mend the unit it raised. The mission asks the
                // order builder for command 8 on the new unit; that arm needs
                // the orderer's repair capability (canreclamate, 0x4899B0) and
                // a target whose health differs from its maximum, and for a
                // finished unit yields REPAIRUNIT. The node goes in at the head
                // of the list and the resurrect mission ends, so its order is
                // dropped here as well rather than left to find no corpse and
                // complain.
                // The order work sits in UnitBehaviorService, not here: a deque
                // of the order variant instantiated in this file costs most of
                // its section budget (CLAUDE.md, the COFF ceiling).
                UnitBehaviorService(this).mendRaisedUnit(unitId, s->target, *newUnitId);
            }
        }

        unitCreationRequests.clear();
    }

    void GameSimulation::updateSelfRepair()
    {
        // 0x48AF3D runs only when the game tick is a multiple of eight
        // (`test BYTE PTR [..],0x7`), and then it does not add hit points: it
        // works out `work = healtime * 8 / 30` (truncated, then converted to a
        // float) and hands it to the shared repair tick 0x41BD10 with the unit
        // as both repairer and patient. That routine is the one every
        // repairer goes through, and it throws the number away -- hit points
        // and energy are each clamped to at most one (0x41BD87, 0x41BD97) --
        // so a unit with any healtime at all mends ONE point every eight
        // ticks, 3.75 a second, and pays one energy for it. A commander's
        // shipped 27 makes `work` 7 and changes nothing about that. RWE
        // used to add `work` itself, 26.25 a second, seven times too fast
        // (play-test 2026-10-10: "commander health regen too fast?").
        if (gameTime.value % 8u != 0u)
        {
            return;
        }

        for (auto& entry : units)
        {
            auto& unit = entry.second;
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            if (!simulatesLocally(*this, unit.owner))
            {
                continue;
            }

            if (unitDefinition.healTime == 0 || unit.isDead() || unit.isBeingBuilt(unitDefinition))
            {
                continue;
            }

            if (unit.hitPoints >= unitDefinition.maxHitPoints)
            {
                continue;
            }

            // work == 0 (a healtime under four) gives hp == 0 in the
            // original's expression, and anything from one up reaches the
            // clamp of one.
            auto work = (unitDefinition.healTime * 8u) / 30u;
            if (work == 0u)
            {
                continue;
            }

            // 0x41BDB7 asks the unit's own economy block for the energy and
            // 0x41BDBC mends nothing if it is turned down.
            auto energy = Energy(unitDefinition.buildCostEnergy.value * static_cast<float>(work) >= 1.0f ? 1.0f : 0.0f);
            if (!addEnergyRequest(entry.first, energy))
            {
                continue;
            }

            unit.hitPoints = std::min(unitDefinition.maxHitPoints, unit.hitPoints + 1u);
        }
    }

#ifdef RWE_ENABLE_SIMPROF
    namespace
    {
        // Temporary: per-phase tick timing, reported every two seconds.
        // The slots live in sim_prof.h so the behaviour pass can add its own
        // without a second reporting mechanism; they are zeroed rather than
        // erased so a reference held at a call site stays good.
        std::chrono::steady_clock::time_point profLastReport = std::chrono::steady_clock::now();
        int profTicks = 0;

        void profReport()
        {
            ++profTicks;
            auto now = std::chrono::steady_clock::now();
            auto span = std::chrono::duration<double, std::milli>(now - profLastReport).count();
            if (span < 2000.0)
            {
                return;
            }
            std::string line;
            for (auto& [name, total] : simProfTotals)
            {
                line += " " + name + "=" + std::to_string(static_cast<int>(total / (profTicks == 0 ? 1 : profTicks) * 1000.0)) + "us";
                total = 0.0;
            }
            LOG_INFO << "SIMPROF ticks=" << profTicks << line;
            profTicks = 0;
            profLastReport = now;
        }
    }
#endif
}
