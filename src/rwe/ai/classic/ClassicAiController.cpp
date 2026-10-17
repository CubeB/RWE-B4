#include "ClassicAiController.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/ai/classic/ClassicAiGroups.h>
#include <rwe/ai/classic/ClassicAiPlacement.h>
#include <rwe/ai/classic/ClassicAiScoring.h>
#include <rwe/ai/classic/ClassicAiScript.h>
#include <rwe/ai/classic/ClassicRandom.h>
#include <rwe/math/det_math.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>
#include <set>
#include <utility>
#include <variant>

namespace rwe
{
    namespace
    {
        // TOTALA-EXE-COMPUTER-PLAYER.md S:119, the timer table.
        constexpr std::size_t SlotCount = 10;
        constexpr std::size_t SlotBuild = 1;
        constexpr std::size_t SlotLandAssault = 2;
        constexpr std::size_t SlotLandReserve = 3;
        constexpr std::size_t SlotScout = 4;
        constexpr std::size_t SlotBase = 5;
        constexpr std::size_t SlotWaterAssault = 6;
        constexpr std::size_t SlotWaterReserve = 7;
        constexpr std::size_t SlotAir = 8;
        constexpr std::size_t SlotProbe = 9;

        constexpr unsigned int ClassifyInterval = 30;
        constexpr unsigned int RefreshInterval = 30;

        // S:122: the cohesion limits, land and water.
        constexpr double LandCohesionLimit = 20000.0;
        constexpr double WaterCohesionLimit = 50000.0;

        // S:122: an assault's defaults, minimum 3 and maximum 6.
        constexpr std::size_t AssaultMinimum = 3;
        constexpr std::size_t AssaultMaximum = 6;

        // S:121: a builder that cannot capture is kept near the base by the
        // "about 320" patrol; the capturing ones wander 640 from it.
        constexpr int PatrolClose = 320;
        constexpr int WanderLimit = 640;

        // S:121: at least this many completed builders holds a cancapture
        // builder back from starting work.
        constexpr int BuilderCap = 5;

        double toDouble(SimScalar s)
        {
            return static_cast<double>(simScalarToFloat(s));
        }

        SimScalar toScalar(double d)
        {
            return SimScalar(static_cast<float>(d));
        }

        std::string upperCopy(const std::string& s)
        {
            std::string out = s;
            for (auto& c : out)
            {
                if (c >= 'a' && c <= 'z')
                {
                    c = static_cast<char>(c - 'a' + 'A');
                }
            }
            return out;
        }

        bool isArmed(const UnitDefinition& def)
        {
            // [open] "armed" is any named weapon slot; the register's flag is
            // 0x80000000 and RWE has no such flag.
            return !def.weapon1.empty() || !def.weapon2.empty() || !def.weapon3.empty();
        }

        /**
         * The original's "metadata bit 8" on a unit's current order: the
         * orders that are work in hand and are not replaced by a fresh pick.
         * A move or a patrol is replaceable; a build, a repair, an assist or a
         * reclaim is not. [open] RWE has no such bit, so this is the set of
         * order kinds that are work.
         */
        bool isWorkOrder(const UnitOrder& order)
        {
            return std::holds_alternative<BuildOrder>(order) || std::holds_alternative<CompleteBuildOrder>(order)
                || std::holds_alternative<RepairOrder>(order) || std::holds_alternative<GuardOrder>(order)
                || std::holds_alternative<ReclaimOrder>(order) || std::holds_alternative<ResurrectOrder>(order)
                || std::holds_alternative<CaptureOrder>(order);
        }

        PlayerCommand issue(UnitId unit, const UnitOrder& order, PlayerUnitCommand::IssueOrder::IssueKind kind)
        {
            return PlayerUnitCommand(unit, PlayerUnitCommand::IssueOrder(order, kind));
        }
    }

    struct ClassicAiController::Impl
    {
        Impl(PlayerId self, ClassicAiConfig config, std::uint64_t seed)
            : self(self), config(std::move(config)), rng(ClassicRandom::fromSeed(seed))
        {
            for (auto& t : nextTime)
            {
                t = 0u;
            }
        }

        PlayerId self;
        ClassicAiConfig config;
        ClassicRandom rng;

        bool ready{false};
        unsigned int calls{0};

        // The type table, built once on the first tick from the simulation's
        // unit definitions in name order, so every index means the same on
        // every machine.
        std::vector<std::string> typeNames;
        std::map<std::string, std::size_t> typeIndex;
        std::vector<ClassicTypeFacts> facts;
        std::vector<ClassicClassifyFacts> classifyFacts;
        std::vector<int> typeValues;
        std::vector<ClassicTypeEntry> entries;
        std::vector<ClassicRatings> ratings;
        std::vector<std::string> typeSides;
        std::vector<bool> excluded;
        std::map<std::size_t, std::vector<std::size_t>> menuCache;
        bool lowWind{false};
        std::string playerSide;

