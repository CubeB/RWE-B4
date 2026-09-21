#include "UnitStateFieldTable.h"

namespace rwe
{
    namespace
    {
        template <typename T>
        struct MemberOf;

        template <typename C, typename M>
        struct MemberOf<M C::*>
        {
            using type = M;
        };

        using nlohmann::json;

        template <auto Member>
        GameHash hashUnitStateField(const UnitState& u)
        {
            return computeHashOf(u.*Member);
        }

        template <auto Member>
        json dumpUnitStateField(const UnitState& u)
        {
            return dumpJson(u.*Member);
        }

        /** A member nlohmann likes as it stands: the numbers, bools and strings. */
        template <auto Member>
        json savePlainUnitStateField(const UnitState& u, const SaveContext&)
        {
            return u.*Member;
        }

        template <auto Member>
        void loadPlainUnitStateField(const json& j, UnitState& u, const LoadContext&)
        {
            u.*Member = j.get<typename MemberOf<decltype(Member)>::type>();
        }

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
            u.*Member = loadEnum<typename MemberOf<decltype(Member)>::type>(j);
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
    }

    const std::vector<UnitStateFieldTableEntry>& unitStateFieldTable()
    {
        static const std::vector<UnitStateFieldTableEntry> table = {
            {"unitType", hashUnitStateField<&UnitState::unitType>, savePlainUnitStateField<&UnitState::unitType>, loadPlainUnitStateField<&UnitState::unitType>, dumpUnitStateField<&UnitState::unitType>},
            {"pieces", nullptr, savePiecesUnitStateField, nullptr, nullptr},
            {"position", hashUnitStateField<&UnitState::position>, saveSimVectorUnitStateField<&UnitState::position>, loadSimVectorUnitStateField<&UnitState::position>, dumpUnitStateField<&UnitState::position>},
            {"previousPosition", nullptr, saveSimVectorUnitStateField<&UnitState::previousPosition>, loadSimVectorUnitStateField<&UnitState::previousPosition>, nullptr},
            {"cobEnvironment", nullptr, saveCobEnvironmentUnitStateField, loadCobEnvironmentUnitStateField, nullptr},
            {"owner", hashUnitStateField<&UnitState::owner>, savePlayerIdUnitStateField<&UnitState::owner>, loadPlayerIdUnitStateField<&UnitState::owner>, dumpUnitStateField<&UnitState::owner>},
            {"rotation", hashUnitStateField<&UnitState::rotation>, saveSimAngleUnitStateField<&UnitState::rotation>, loadSimAngleUnitStateField<&UnitState::rotation>, dumpUnitStateField<&UnitState::rotation>},
            {"previousRotation", nullptr, saveSimAngleUnitStateField<&UnitState::previousRotation>, loadSimAngleUnitStateField<&UnitState::previousRotation>, nullptr},
            {"physics", hashUnitStateField<&UnitState::physics>, saveUnitPhysicsInfoUnitStateField<&UnitState::physics>, loadUnitPhysicsInfoUnitStateField<&UnitState::physics>, dumpUnitStateField<&UnitState::physics>},
            {"hitPoints", hashUnitStateField<&UnitState::hitPoints>, savePlainUnitStateField<&UnitState::hitPoints>, loadPlainUnitStateField<&UnitState::hitPoints>, dumpUnitStateField<&UnitState::hitPoints>},
            {"lifeState", hashUnitStateField<&UnitState::lifeState>, saveLifeStateUnitStateField<&UnitState::lifeState>, loadLifeStateUnitStateField<&UnitState::lifeState>, dumpUnitStateField<&UnitState::lifeState>},
            {"orders", hashUnitStateField<&UnitState::orders>, saveOrdersUnitStateField, loadOrdersUnitStateField, dumpUnitStateField<&UnitState::orders>},
            {"behaviourState", hashUnitStateField<&UnitState::behaviourState>, saveUnitBehaviorStateUnitStateField<&UnitState::behaviourState>, loadUnitBehaviorStateUnitStateField<&UnitState::behaviourState>, dumpUnitStateField<&UnitState::behaviourState>},
            {"navigationState", hashUnitStateField<&UnitState::navigationState>, saveNavigationStateInfoUnitStateField<&UnitState::navigationState>, loadNavigationStateInfoUnitStateField<&UnitState::navigationState>, dumpUnitStateField<&UnitState::navigationState>},
            {"buildOrderUnitId", nullptr, saveOptionalUnitIdUnitStateField<&UnitState::buildOrderUnitId>, loadOptionalUnitIdUnitStateField<&UnitState::buildOrderUnitId>, nullptr},
            {"inBuildStance", hashUnitStateField<&UnitState::inBuildStance>, savePlainUnitStateField<&UnitState::inBuildStance>, loadPlainUnitStateField<&UnitState::inBuildStance>, dumpUnitStateField<&UnitState::inBuildStance>},
            {"armStowDueTime", hashUnitStateField<&UnitState::armStowDueTime>, saveOptionalGameTimeUnitStateField<&UnitState::armStowDueTime>, loadOptionalGameTimeUnitStateField<&UnitState::armStowDueTime>, dumpUnitStateField<&UnitState::armStowDueTime>},
            {"nanoPointQueriedAt", hashUnitStateField<&UnitState::nanoPointQueriedAt>, saveOptionalGameTimeUnitStateField<&UnitState::nanoPointQueriedAt>, loadOptionalGameTimeUnitStateField<&UnitState::nanoPointQueriedAt>, dumpUnitStateField<&UnitState::nanoPointQueriedAt>},
            {"nanoPoint", hashUnitStateField<&UnitState::nanoPoint>, saveSimVectorUnitStateField<&UnitState::nanoPoint>, loadSimVectorUnitStateField<&UnitState::nanoPoint>, dumpUnitStateField<&UnitState::nanoPoint>},
            {"commandFireShotFired", nullptr, savePlainUnitStateField<&UnitState::commandFireShotFired>, loadLegacyBoolUnitStateField<&UnitState::commandFireShotFired>, nullptr, true},
            {"yardOpen", hashUnitStateField<&UnitState::yardOpen>, savePlainUnitStateField<&UnitState::yardOpen>, loadPlainUnitStateField<&UnitState::yardOpen>, dumpUnitStateField<&UnitState::yardOpen>},
            {"inCollision", hashUnitStateField<&UnitState::inCollision>, savePlainUnitStateField<&UnitState::inCollision>, loadPlainUnitStateField<&UnitState::inCollision>, dumpUnitStateField<&UnitState::inCollision>},
            {"weapons", hashUnitStateField<&UnitState::weapons>, saveWeaponsUnitStateField, loadWeaponsUnitStateField, dumpUnitStateField<&UnitState::weapons>},
            {"fireOrders", hashUnitStateField<&UnitState::fireOrders>, saveEnumUnitStateField<&UnitState::fireOrders>, loadEnumUnitStateField<&UnitState::fireOrders>, dumpUnitStateField<&UnitState::fireOrders>},
            {"moveOrders", hashUnitStateField<&UnitState::moveOrders>, saveEnumUnitStateField<&UnitState::moveOrders>, loadEnumUnitStateField<&UnitState::moveOrders>, dumpUnitStateField<&UnitState::moveOrders>},
            {"cobBusy", hashUnitStateField<&UnitState::cobBusy>, savePlainUnitStateField<&UnitState::cobBusy>, loadPlainUnitStateField<&UnitState::cobBusy>, dumpUnitStateField<&UnitState::cobBusy>},
            {"buggerOffActive", hashUnitStateField<&UnitState::buggerOffActive>, savePlainUnitStateField<&UnitState::buggerOffActive>, loadPlainUnitStateField<&UnitState::buggerOffActive>, dumpUnitStateField<&UnitState::buggerOffActive>},
            {"armored", hashUnitStateField<&UnitState::armored>, savePlainUnitStateField<&UnitState::armored>, loadPlainUnitStateField<&UnitState::armored>, dumpUnitStateField<&UnitState::armored>},
            {"kills", hashUnitStateField<&UnitState::kills>, savePlainUnitStateField<&UnitState::kills>, loadPlainUnitStateField<&UnitState::kills>, dumpUnitStateField<&UnitState::kills>},
            {"sfxOccupyState", hashUnitStateField<&UnitState::sfxOccupyState>, savePlainUnitStateField<&UnitState::sfxOccupyState>, loadPlainUnitStateField<&UnitState::sfxOccupyState>, dumpUnitStateField<&UnitState::sfxOccupyState>},
            {"buildTimeCompleted", hashUnitStateField<&UnitState::buildTimeCompleted>, savePlainUnitStateField<&UnitState::buildTimeCompleted>, loadPlainUnitStateField<&UnitState::buildTimeCompleted>, dumpUnitStateField<&UnitState::buildTimeCompleted>},
            {"nanoframeDecayTime", hashUnitStateField<&UnitState::nanoframeDecayTime>, saveOptionalGameTimeUnitStateField<&UnitState::nanoframeDecayTime>, loadOptionalGameTimeUnitStateField<&UnitState::nanoframeDecayTime>, dumpUnitStateField<&UnitState::nanoframeDecayTime>},
            {"nanoframeWorkedOn", hashUnitStateField<&UnitState::nanoframeWorkedOn>, savePlainUnitStateField<&UnitState::nanoframeWorkedOn>, loadPlainUnitStateField<&UnitState::nanoframeWorkedOn>, dumpUnitStateField<&UnitState::nanoframeWorkedOn>},
            {"nanoframeDecayRemainder", hashUnitStateField<&UnitState::nanoframeDecayRemainder>, savePlainUnitStateField<&UnitState::nanoframeDecayRemainder>, loadPlainUnitStateField<&UnitState::nanoframeDecayRemainder>, dumpUnitStateField<&UnitState::nanoframeDecayRemainder>},
            {"reclaimProgress", hashUnitStateField<&UnitState::reclaimProgress>, savePlainUnitStateField<&UnitState::reclaimProgress>, loadPlainUnitStateField<&UnitState::reclaimProgress>, dumpUnitStateField<&UnitState::reclaimProgress>},
            {"selfDestructTime", hashUnitStateField<&UnitState::selfDestructTime>, saveOptionalGameTimeUnitStateField<&UnitState::selfDestructTime>, loadOptionalGameTimeUnitStateField<&UnitState::selfDestructTime>, dumpUnitStateField<&UnitState::selfDestructTime>},
            {"paralyzedUntil", hashUnitStateField<&UnitState::paralyzedUntil>, saveOptionalGameTimeUnitStateField<&UnitState::paralyzedUntil>, loadOptionalGameTimeUnitStateField<&UnitState::paralyzedUntil>, dumpUnitStateField<&UnitState::paralyzedUntil>},
            {"moveRateBand", hashUnitStateField<&UnitState::moveRateBand>, savePlainUnitStateField<&UnitState::moveRateBand>, loadPlainUnitStateField<&UnitState::moveRateBand>, dumpUnitStateField<&UnitState::moveRateBand>},
            {"carriedBy", hashUnitStateField<&UnitState::carriedBy>, saveOptionalUnitIdUnitStateField<&UnitState::carriedBy>, loadOptionalUnitIdUnitStateField<&UnitState::carriedBy>, dumpUnitStateField<&UnitState::carriedBy>},
            {"carriedPiece", nullptr, savePlainUnitStateField<&UnitState::carriedPiece>, loadPlainUnitStateField<&UnitState::carriedPiece>, nullptr},
            {"carriedUnits", nullptr, saveCarriedUnitsUnitStateField, loadCarriedUnitsUnitStateField, nullptr},
            {"transportScriptTarget", hashUnitStateField<&UnitState::transportScriptTarget>, saveOptionalUnitIdUnitStateField<&UnitState::transportScriptTarget>, loadOptionalUnitIdUnitStateField<&UnitState::transportScriptTarget>, dumpUnitStateField<&UnitState::transportScriptTarget>},
            {"transportScriptStartedAt", hashUnitStateField<&UnitState::transportScriptStartedAt>, saveGameTimeUnitStateField<&UnitState::transportScriptStartedAt>, loadGameTimeUnitStateField<&UnitState::transportScriptStartedAt>, dumpUnitStateField<&UnitState::transportScriptStartedAt>},
            {"airWorkOrbit", hashUnitStateField<&UnitState::airWorkOrbit>, saveOptionalAirWorkOrbitUnitStateField<&UnitState::airWorkOrbit>, loadOptionalAirWorkOrbitUnitStateField<&UnitState::airWorkOrbit>, dumpUnitStateField<&UnitState::airWorkOrbit>},
            {"airLoiter", hashUnitStateField<&UnitState::airLoiter>, saveOptionalAirLoiterUnitStateField<&UnitState::airLoiter>, loadOptionalAirLoiterUnitStateField<&UnitState::airLoiter>, dumpUnitStateField<&UnitState::airLoiter>},
            {"slowFacePoint", hashUnitStateField<&UnitState::slowFacePoint>, saveOptionalSimVectorUnitStateField<&UnitState::slowFacePoint>, loadOptionalSimVectorUnitStateField<&UnitState::slowFacePoint>, dumpUnitStateField<&UnitState::slowFacePoint>},
            {"activated", hashUnitStateField<&UnitState::activated>, savePlainUnitStateField<&UnitState::activated>, loadPlainUnitStateField<&UnitState::activated>, dumpUnitStateField<&UnitState::activated>},
            {"isSufficientlyPowered", hashUnitStateField<&UnitState::isSufficientlyPowered>, savePlainUnitStateField<&UnitState::isSufficientlyPowered>, loadPlainUnitStateField<&UnitState::isSufficientlyPowered>, dumpUnitStateField<&UnitState::isSufficientlyPowered>},
            {"cloakRequested", hashUnitStateField<&UnitState::cloakRequested>, savePlainUnitStateField<&UnitState::cloakRequested>, loadPlainUnitStateField<&UnitState::cloakRequested>, dumpUnitStateField<&UnitState::cloakRequested>},
            {"cloaked", hashUnitStateField<&UnitState::cloaked>, savePlainUnitStateField<&UnitState::cloaked>, loadPlainUnitStateField<&UnitState::cloaked>, dumpUnitStateField<&UnitState::cloaked>},
            {"cloakSuppressedUntil", hashUnitStateField<&UnitState::cloakSuppressedUntil>, saveGameTimeUnitStateField<&UnitState::cloakSuppressedUntil>, loadGameTimeUnitStateField<&UnitState::cloakSuppressedUntil>, dumpUnitStateField<&UnitState::cloakSuppressedUntil>},
            {"energyProductionBuffer", hashUnitStateField<&UnitState::energyProductionBuffer>, saveEnergyUnitStateField<&UnitState::energyProductionBuffer>, loadEnergyUnitStateField<&UnitState::energyProductionBuffer>, dumpUnitStateField<&UnitState::energyProductionBuffer>},
            {"metalProductionBuffer", hashUnitStateField<&UnitState::metalProductionBuffer>, saveMetalUnitStateField<&UnitState::metalProductionBuffer>, loadMetalUnitStateField<&UnitState::metalProductionBuffer>, dumpUnitStateField<&UnitState::metalProductionBuffer>},
            {"previousEnergyProductionBuffer", hashUnitStateField<&UnitState::previousEnergyProductionBuffer>, saveEnergyUnitStateField<&UnitState::previousEnergyProductionBuffer>, loadEnergyUnitStateField<&UnitState::previousEnergyProductionBuffer>, dumpUnitStateField<&UnitState::previousEnergyProductionBuffer>},
            {"previousMetalProductionBuffer", hashUnitStateField<&UnitState::previousMetalProductionBuffer>, saveMetalUnitStateField<&UnitState::previousMetalProductionBuffer>, loadMetalUnitStateField<&UnitState::previousMetalProductionBuffer>, dumpUnitStateField<&UnitState::previousMetalProductionBuffer>},
            {"previousEnergyConsumptionBuffer", hashUnitStateField<&UnitState::previousEnergyConsumptionBuffer>, saveEnergyUnitStateField<&UnitState::previousEnergyConsumptionBuffer>, loadEnergyUnitStateField<&UnitState::previousEnergyConsumptionBuffer>, dumpUnitStateField<&UnitState::previousEnergyConsumptionBuffer>},
            {"previousMetalConsumptionBuffer", hashUnitStateField<&UnitState::previousMetalConsumptionBuffer>, saveMetalUnitStateField<&UnitState::previousMetalConsumptionBuffer>, loadMetalUnitStateField<&UnitState::previousMetalConsumptionBuffer>, dumpUnitStateField<&UnitState::previousMetalConsumptionBuffer>},
            {"energyConsumptionBuffer", hashUnitStateField<&UnitState::energyConsumptionBuffer>, saveEnergyUnitStateField<&UnitState::energyConsumptionBuffer>, loadEnergyUnitStateField<&UnitState::energyConsumptionBuffer>, dumpUnitStateField<&UnitState::energyConsumptionBuffer>},
            {"metalConsumptionBuffer", hashUnitStateField<&UnitState::metalConsumptionBuffer>, saveMetalUnitStateField<&UnitState::metalConsumptionBuffer>, loadMetalUnitStateField<&UnitState::metalConsumptionBuffer>, dumpUnitStateField<&UnitState::metalConsumptionBuffer>},
            {"energyRequestBuffer", hashUnitStateField<&UnitState::energyRequestBuffer>, saveEnergyUnitStateField<&UnitState::energyRequestBuffer>, loadEnergyUnitStateField<&UnitState::energyRequestBuffer>, dumpUnitStateField<&UnitState::energyRequestBuffer>},
            {"metalRequestBuffer", hashUnitStateField<&UnitState::metalRequestBuffer>, saveMetalUnitStateField<&UnitState::metalRequestBuffer>, loadMetalUnitStateField<&UnitState::metalRequestBuffer>, dumpUnitStateField<&UnitState::metalRequestBuffer>},
            {"energyDebt", hashUnitStateField<&UnitState::energyDebt>, saveEnergyUnitStateField<&UnitState::energyDebt>, loadEnergyUnitStateField<&UnitState::energyDebt>, dumpUnitStateField<&UnitState::energyDebt>},
            {"metalDebt", hashUnitStateField<&UnitState::metalDebt>, saveMetalUnitStateField<&UnitState::metalDebt>, loadMetalUnitStateField<&UnitState::metalDebt>, dumpUnitStateField<&UnitState::metalDebt>},
            {"buildQueue", nullptr, saveBuildQueueUnitStateField, loadBuildQueueUnitStateField, nullptr},
            {"factoryState", nullptr, saveFactoryBehaviorStateUnitStateField<&UnitState::factoryState>, loadFactoryBehaviorStateUnitStateField<&UnitState::factoryState>, nullptr},
        };
        return table;
    }
}
