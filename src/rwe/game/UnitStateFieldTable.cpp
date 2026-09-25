#include "UnitStateFieldTable.h"

namespace rwe
{
    namespace
    {
        using nlohmann::json;

        template <auto Member>
        json saveSimVectorUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveSimVector(u.*Member);
        }

        template <auto Member>
        void loadSimVectorUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadSimVector(j);
        }

        template <auto Member>
        json saveOptionalSimVectorUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveOptional(u.*Member, saveSimVector);
        }

        template <auto Member>
        void loadOptionalSimVectorUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadOptional(j, loadSimVector);
        }

        template <auto Member>
        json saveSimAngleUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveSimAngle(u.*Member);
        }

        template <auto Member>
        void loadSimAngleUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadSimAngle(j);
        }

        template <auto Member>
        json saveGameTimeUnitStateField(const UnitState& u, const SaveContext&)
        {
            return (u.*Member).value;
        }

        template <auto Member>
        void loadGameTimeUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = GameTime(j.get<unsigned int>());
        }

        template <auto Member>
        json saveEnergyUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveEnergy(u.*Member);
        }

        template <auto Member>
        void loadEnergyUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadEnergy(j);
        }

        template <auto Member>
        json saveMetalUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveMetal(u.*Member);
        }

        template <auto Member>
        void loadMetalUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadMetal(j);
        }

        template <auto Member>
        json saveOptionalGameTimeUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveOptional(u.*Member, saveGameTime);
        }

        template <auto Member>
        void loadOptionalGameTimeUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadOptional(j, loadGameTime);
        }

        template <auto Member>
        json saveOptionalUnitIdUnitStateField(const UnitState& u, const SaveContext& ctx)
        {
            return saveOptional(u.*Member, [&](UnitId id) { return saveUnitIdRef(id, ctx); });
        }

        template <auto Member>
        void loadOptionalUnitIdUnitStateField(const json& j, UnitState& u, const LoadContext& ctx)
        {
            u.*Member = loadOptional(j, [&](const json& v) { return loadUnitIdRef(v, ctx); });
        }

        template <auto Member>
        json savePlayerIdUnitStateField(const UnitState& u, const SaveContext&)
        {
            return (u.*Member).value;
        }

        template <auto Member>
        json saveEnumUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveEnum(u.*Member);
        }

        template <auto Member>
        void loadEnumUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadEnum<typename UnitStateFieldMemberOf<decltype(Member)>::type>(j);
        }

        template <auto Member>
        void loadPlayerIdUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = PlayerId(j.get<unsigned int>());
        }

        /**
         * A key that saves older than the field may omit. Absence stands for
         * the member's own default, which is how loadUnitStateInto's
         * `j.value(..., false)` used to read it.
         */
        template <auto Member>
        void loadLegacyBoolUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = j.is_null() ? false : j.get<bool>();
        }

        json savePiecesUnitStateField(const UnitState& u, const SaveContext&)
        {
            json pieces = json::array();
            for (const auto& p : u.pieces)
            {
                pieces.push_back(saveUnitMesh(p));
            }
            return pieces;
        }

        json saveCobEnvironmentUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveCobEnvironment(*u.cobEnvironment);
        }

        void loadCobEnvironmentUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            loadCobEnvironmentInto(j, *u.cobEnvironment);
        }

        /**
         * Genuinely special: each saved weapon names its aim thread through
         * the unit's own COB environment, so both steps reach in for it, and
         * the load matches the array position-for-position against the slots
         * the unit was emplaced with.
         */
        json saveWeaponsUnitStateField(const UnitState& u, const SaveContext& ctx)
        {
            json weapons = json::array();
            for (const auto& w : u.weapons)
            {
                weapons.push_back(saveOptional(w, [&](const UnitWeapon& weapon) { return saveUnitWeapon(weapon, ctx, *u.cobEnvironment); }));
            }
            return weapons;
        }

        void loadWeaponsUnitStateField(const json& j, UnitState& u, const LoadContext& ctx)
        {
            for (std::size_t i = 0; i < u.weapons.size(); ++i)
            {
                u.weapons[i] = loadOptional(j.at(i), [&](const json& wj) { return loadUnitWeapon(wj, ctx, *u.cobEnvironment); });
            }
        }

        /** The order queue, saved and loaded through id remapping. */
        json saveOrdersUnitStateField(const UnitState& u, const SaveContext& ctx)
        {
            json orders = json::array();
            for (const auto& o : u.orders)
            {
                orders.push_back(saveUnitOrder(o, ctx));
            }
            return orders;
        }

        void loadOrdersUnitStateField(const json& j, UnitState& u, const LoadContext& ctx)
        {
            u.orders.clear();
            for (const auto& oj : j)
            {
                u.orders.push_back(loadUnitOrder(oj, ctx));
            }
        }

        json saveBuildQueueUnitStateField(const UnitState& u, const SaveContext&)
        {
            json buildQueue = json::array();
            for (const auto& [unitType, count] : u.buildQueue)
            {
                buildQueue.push_back(json{{"unitType", unitType}, {"count", count}});
            }
            return buildQueue;
        }

        void loadBuildQueueUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.buildQueue.clear();
            for (const auto& bj : j)
            {
                u.buildQueue.emplace_back(bj.at("unitType").get<std::string>(), bj.at("count").get<int>());
            }
        }

        json saveCarriedUnitsUnitStateField(const UnitState& u, const SaveContext& ctx)
        {
            json carriedUnits = json::array();
            for (const auto& id : u.carriedUnits)
            {
                carriedUnits.push_back(saveUnitIdRef(id, ctx));
            }
            return carriedUnits;
        }

        void loadCarriedUnitsUnitStateField(const json& j, UnitState& u, const LoadContext& ctx)
        {
            u.carriedUnits.clear();
            for (const auto& cj : j)
            {
                u.carriedUnits.push_back(loadUnitIdRef(cj, ctx));
            }
        }

        template <auto Member>
        json saveUnitPhysicsInfoUnitStateField(const UnitState& u, const SaveContext& ctx)
        {
            return saveUnitPhysicsInfo(u.*Member, ctx);
        }

        template <auto Member>
        void loadUnitPhysicsInfoUnitStateField(const json& j, UnitState& u, const LoadContext& ctx)
        {
            u.*Member = loadUnitPhysicsInfo(j, ctx);
        }

        template <auto Member>
        json saveLifeStateUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveLifeState(u.*Member);
        }

        template <auto Member>
        void loadLifeStateUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadLifeState(j);
        }

        template <auto Member>
        json saveUnitBehaviorStateUnitStateField(const UnitState& u, const SaveContext& ctx)
        {
            return saveUnitBehaviorState(u.*Member, ctx);
        }

        template <auto Member>
        void loadUnitBehaviorStateUnitStateField(const json& j, UnitState& u, const LoadContext& ctx)
        {
            u.*Member = loadUnitBehaviorState(j, ctx);
        }

        template <auto Member>
        json saveNavigationStateInfoUnitStateField(const UnitState& u, const SaveContext& ctx)
        {
            return saveNavigationStateInfo(u.*Member, ctx);
        }

        template <auto Member>
        void loadNavigationStateInfoUnitStateField(const json& j, UnitState& u, const LoadContext& ctx)
        {
            u.*Member = loadNavigationStateInfo(j, ctx);
        }

        template <auto Member>
        json saveOptionalAirWorkOrbitUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveOptional(u.*Member, saveAirWorkOrbitState);
        }

        template <auto Member>
        void loadOptionalAirWorkOrbitUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadOptional(j, loadAirWorkOrbitState);
        }

        template <auto Member>
        json saveOptionalAirLoiterUnitStateField(const UnitState& u, const SaveContext&)
        {
            return saveOptional(u.*Member, saveAirLoiterState);
        }

        template <auto Member>
        void loadOptionalAirLoiterUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = loadOptional(j, loadAirLoiterState);
        }

        template <auto Member>
        json saveFactoryBehaviorStateUnitStateField(const UnitState& u, const SaveContext& ctx)
        {
            return saveFactoryBehaviorState(u.*Member, ctx);
        }

        template <auto Member>
        void loadFactoryBehaviorStateUnitStateField(const json& j, UnitState& u, const LoadContext& ctx)
        {
            u.*Member = loadFactoryBehaviorState(j, ctx);
        }

        constexpr UnitStateFieldTableEntry unitStateFieldTableRows[] = {
            {"unitType", hashedPlain<&UnitState::unitType>()},
            {"pieces", saveOnly(
                           savePiecesUnitStateField,
                           "the unit's mesh piece transforms and the in-flight move/turn animations on them, written by the scripts as they run; the hash reads a piece only through what is read off it into fields of its own (`nanoPoint`, a carried unit's `position`), and the mesh itself is render-facing",
                           "restored at unit emplacement, which builds the pieces the load walk then finds already waiting")},
            {"position", hashed<&UnitState::position>(saveSimVectorUnitStateField<&UnitState::position>, loadSimVectorUnitStateField<&UnitState::position>)},
            {"previousPosition", unhashed<&UnitState::previousPosition>(
                                    saveSimVectorUnitStateField<&UnitState::previousPosition>,
                                    loadSimVectorUnitStateField<&UnitState::previousPosition>,
                                    "last tick's position, for interpolation and the move-rate step; derived from position, which is hashed")},
            {"cobEnvironment", unhashed<&UnitState::cobEnvironment>(
                                  saveCobEnvironmentUnitStateField,
                                  loadCobEnvironmentUnitStateField,
                                  "the COB VM's threads and statics; derived from the hashed events that drive the scripts, and saved because a reload must resume them where they were")},
            {"owner", hashed<&UnitState::owner>(savePlayerIdUnitStateField<&UnitState::owner>, loadPlayerIdUnitStateField<&UnitState::owner>)},
            {"rotation", hashed<&UnitState::rotation>(saveSimAngleUnitStateField<&UnitState::rotation>, loadSimAngleUnitStateField<&UnitState::rotation>)},
            {"previousRotation", unhashed<&UnitState::previousRotation>(
                                    saveSimAngleUnitStateField<&UnitState::previousRotation>,
                                    loadSimAngleUnitStateField<&UnitState::previousRotation>,
                                    "last tick's rotation, for interpolation; derived from rotation, which is hashed")},
            {"physics", hashed<&UnitState::physics>(saveUnitPhysicsInfoUnitStateField<&UnitState::physics>, loadUnitPhysicsInfoUnitStateField<&UnitState::physics>)},
            {"hitPoints", hashedPlain<&UnitState::hitPoints>()},
            {"lifeState", hashed<&UnitState::lifeState>(saveLifeStateUnitStateField<&UnitState::lifeState>, loadLifeStateUnitStateField<&UnitState::lifeState>)},
            {"orders", hashed<&UnitState::orders>(saveOrdersUnitStateField, loadOrdersUnitStateField)},
            {"behaviourState", hashed<&UnitState::behaviourState>(saveUnitBehaviorStateUnitStateField<&UnitState::behaviourState>, loadUnitBehaviorStateUnitStateField<&UnitState::behaviourState>)},
            {"navigationState", hashed<&UnitState::navigationState>(saveNavigationStateInfoUnitStateField<&UnitState::navigationState>, loadNavigationStateInfoUnitStateField<&UnitState::navigationState>)},
            {"buildOrderUnitId", unhashed<&UnitState::buildOrderUnitId>(
                                     saveOptionalUnitIdUnitStateField<&UnitState::buildOrderUnitId>,
                                     loadOptionalUnitIdUnitStateField<&UnitState::buildOrderUnitId>,
                                     "the unit a build order's arm is deploying towards; written from the creation status on the behaviour state and cleared with the order, both of which are hashed")},
            {"inBuildStance", hashedPlain<&UnitState::inBuildStance>()},
            {"armStowDueTime", hashed<&UnitState::armStowDueTime>(saveOptionalGameTimeUnitStateField<&UnitState::armStowDueTime>, loadOptionalGameTimeUnitStateField<&UnitState::armStowDueTime>)},
            {"nanoPointQueriedAt", hashed<&UnitState::nanoPointQueriedAt>(saveOptionalGameTimeUnitStateField<&UnitState::nanoPointQueriedAt>, loadOptionalGameTimeUnitStateField<&UnitState::nanoPointQueriedAt>)},
            {"nanoPoint", hashed<&UnitState::nanoPoint>(saveSimVectorUnitStateField<&UnitState::nanoPoint>, loadSimVectorUnitStateField<&UnitState::nanoPoint>)},
            {"commandFireShotFired", unhashed<&UnitState::commandFireShotFired>(
                                        savePlainUnitStateField<&UnitState::commandFireShotFired>,
                                        loadLegacyBoolUnitStateField<&UnitState::commandFireShotFired>,
                                        "a commandfire shot's latch between the weapon pass and the order handler that ends the order; saved so a reload can still end the order, and the sync hash does not cover it"),
                true},
            {"yardOpen", hashedPlain<&UnitState::yardOpen>()},
            {"inCollision", hashedPlain<&UnitState::inCollision>()},
            {"weapons", hashed<&UnitState::weapons>(saveWeaponsUnitStateField, loadWeaponsUnitStateField)},
            {"fireOrders", hashed<&UnitState::fireOrders>(saveEnumUnitStateField<&UnitState::fireOrders>, loadEnumUnitStateField<&UnitState::fireOrders>)},
            {"moveOrders", hashed<&UnitState::moveOrders>(saveEnumUnitStateField<&UnitState::moveOrders>, loadEnumUnitStateField<&UnitState::moveOrders>)},
            {"cobBusy", hashedPlain<&UnitState::cobBusy>()},
            {"buggerOffActive", hashedPlain<&UnitState::buggerOffActive>()},
            {"armored", hashedPlain<&UnitState::armored>()},
            {"kills", hashedPlain<&UnitState::kills>()},
            {"sfxOccupyState", hashedPlain<&UnitState::sfxOccupyState>()},
            {"buildTimeCompleted", hashedPlain<&UnitState::buildTimeCompleted>()},
            {"nanoframeDecayTime", hashed<&UnitState::nanoframeDecayTime>(saveOptionalGameTimeUnitStateField<&UnitState::nanoframeDecayTime>, loadOptionalGameTimeUnitStateField<&UnitState::nanoframeDecayTime>)},
            {"nanoframeWorkedOn", hashedPlain<&UnitState::nanoframeWorkedOn>()},
            {"nanoframeDecayRemainder", hashedPlain<&UnitState::nanoframeDecayRemainder>()},
            {"selfDestructTime", hashed<&UnitState::selfDestructTime>(saveOptionalGameTimeUnitStateField<&UnitState::selfDestructTime>, loadOptionalGameTimeUnitStateField<&UnitState::selfDestructTime>)},
            {"paralyzedUntil", hashed<&UnitState::paralyzedUntil>(saveOptionalGameTimeUnitStateField<&UnitState::paralyzedUntil>, loadOptionalGameTimeUnitStateField<&UnitState::paralyzedUntil>)},
            {"moveRateBand", hashedPlain<&UnitState::moveRateBand>()},
            {"carriedBy", hashed<&UnitState::carriedBy>(saveOptionalUnitIdUnitStateField<&UnitState::carriedBy>, loadOptionalUnitIdUnitStateField<&UnitState::carriedBy>)},
            {"carriedPiece", unhashedPlain<&UnitState::carriedPiece>(
                                "the transport piece a passenger hangs from, named by the pickup script; the passenger link (carriedBy) is what is hashed")},
            {"carriedUnits", unhashed<&UnitState::carriedUnits>(
                                saveCarriedUnitsUnitStateField,
                                loadCarriedUnitsUnitStateField,
                                "the transport's passenger list, which every passenger's hashed carriedBy already points back at")},
            {"transportScriptTarget", hashed<&UnitState::transportScriptTarget>(saveOptionalUnitIdUnitStateField<&UnitState::transportScriptTarget>, loadOptionalUnitIdUnitStateField<&UnitState::transportScriptTarget>)},
            {"transportScriptStartedAt", hashed<&UnitState::transportScriptStartedAt>(saveGameTimeUnitStateField<&UnitState::transportScriptStartedAt>, loadGameTimeUnitStateField<&UnitState::transportScriptStartedAt>)},
            {"airWorkOrbit", hashed<&UnitState::airWorkOrbit>(saveOptionalAirWorkOrbitUnitStateField<&UnitState::airWorkOrbit>, loadOptionalAirWorkOrbitUnitStateField<&UnitState::airWorkOrbit>)},
            {"airLoiter", hashed<&UnitState::airLoiter>(saveOptionalAirLoiterUnitStateField<&UnitState::airLoiter>, loadOptionalAirLoiterUnitStateField<&UnitState::airLoiter>)},
            {"slowFacePoint", hashed<&UnitState::slowFacePoint>(saveOptionalSimVectorUnitStateField<&UnitState::slowFacePoint>, loadOptionalSimVectorUnitStateField<&UnitState::slowFacePoint>)},
            {"activated", hashedPlain<&UnitState::activated>()},
            // Only a mission unit is ever held, and a save from before the
            // flag existed holds none.
            {"heldByMission", hashedPlain<&UnitState::heldByMission>(), true},
            {"isSufficientlyPowered", hashedPlain<&UnitState::isSufficientlyPowered>()},
            {"cloakRequested", hashedPlain<&UnitState::cloakRequested>()},
            {"cloaked", hashedPlain<&UnitState::cloaked>()},
            {"cloakSuppressedUntil", hashed<&UnitState::cloakSuppressedUntil>(saveGameTimeUnitStateField<&UnitState::cloakSuppressedUntil>, loadGameTimeUnitStateField<&UnitState::cloakSuppressedUntil>)},
            {"energyProductionBuffer", hashed<&UnitState::energyProductionBuffer>(saveEnergyUnitStateField<&UnitState::energyProductionBuffer>, loadEnergyUnitStateField<&UnitState::energyProductionBuffer>)},
            {"metalProductionBuffer", hashed<&UnitState::metalProductionBuffer>(saveMetalUnitStateField<&UnitState::metalProductionBuffer>, loadMetalUnitStateField<&UnitState::metalProductionBuffer>)},
            {"previousEnergyProductionBuffer", hashed<&UnitState::previousEnergyProductionBuffer>(saveEnergyUnitStateField<&UnitState::previousEnergyProductionBuffer>, loadEnergyUnitStateField<&UnitState::previousEnergyProductionBuffer>)},
            {"previousMetalProductionBuffer", hashed<&UnitState::previousMetalProductionBuffer>(saveMetalUnitStateField<&UnitState::previousMetalProductionBuffer>, loadMetalUnitStateField<&UnitState::previousMetalProductionBuffer>)},
            {"previousEnergyConsumptionBuffer", hashed<&UnitState::previousEnergyConsumptionBuffer>(saveEnergyUnitStateField<&UnitState::previousEnergyConsumptionBuffer>, loadEnergyUnitStateField<&UnitState::previousEnergyConsumptionBuffer>)},
            {"previousMetalConsumptionBuffer", hashed<&UnitState::previousMetalConsumptionBuffer>(saveMetalUnitStateField<&UnitState::previousMetalConsumptionBuffer>, loadMetalUnitStateField<&UnitState::previousMetalConsumptionBuffer>)},
            {"energyConsumptionBuffer", hashed<&UnitState::energyConsumptionBuffer>(saveEnergyUnitStateField<&UnitState::energyConsumptionBuffer>, loadEnergyUnitStateField<&UnitState::energyConsumptionBuffer>)},
            {"metalConsumptionBuffer", hashed<&UnitState::metalConsumptionBuffer>(saveMetalUnitStateField<&UnitState::metalConsumptionBuffer>, loadMetalUnitStateField<&UnitState::metalConsumptionBuffer>)},
            {"energyRequestBuffer", hashed<&UnitState::energyRequestBuffer>(saveEnergyUnitStateField<&UnitState::energyRequestBuffer>, loadEnergyUnitStateField<&UnitState::energyRequestBuffer>)},
            {"metalRequestBuffer", hashed<&UnitState::metalRequestBuffer>(saveMetalUnitStateField<&UnitState::metalRequestBuffer>, loadMetalUnitStateField<&UnitState::metalRequestBuffer>)},
            {"energyDebt", hashed<&UnitState::energyDebt>(saveEnergyUnitStateField<&UnitState::energyDebt>, loadEnergyUnitStateField<&UnitState::energyDebt>)},
            {"metalDebt", hashed<&UnitState::metalDebt>(saveMetalUnitStateField<&UnitState::metalDebt>, loadMetalUnitStateField<&UnitState::metalDebt>)},
            {"buildQueue", unhashed<&UnitState::buildQueue>(
                              saveBuildQueueUnitStateField,
                              loadBuildQueueUnitStateField,
                              "the factory's pending unit types, queued by build commands and worked off the front by the factory's behaviour; a reload must resume the queue, and the sync hash does not cover it")},
            {"factoryState", unhashed<&UnitState::factoryState>(
                                saveFactoryBehaviorStateUnitStateField<&UnitState::factoryState>,
                                loadFactoryBehaviorStateUnitStateField<&UnitState::factoryState>,
                                "the factory's create/build working state, written and read by the factory's behaviour beside the build queue it works through; the sync hash does not cover it")},
        };

        static_assert(
            unitStateFieldTableWellFormed(unitStateFieldTableRows),
            "a UnitStateFieldTable row has a null step, an empty name or an omission without a reason");
    }

    std::span<const UnitStateFieldTableEntry> unitStateFieldTable()
    {
        return unitStateFieldTableRows;
    }
}