        // What the last strategy refresh counted.
        std::vector<int> completedCounts;
        int completedTotal{0};
        int completedBuilders{0};
        double baseX{0.0};
        double baseZ{0.0};

        // Timers: each slot's absolute next time.
        unsigned int nextTime[SlotCount];
        unsigned int nextRefresh{0};

        // The retaliation throttle, and what it watches.
        unsigned int nextAction{0};
        std::map<unsigned int, unsigned int> lastHitPoints;

        ClassicGroups groups;
        std::map<unsigned int, std::string> groupedType;
        bool landAttacking{false};
        bool waterAttacking{false};

        ClassicPlacement placement;

        // ---- setup -------------------------------------------------------

        void initialise(const GameSimulation& sim)
        {
            ready = true;

            std::vector<std::string> names;
            names.reserve(sim.unitDefinitions.size());
            for (const auto& [name, def] : sim.unitDefinitions)
            {
                (void)def;
                names.push_back(name);
            }
            std::sort(names.begin(), names.end());
            typeNames = std::move(names);

            std::set<std::string> knownSides{"ARM", "CORE"};
            for (const auto& p : sim.players)
            {
                knownSides.insert(upperCopy(p.side));
            }
            playerSide = upperCopy(sim.getPlayer(self).side);

            std::vector<ClassicScriptType> scriptTypes;
            for (std::size_t i = 0; i < typeNames.size(); ++i)
            {
                const auto& name = typeNames[i];
                const auto& def = sim.unitDefinitions.at(name);
                typeIndex[name] = i;

                ClassicTypeFacts f;
                const auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);
                f.canAttack = def.canAttack;
                f.builder = def.builder;
                f.mobile = def.isMobile;
                f.canFly = def.canFly;
                f.canLoad = def.canLoad;
                f.isFeature = def.isFeature;
                f.extractor = def.extractsMetal != Metal(0.0f);
                f.converter = def.makesMetal != Metal(0.0f);
                f.sonar = def.sonarDistance > 0;
                f.radar = def.radarDistance > 0;
                f.windGenerator = def.windGenerator != Energy(0.0f);
                f.waterDepthWord = mc.signedMinWaterDepth() >= 0;
                f.metalCost = static_cast<double>(def.buildCostMetal.value);
                f.energyCost = static_cast<double>(def.buildCostEnergy.value);
                f.energyMake = static_cast<double>(def.energyMake.value);
                f.energyUse = static_cast<double>(def.energyUse.value);
                facts.push_back(f);

                ClassicClassifyFacts c;
                c.building = !def.isMobile;
                c.armed = isArmed(def);
                c.builder = def.builder;
                c.canFly = def.canFly;
                c.waterDepthPositive = mc.signedMinWaterDepth() > 0;
                classifyFacts.push_back(c);

                std::vector<ClassicWeaponFacts> weapons;
                for (const auto* weaponName : {&def.weapon1, &def.weapon2, &def.weapon3})
                {
                    if (weaponName->empty())
                    {
                        continue;
                    }
                    ClassicWeaponFacts w;
                    auto it = sim.weaponDefinitions.find(*weaponName);
                    if (it == sim.weaponDefinitions.end())
                    {
                        it = sim.weaponDefinitions.find(upperCopy(*weaponName));
                    }
                    if (it != sim.weaponDefinitions.end())
                    {
                        auto d = it->second.damage.find("DEFAULT");
                        w.damage = d == it->second.damage.end() ? 0u : d->second;
                        w.range = toDouble(it->second.maxRange);
                    }
                    weapons.push_back(w);
                }
                typeValues.push_back(classicTypeValue(f, classicWeaponScore(def.canAttack, weapons)));

                // The side the type belongs to: the first of its category
                // tokens that names a side in the game.
                std::string side;
                {
                    auto category = upperCopy(def.category);
                    std::size_t pos = 0;
                    while (pos < category.size() && side.empty())
                    {
                        while (pos < category.size() && category[pos] == ' ')
                        {
                            ++pos;
                        }
                        auto end = category.find(' ', pos);
                        if (end == std::string::npos)
                        {
                            end = category.size();
                        }
                        auto token = category.substr(pos, end - pos);
                        if (!token.empty() && knownSides.count(token) != 0)
                        {
                            side = token;
                        }
                        pos = end;
                    }
                }
                typeSides.push_back(side);
                excluded.push_back(def.excludedByMission);

                scriptTypes.push_back(ClassicScriptType{upperCopy(name), upperCopy(def.category)});
            }

            // The profile is applied once, now that the definitions are here.
            entries.assign(typeNames.size(), ClassicTypeEntry{});
            ClassicPlan plan = ClassicPlan::Medium;
            switch (config.difficulty)
            {
                case AiDifficulty::Easy:
                    plan = ClassicPlan::Easy;
                    break;
                case AiDifficulty::Standard:
                    plan = ClassicPlan::Medium;
                    break;
                default:
                    plan = ClassicPlan::Hard;
                    break;
            }
            auto report = applyClassicScript(parseClassicScript(config.profileText), plan, scriptTypes, entries);
            for (const auto& unknown : report.unknownNames)
            {
                LOG_WARN << "Classic AI profile names '" << unknown << "', which is neither a unit type nor a category here";
            }

            // [open] The wind test: the map's mean wind speed under a quarter
            // of the most a generator can use. The register says only "the
            // code's low-wind test".
            lowWind = ((sim.minWindSpeed + sim.maxWindSpeed) / 2) < (MaxUtilizableWindSpeed / 4);

            placement.initialise(sim, rng);

            ratings.assign(typeNames.size(), ClassicRatings{});
            completedCounts.assign(typeNames.size(), 0);
            refreshLists(sim);
            recomputeRatings();
            nextRefresh = sim.gameTime.value + RefreshInterval;
        }

        // ---- the strategy table -------------------------------------------

        bool isCompleted(const GameSimulation& sim, const UnitState& unit, const UnitDefinition*& defOut) const
        {
            auto it = sim.unitDefinitions.find(unit.unitType);
            if (it == sim.unitDefinitions.end())
            {
                defOut = nullptr;
                return false;
            }
            defOut = &it->second;
            return !unit.isBeingBuilt(it->second);
        }

        void refreshLists(const GameSimulation& sim)
        {
            std::fill(completedCounts.begin(), completedCounts.end(), 0);
            completedTotal = 0;
            completedBuilders = 0;
            double sumW = 0.0;
            double sumX = 0.0;
            double sumZ = 0.0;
            for (const auto& [unitId, unit] : sim.units)
            {
                (void)unitId;
                if (unit.owner != self || !unit.isAlive())
                {
                    continue;
                }
                const UnitDefinition* def = nullptr;
                if (!isCompleted(sim, unit, def))
                {
                    continue;
                }
                auto idx = typeIndex.find(unit.unitType);
                if (idx != typeIndex.end())
                {
                    ++completedCounts[idx->second];
                }
                ++completedTotal;
                if (def->builder)
                {
                    ++completedBuilders;
                }
                // S:119 "The base position": weight 1, plus 40 if it is not
                // mobile, plus 20 more if it has a build menu.
                double w = 1.0 + (def->isMobile ? 0.0 : 40.0) + (def->builder ? 20.0 : 0.0);
                sumW += w;
                sumX += w * toDouble(unit.position.x);
                sumZ += w * toDouble(unit.position.z);
            }
            if (sumW > 0.0)
            {
                baseX = sumX / sumW;
                baseZ = sumZ / sumW;
            }
        }

        void recomputeRatings()
        {
            for (std::size_t i = 0; i < typeNames.size(); ++i)
            {
                ClassicRatingContext context;
                context.completedOfType = completedCounts[i];
                context.completedTotal = completedTotal;
                context.unitLimit = static_cast<int>(config.unitLimit);
                context.lowWind = lowWind;
                ratings[i] = classicRatings(facts[i], typeValues[i], context);
            }
        }

        void strategyUpdate(const GameSimulation& sim)
        {
            if (sim.gameTime.value < nextRefresh)
            {
                return;
            }
            nextRefresh = sim.gameTime.value + RefreshInterval;
            refreshLists(sim);

            // "at each refresh it also draws rand(30) == 0 and, on a hit,
            // recomputes the per-type scores".
            if (rng.below(30) == 0)
            {
                recomputeRatings();
            }
        }

        ClassicEconomy economyOf(const GameSimulation& sim) const
        {
            const auto& p = sim.getPlayer(self);
            ClassicEconomy e;
            e.energy = static_cast<double>(p.energy.value);
            e.metal = static_cast<double>(p.metal.value);
            e.energyStorage = static_cast<double>(p.maxEnergy.value);
            e.metalStorage = static_cast<double>(p.maxMetal.value);
            e.energyIncome = static_cast<double>(p.previousEnergyProductionBuffer.value);
            e.metalIncome = static_cast<double>(p.previousMetalProductionBuffer.value);
            // [open] Net is production less what the player's units asked to
            // spend, both per second.
            e.netEnergy = e.energyIncome - static_cast<double>(p.previousDesiredEnergyConsumptionBuffer.value);
            e.netMetal = e.metalIncome - static_cast<double>(p.previousDesiredMetalConsumptionBuffer.value);
            return e;
        }

        // ---- choosing what to build ----------------------------------------

        const std::vector<std::size_t>& menuFor(const std::string& builderType)
        {
            auto idx = typeIndex.find(builderType);
            static const std::vector<std::size_t> none;
            if (idx == typeIndex.end())
            {
                return none;
            }
            auto cached = menuCache.find(idx->second);
            if (cached != menuCache.end())
            {
                return cached->second;
            }
            std::vector<std::size_t> menu;
            if (config.buildTree && config.buildTree->knows(builderType))
            {
                // The tree's sets are in name order. [open] The original walks
                // the builder's menu in the order of its GUI pages, which the
                // tree does not keep; the draw is order-sensitive but not
                // order-biased.
                for (const auto& name : config.buildTree->buildableBy.at(builderType))
                {
                    auto t = typeIndex.find(name);
                    if (t == typeIndex.end())
                    {
                        t = typeIndex.find(upperCopy(name));
                    }
                    if (t != typeIndex.end())
                    {
                        menu.push_back(t->second);
                    }
                }
            }
            else
            {
                for (std::size_t i = 0; i < typeNames.size(); ++i)
                {
                    menu.push_back(i);
                }
            }
            return menuCache.emplace(idx->second, std::move(menu)).first->second;
        }

        /**
         * S:120: scores every type on the builder's menu, draws one by weighted
         * reservoir, and rejects it (no re-draw) if its side is not the
         * builder's. `wantMobile` is what the builder can make with an order
         * RWE can express: a factory makes units, a mobile builder structures.
         */
        std::optional<std::string> chooseType(const GameSimulation& sim, const std::string& builderType, bool wantMobile)
        {
            const auto& menu = menuFor(builderType);
            if (menu.empty())
            {
                return std::nullopt;
            }
            const auto economy = economyOf(sim);

            std::vector<int> scores(menu.size(), 0);
            for (std::size_t i = 0; i < menu.size(); ++i)
            {
                const auto t = menu[i];
                if (facts[t].mobile != wantMobile || excluded[t])
                {
                    continue;
                }
                scores[i] = classicFinalRating(ratings[t], economy, entries[t].multiplier, entries[t].limit, completedCounts[t]);
            }

            const int chosen = classicWeightedDraw(scores, rng);
            if (chosen < 0)
            {
                return std::nullopt;
            }
            const auto t = menu[static_cast<std::size_t>(chosen)];

            // The side filter; a pick of the wrong side is dropped.
            auto builderIdx = typeIndex.find(builderType);
            std::string builderSide = builderIdx == typeIndex.end() ? std::string() : typeSides[builderIdx->second];
            if (builderSide.empty())
            {
                builderSide = playerSide;
            }
            if (!typeSides[t].empty() && !builderSide.empty() && typeSides[t] != builderSide)
            {
                return std::nullopt;
            }
            return typeNames[t];
        }

        // ---- membership and the 30-tick sweep ------------------------------

        bool eligible(const GameSimulation& sim, const UnitState& unit, const UnitDefinition*& def) const
        {
            if (unit.owner != self || !unit.isAlive() || unit.heldByMission || unit.carriedBy)
            {
                return false;
            }
            return isCompleted(sim, unit, def);
        }

        void purge(const GameSimulation& sim)
        {
            for (auto id : groups.allUnits())
            {
                auto unit = sim.tryGetUnitState(id);
                bool keep = false;
                if (unit)
                {
                    const UnitDefinition* def = nullptr;
                    const auto& u = unit->get();
                    keep = u.owner == self && u.isAlive() && isCompleted(sim, u, def);
                    auto recorded = groupedType.find(id.value);
                    keep = keep && recorded != groupedType.end() && recorded->second == u.unitType;
                }
                if (!keep)
                {
                    groups.remove(id);
                    groupedType.erase(id.value);
                }
            }
        }

        void classify(const GameSimulation& sim, std::vector<PlayerCommand>& out)
        {
            for (const auto& [unitId, unit] : sim.units)
            {
                const UnitDefinition* def = nullptr;
                if (!eligible(sim, unit, def))
                {
                    continue;
                }

                // Only a unit whose group number is 0 is assigned.
                if (groups.groupOf(unitId) == ClassicGroup::None)
                {
                    auto idx = typeIndex.find(unit.unitType);
                    if (idx != typeIndex.end())
                    {
                        int g = classicClassify(classifyFacts[idx->second]);
                        if (g != ClassicGroup::None)
                        {
                            groups.assign(unitId, g);
                            groupedType[unitId.value] = unit.unitType;
                        }
                    }
                }

                // S:119 "Standing policies are overwritten": fire at will, and
                // maneuver for a cancapture type, roam for the rest. Only
                // written where it differs; the result is the same as the
                // original's rewrite every sweep.
                if (def->fireStandOrders && unit.fireOrders != UnitFireOrders::FireAtWill)
                {
                    out.emplace_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetFireOrders{UnitFireOrders::FireAtWill}));
                }
                if (def->mobileStandOrders)
                {
                    const auto wanted = def->canCapture ? UnitMovementOrders::Maneuver : UnitMovementOrders::Roam;
                    if (unit.moveOrders != wanted)
                    {
                        out.emplace_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetMovementOrders{wanted}));
                    }
                }
            }
        }

        // ---- reading the world ----------------------------------------------

        std::function<ClassicPosition(UnitId)> positionFn(const GameSimulation& sim) const
        {
            return [&sim](UnitId id) {
                auto unit = sim.tryGetUnitState(id);
                if (!unit)
                {
                    return ClassicPosition{};
                }
                return ClassicPosition{toDouble(unit->get().position.x), toDouble(unit->get().position.z)};
            };
        }

        SimVector groundAt(const GameSimulation& sim, double x, double z) const
        {
            // Kept on the map: the terrain lookup is only defined inside it.
            const double left = toDouble(sim.terrain.leftInWorldUnits());
            const double right = toDouble(sim.terrain.rightCutoffInWorldUnits());
            const double top = toDouble(sim.terrain.topInWorldUnits());
            const double bottom = toDouble(sim.terrain.bottomCutoffInWorldUnits());
            x = std::max(left + 16.0, std::min(x, right - 16.0));
            z = std::max(top + 16.0, std::min(z, bottom - 16.0));
            auto sx = toScalar(x);
            auto sz = toScalar(z);
            return SimVector(sx, sim.terrain.getHeightAt(sx, sz), sz);
        }

        struct EnemyTarget
        {
            UnitId id;
            SimVector position;
        };

        /**
         * S:119: the assault target search scans every non-allied active
         * player's units directly, skipping the dead, the airborne and the
         * immune, and takes the smallest horizontal squared distance, first on
         * a tie. It does not look at what is visible.
         */
        std::optional<EnemyTarget> nearestEnemy(const GameSimulation& sim, double fromX, double fromZ) const
        {
            std::optional<EnemyTarget> best;
            double bestD2 = 0.0;
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unit.owner == self || !unit.isAlive() || unit.immune || unit.carriedBy)
                {
                    continue;
                }
                if (sim.flyingUnitsSet.count(unitId) != 0)
                {
                    continue;
                }
                if (sim.getPlayer(unit.owner).status != GamePlayerStatus::Alive || sim.arePlayersAllied(self, unit.owner))
                {
                    continue;
                }
                double dx = toDouble(unit.position.x) - fromX;
                double dz = toDouble(unit.position.z) - fromZ;
                double d2 = dx * dx + dz * dz;
                if (!best || d2 < bestD2)
                {
                    best = EnemyTarget{unitId, unit.position};
                    bestD2 = d2;
                }
            }
            return best;
        }

        // ---- being attacked -------------------------------------------------

        void noticeAttacks(const GameSimulation& sim, std::vector<PlayerCommand>& out)
        {
            // S:122 "Being attacked": a cancapture unit that has been hit
            // postpones builder work until tick + 30 + rand(300) and loses its
            // replaceable orders. Damage is read off the unit's own hit points,
            // which is the same on every peer.
            for (const auto& [unitId, unit] : sim.units)
            {
                if (unit.owner != self || !unit.isAlive())
                {
                    continue;
                }
                auto defIt = sim.unitDefinitions.find(unit.unitType);
                if (defIt == sim.unitDefinitions.end() || !defIt->second.canCapture || unit.isBeingBuilt(defIt->second))
                {
                    continue;
                }
                auto previous = lastHitPoints.find(unitId.value);
                const bool hit = previous != lastHitPoints.end() && unit.hitPoints < previous->second;
                lastHitPoints[unitId.value] = unit.hitPoints;
                if (!hit || unit.heldByMission)
                {
                    continue;
                }
                nextAction = sim.gameTime.value + 30u + rng.below(300);
                if (!unit.orders.empty()
                    && std::none_of(unit.orders.begin(), unit.orders.end(), [](const UnitOrder& o) { return isWorkOrder(o); }))
                {
                    out.emplace_back(PlayerUnitCommand(unitId, PlayerUnitCommand::Stop{}));
                }
            }
        }

        // ---- slot 1: fixed builders and converters ---------------------------

        void runBuildTimer(const GameSimulation& sim, std::vector<PlayerCommand>& out)
        {
            const auto economy = economyOf(sim);
            for (const auto& [unitId, unit] : sim.units)
            {
                const UnitDefinition* def = nullptr;
                if (!eligible(sim, unit, def) || def->isMobile)
                {
                    continue;
                }

                // A metal converter: off when stored energy is at most twice
                // stored metal; above that, on with a positive net energy and
                // a nonzero rand(5). Failing both leaves it as it is.
                if (def->makesMetal != Metal(0.0f))
                {
                    if (economy.energy <= 2.0 * economy.metal)
                    {
                        if (unit.activated)
                        {
                            out.emplace_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetOnOff{false}));
                        }
                    }
                    else if (economy.netEnergy > 0.0 && rng.below(5) != 0)
                    {
                        if (!unit.activated)
                        {
                            out.emplace_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetOnOff{true}));
                        }
                    }
                }

                // An idle fixed builder draws one item and queues one of it.
                if (def->builder && unit.orders.empty() && unit.buildQueue.empty()
                    && std::holds_alternative<FactoryBehaviorStateIdle>(unit.factoryState))
                {
                    if (auto type = chooseType(sim, unit.unitType, true))
                    {
                        out.emplace_back(PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyBuildQueue{1, *type}));
                    }
                }
            }
        }

        // ---- slots 2, 3, 6, 7: assaults and reserves ---------------------------

        void runAssault(const GameSimulation& sim, std::vector<PlayerCommand>& out, int assaultGroup, int reserveGroup, double limit, bool& attacking)
        {
            auto positionOf = positionFn(sim);
            classicRebalanceAssault(groups, assaultGroup, reserveGroup, limit, positionOf);

            const auto size = groups.members(assaultGroup).size();

            // With three or fewer members, or before the assault has begun
            // attacking with fewer than six, it rallies. Once attacking, four
            // or five units continue.
            const bool rally = size <= AssaultMinimum || (!attacking && size < AssaultMaximum);
            if (rally)
            {
                ClassicPosition point;
                bool found = false;
                for (int g : {ClassicGroup::Defences, ClassicGroup::Buildings, ClassicGroup::Builders})
                {
                    if (classicCentroid(groups, g, positionOf, point))
                    {
                        found = true;
                        break;
                    }
                }
                if (found)
                {
                    attacking = false;
                    const auto where = groundAt(sim, point.x, point.z);
                    for (const auto& [unitId, unit] : sim.units)
                    {
                        (void)unit;
                        if (groups.groupOf(unitId) == assaultGroup)
                        {
                            // The source passes an extra argument of 160 with this
                            // move; RWE's move order has no such field. [open]
                            out.emplace_back(issue(unitId, MoveOrder(where), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
                        }
                    }
                    return;
                }
                // No rally point: it falls through to attack regardless of size.
            }

            ClassicPosition centre;
            if (!classicCentroid(groups, assaultGroup, positionOf, centre))
            {
                // An empty assault has nothing to send, and has not begun.
                return;
            }
            attacking = true;
            auto target = nearestEnemy(sim, centre.x, centre.z);
            if (!target)
            {
                return;
            }
            for (const auto& [unitId, unit] : sim.units)
            {
                (void)unit;
                if (groups.groupOf(unitId) == assaultGroup)
                {
                    out.emplace_back(issue(unitId, AttackOrder(target->id), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
                }
            }
        }

        void runReserve(const GameSimulation& sim, std::vector<PlayerCommand>& out, int assaultGroup, int reserveGroup)
        {
            ClassicPosition centre;
            if (!classicCentroid(groups, assaultGroup, positionFn(sim), centre))
            {
                return;
            }
            const auto where = groundAt(sim, centre.x, centre.z);
            for (const auto& [unitId, unit] : sim.units)
            {
                (void)unit;
                if (groups.groupOf(unitId) == reserveGroup)
                {
                    out.emplace_back(issue(unitId, MoveOrder(where), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
                }
            }
        }

        // ---- slot 4: mobile builders -------------------------------------------

        void runBuilders(const GameSimulation& sim, std::vector<PlayerCommand>& out)
        {
            const SimVector base = groundAt(sim, baseX, baseZ);
            const double mapW = toDouble(sim.terrain.getWidthInWorldUnits());
            const double mapH = toDouble(sim.terrain.getHeightInWorldUnits());
            const double leash = (mapW + mapH) / 3.0;

            for (const auto& [unitId, unit] : sim.units)
            {
                if (groups.groupOf(unitId) != ClassicGroup::Builders)
                {
                    continue;
                }
                const UnitDefinition* def = nullptr;
                if (!eligible(sim, unit, def))
                {
                    continue;
                }

                // A unit whose current order is work in hand is not replaced.
                if (!unit.orders.empty() && isWorkOrder(unit.orders.front()))
                {
                    continue;
                }

                // BT-117 (0x408100, first pass): a cancapture builder is held
                // back at five builders and after being attacked, and only
                // then. Held back is not the end of it: the idle fallback
                // below is the second pass, and takes it at five or more.
                const bool heldBack = def->canCapture
                    && (completedBuilders >= BuilderCap || sim.gameTime.value < nextAction);

                // Draw a type, search for a site, issue the build.
                if (!heldBack)
                {
                    if (auto type = chooseType(sim, unit.unitType, false))
                    {
                        const auto& typeDef = sim.unitDefinitions.at(*type);
                        if (auto site = placement.findSite(sim, typeDef, unit.position, base, rng))
                        {
                            // The range limit is on the SITE, measured from the
                            // base point: (mapWidth + mapHeight) / 3. A site
                            // farther off is discarded and no order is issued;
                            // the builder is never sent home for being far away.
                            bool inRange = true;
                            if (def->canCapture)
                            {
                                double dx = toDouble(site->x) - baseX;
                                double dz = toDouble(site->z) - baseZ;
                                inRange = std::sqrt(dx * dx + dz * dz) <= leash;
                            }
                            if (inRange)
                            {
                                out.emplace_back(issue(unitId, BuildOrder(*type, *site), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
                                continue;
                            }
                        }
                    }
                }

                // Nothing to do: the idle fallback (the second pass).
                if (def->canCapture)
                {
                    // BT-117: a cancapture unit takes part only when five or
                    // more builders exist (0x40830A), where RWE had it the
                    // other way about.
                    if (completedBuilders < BuilderCap)
                    {
                        continue;
                    }
                    // It moves to the mirror of its own position through the
                    // base point -- the base plus the vector from itself to
                    // the base -- unless that vector is longer than 640, when
                    // a random direction of length 640 replaces it. A patrol
                    // is queued after.
                    double vx = baseX - toDouble(unit.position.x);
                    double vz = baseZ - toDouble(unit.position.z);
                    if (std::sqrt(vx * vx + vz * vz) > static_cast<double>(WanderLimit))
                    {
                        const float radians = static_cast<float>(rng.below(360)) * (3.14159265358979323846f / 180.0f);
                        vx = static_cast<double>(detCos(radians)) * static_cast<double>(WanderLimit);
                        vz = static_cast<double>(detSin(radians)) * static_cast<double>(WanderLimit);
                    }
                    out.emplace_back(issue(unitId, MoveOrder(groundAt(sim, baseX + vx, baseZ + vz)), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
                    auto around = [&]() {
                        double x = baseX + static_cast<double>(static_cast<int>(rng.below(2 * WanderLimit + 1)) - WanderLimit);
                        double z = baseZ + static_cast<double>(static_cast<int>(rng.below(2 * WanderLimit + 1)) - WanderLimit);
                        return groundAt(sim, x, z);
                    };
                    out.emplace_back(issue(unitId, PatrolOrder(around()), PlayerUnitCommand::IssueOrder::IssueKind::Queued));
                }
                else
                {
                    double dx = toDouble(unit.position.x) - baseX;
                    double dz = toDouble(unit.position.z) - baseZ;
                    if (std::sqrt(dx * dx + dz * dz) > static_cast<double>(PatrolClose))
                    {
                        out.emplace_back(issue(unitId, PatrolOrder(base), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
                    }
                    else
                    {
                        const float radians = static_cast<float>(rng.below(360)) * (3.14159265358979323846f / 180.0f);
                        const double distance = static_cast<double>(PatrolClose) + static_cast<double>(rng.below(PatrolClose / 2));
                        const double x = baseX + static_cast<double>(detCos(radians)) * distance;
                        const double z = baseZ + static_cast<double>(detSin(radians)) * distance;
                        out.emplace_back(issue(unitId, PatrolOrder(groundAt(sim, x, z)), PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
                    }
                }
            }
        }

        // ---- slot 8: aircraft routes -------------------------------------------

        void runAir(const GameSimulation& sim, std::vector<PlayerCommand>& out)
        {
            const auto count = groups.members(ClassicGroup::Aircraft).size();
            if (count == 0)
            {
                return;
            }

            std::vector<SimVector> route;
            bool firstIsMove = true;
            if (count < 5)
            {
                if (baseX == 0.0 && baseZ == 0.0)
                {
                    // The base position is exactly zero: patrol to the nearest
                    // enemy instead. The source does not check there is one;
                    // RWE has to.
                    auto enemy = nearestEnemy(sim, baseX, baseZ);
                    if (!enemy)
                    {
                        return;
                    }
                    route.push_back(enemy->position);
                    firstIsMove = false;
                }
                else
                {
                    // Two or three random points near the base: the first a
                    // move, the rest patrols. [open] "near" is a draw of up to
                    // 400 either way on each axis.
                    const auto points = 2u + rng.below(2);
                    for (unsigned int i = 0; i < points; ++i)
                    {
                        double x = baseX + static_cast<double>(static_cast<int>(rng.below(801)) - 400);
                        double z = baseZ + static_cast<double>(static_cast<int>(rng.below(801)) - 400);
                        route.push_back(groundAt(sim, x, z));
                    }
                }
            }
            else
            {
                // Five or more: a random map edge, patrolled. [open] The point
                // along the edge is a draw, kept 64 in from the border.
                const double left = toDouble(sim.terrain.leftInWorldUnits());
                const double right = toDouble(sim.terrain.rightCutoffInWorldUnits());
                const double top = toDouble(sim.terrain.topInWorldUnits());
                const double bottom = toDouble(sim.terrain.bottomCutoffInWorldUnits());
                const auto edge = rng.below(4);
                const double along = static_cast<double>(rng.below(1000)) / 1000.0;
                double x = 0.0;
                double z = 0.0;
                switch (edge)
                {
                    case 0:
                        x = left + 64.0 + along * (right - left - 128.0);
                        z = top + 64.0;
                        break;
                    case 1:
                        x = left + 64.0 + along * (right - left - 128.0);
                        z = bottom - 64.0;
                        break;
                    case 2:
                        x = left + 64.0;
                        z = top + 64.0 + along * (bottom - top - 128.0);
                        break;
                    default:
                        x = right - 64.0;
                        z = top + 64.0 + along * (bottom - top - 128.0);
                        break;
                }
                route.push_back(groundAt(sim, x, z));
                firstIsMove = false;
            }

            for (const auto& [unitId, unit] : sim.units)
            {
                (void)unit;
                if (groups.groupOf(unitId) != ClassicGroup::Aircraft)
                {
                    continue;
                }
                for (std::size_t i = 0; i < route.size(); ++i)
                {
                    auto kind = i == 0 ? PlayerUnitCommand::IssueOrder::IssueKind::Immediate : PlayerUnitCommand::IssueOrder::IssueKind::Queued;
                    if (i == 0 && firstIsMove)
                    {
                        out.emplace_back(issue(unitId, MoveOrder(route[i]), kind));
                    }
                    else
                    {
                        out.emplace_back(issue(unitId, PatrolOrder(route[i]), kind));
                    }
                }
            }
        }

        // ---- the squad manager's tick ---------------------------------------------

        void runSlots(const GameSimulation& sim, std::vector<PlayerCommand>& out)
        {
            const unsigned int now = sim.gameTime.value;
            for (std::size_t slot = 0; slot < SlotCount; ++slot)
            {
                // Slot 0 has no timer; slot 5 is "due every call and does nothing".
                if (slot == 0 || slot == SlotBase)
                {
                    continue;
                }
                if (nextTime[slot] > now)
                {
                    continue;
                }
                switch (slot)
                {
                    case SlotBuild:
                        nextTime[slot] = now + 30u;
                        runBuildTimer(sim, out);
                        break;
                    case SlotLandAssault:
                        nextTime[slot] = now + 300u;
                        runAssault(sim, out, ClassicGroup::LandAssault, ClassicGroup::LandReserve, LandCohesionLimit, landAttacking);
                        break;
                    case SlotLandReserve:
                        nextTime[slot] = now + 150u;
                        runReserve(sim, out, ClassicGroup::LandAssault, ClassicGroup::LandReserve);
                        break;
                    case SlotScout:
                        nextTime[slot] = now + 90u;
                        runBuilders(sim, out);
                        break;
                    case SlotWaterAssault:
                        nextTime[slot] = now + 300u;
                        runAssault(sim, out, ClassicGroup::WaterAssault, ClassicGroup::WaterReserve, WaterCohesionLimit, waterAttacking);
                        break;
                    case SlotWaterReserve:
                        nextTime[slot] = now + 150u;
                        runReserve(sim, out, ClassicGroup::WaterAssault, ClassicGroup::WaterReserve);
                        break;
                    case SlotAir:
                        nextTime[slot] = now + 30u + rng.below(900);
                        runAir(sim, out);
                        break;
                    case SlotProbe:
                        // Re-armed at tick + 30 + rand(150). Nothing in the
                        // ordinary classifier ever fills group 9, so the probe
                        // has nobody to send anywhere and is left at that.
                        nextTime[slot] = now + 30u + rng.below(150);
                        break;
                    default:
                        break;
                }
            }
        }
    };

    ClassicAiController::ClassicAiController(PlayerId playerId, ClassicAiConfig config, std::uint64_t rngSeed)
        : playerId(playerId), impl(std::make_unique<Impl>(playerId, std::move(config), rngSeed))
    {
        profile.name = "CLASSIC";
        profile.difficulty = impl->config.difficulty;
        profile.idle = impl->config.idle;
        switch (impl->config.difficulty)
        {
            case AiDifficulty::Easy:
                profile.resourceCheatMultiplier = SimScalar(0.5f);
                break;
            case AiDifficulty::Standard:
                profile.resourceCheatMultiplier = SimScalar(0.7f);
                break;
            default:
                profile.resourceCheatMultiplier = SimScalar(1.0f);
                break;
        }
    }

    ClassicAiController::~ClassicAiController() = default;

    void ClassicAiController::tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands)
    {
        // An idle seat, or a replay: construct, draw the seed, and do nothing.
        if (profile.idle || profile.difficulty == AiDifficulty::Idle)
        {
            return;
        }
        auto& m = *impl;
        if (sim.getPlayer(playerId).status != GamePlayerStatus::Alive)
        {
            return;
        }
        if (!m.ready)
        {
            m.initialise(sim);
        }
        ++m.calls;

        // S:119: the squad manager's tick, then the strategy table's update.
        m.purge(sim);
        m.noticeAttacks(sim, outCommands);
        if (m.calls % ClassifyInterval == 0)
        {
            m.classify(sim, outCommands);
        }
        m.runSlots(sim, outCommands);
        m.strategyUpdate(sim);
    }
}
