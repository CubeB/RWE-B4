#include "save_util.h"

#include <algorithm>
#include <cstring>
#include <rwe/util/match.h>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace rwe
{
    namespace
    {
        using nlohmann::json;

        // ---- primitives -------------------------------------------------
        //
        // Floats are stored as their IEEE bit pattern, not as json numbers:
        // a decimal round-trip is allowed to land on a neighbouring value,
        // and a neighbouring value is a different game.

        json saveFloat(float f)
        {
            uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            return bits;
        }

        float loadFloat(const json& j)
        {
            auto bits = j.get<uint32_t>();
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return f;
        }

        json saveSimScalar(SimScalar s)
        {
            return saveFloat(s.value);
        }

        SimScalar loadSimScalar(const json& j)
        {
            return SimScalar(loadFloat(j));
        }

        json saveEnergy(Energy e)
        {
            return saveFloat(e.value);
        }

        Energy loadEnergy(const json& j)
        {
            return Energy(loadFloat(j));
        }

        json saveMetal(Metal m)
        {
            return saveFloat(m.value);
        }

        Metal loadMetal(const json& j)
        {
            return Metal(loadFloat(j));
        }

        json saveSimVector(const SimVector& v)
        {
            return json::array({saveFloat(v.x.value), saveFloat(v.y.value), saveFloat(v.z.value)});
        }

        SimVector loadSimVector(const json& j)
        {
            return SimVector(SimScalar(loadFloat(j.at(0))), SimScalar(loadFloat(j.at(1))), SimScalar(loadFloat(j.at(2))));
        }

        json saveSimAngle(SimAngle a)
        {
            return a.value;
        }

        SimAngle loadSimAngle(const json& j)
        {
            return SimAngle(j.get<uint16_t>());
        }

        json saveGameTime(GameTime t)
        {
            return t.value;
        }

        GameTime loadGameTime(const json& j)
        {
            return GameTime(j.get<unsigned int>());
        }

        template <typename T, typename F>
        json saveOptional(const std::optional<T>& o, F f)
        {
            return o ? json(f(*o)) : json();
        }

        template <typename F>
        auto loadOptional(const json& j, F f) -> std::optional<decltype(f(j))>
        {
            if (j.is_null())
            {
                return std::nullopt;
            }
            return f(j);
        }

        template <typename E>
        json saveEnum(E e)
        {
            return static_cast<std::underlying_type_t<E>>(e);
        }

        template <typename E>
        E loadEnum(const json& j)
        {
            return static_cast<E>(j.get<std::underlying_type_t<E>>());
        }

        // ---- id remapping -----------------------------------------------
        //
        // VectorMap ids carry slot and generation, and a freshly built sim
        // hands them out densely in insertion order, so a saved id is
        // meaningless on its own. The save rewrites every reference as the
        // dense index of its target in the saved array; the load records the
        // real id the fresh sim hands out for each index and resolves
        // references through that table.
        //
        // A reference whose target no longer existed at save time is written
        // as "stale" and restored as an id whose generation can never be in
        // use, so lookups fail on it just as they failed on the original.

        constexpr const char* StaleRef = "stale";
        constexpr unsigned int StaleIdValue = 0xFFu;

        struct SaveContext
        {
            std::unordered_map<UnitId, uint32_t> units;
            std::unordered_map<FeatureId, uint32_t> features;
            std::unordered_map<ProjectileId, uint32_t> projectiles;
        };

        struct LoadContext
        {
            std::vector<UnitId> units;
            std::vector<FeatureId> features;
            std::vector<ProjectileId> projectiles;
        };

        template <typename Id>
        json saveIdRef(Id id, const std::unordered_map<Id, uint32_t>& table)
        {
            auto it = table.find(id);
            if (it == table.end())
            {
                return StaleRef;
            }
            return it->second;
        }

        template <typename Id>
        Id loadIdRef(const json& j, const std::vector<Id>& table)
        {
            if (j.is_string())
            {
                return Id(StaleIdValue);
            }
            return table.at(j.get<uint32_t>());
        }

        json saveUnitIdRef(UnitId id, const SaveContext& ctx)
        {
            return saveIdRef(id, ctx.units);
        }

        UnitId loadUnitIdRef(const json& j, const LoadContext& ctx)
        {
            return loadIdRef(j, ctx.units);
        }

        json saveFeatureIdRef(FeatureId id, const SaveContext& ctx)
        {
            return saveIdRef(id, ctx.features);
        }

        FeatureId loadFeatureIdRef(const json& j, const LoadContext& ctx)
        {
            return loadIdRef(j, ctx.features);
        }

        json saveProjectileIdRef(ProjectileId id, const SaveContext& ctx)
        {
            return saveIdRef(id, ctx.projectiles);
        }

        ProjectileId loadProjectileIdRef(const json& j, const LoadContext& ctx)
        {
            return loadIdRef(j, ctx.projectiles);
        }

        // ---- small shared structs ---------------------------------------

        json saveDiscreteRect(const DiscreteRect& r)
        {
            return json{
                {"x", r.x},
                {"y", r.y},
                {"width", r.width},
                {"height", r.height}};
        }

        DiscreteRect loadDiscreteRect(const json& j)
        {
            return DiscreteRect(j.at("x").get<int>(), j.at("y").get<int>(), j.at("width").get<int>(), j.at("height").get<int>());
        }

        json saveWinStatus(const WinStatus& s)
        {
            return match(
                s,
                [](const WinStatusWon& w) { return json{{"kind", "won"}, {"winner", w.winner.value}}; },
                [](const WinStatusDraw&) { return json{{"kind", "draw"}}; },
                [](const WinStatusUndecided&) { return json{{"kind", "undecided"}}; });
        }

        WinStatus loadWinStatus(const json& j)
        {
            const auto& kind = j.at("kind").get_ref<const std::string&>();
            if (kind == "won")
            {
                return WinStatusWon{PlayerId(j.at("winner").get<unsigned int>())};
            }
            if (kind == "draw")
            {
                return WinStatusDraw();
            }
            if (kind == "undecided")
            {
                return WinStatusUndecided();
            }
            throw std::runtime_error("bad WinStatus kind: " + kind);
        }

        // ---- players ----------------------------------------------------

        json saveGamePlayerInfo(const GamePlayerInfo& p)
        {
            return json{
                {"name", saveOptional(p.name, [](const std::string& s) { return json(s); })},
                {"type", saveEnum(p.type)},
                {"color", p.color.value},
                {"status", saveEnum(p.status)},
                {"side", p.side},
                {"teamId", saveOptional(p.teamId, [](int v) { return json(v); })},
                {"metal", saveMetal(p.metal)},
                {"energy", saveEnergy(p.energy)},
                {"maxMetal", saveMetal(p.maxMetal)},
                {"maxEnergy", saveEnergy(p.maxEnergy)},
                {"startingMetal", saveMetal(p.startingMetal)},
                {"startingEnergy", saveEnergy(p.startingEnergy)},
                {"metalStalled", p.metalStalled},
                {"energyStalled", p.energyStalled},
                {"unitsKilled", p.unitsKilled},
                {"unitsLost", p.unitsLost},
                {"desiredMetalConsumptionBuffer", saveMetal(p.desiredMetalConsumptionBuffer)},
                {"desiredEnergyConsumptionBuffer", saveEnergy(p.desiredEnergyConsumptionBuffer)},
                {"previousDesiredMetalConsumptionBuffer", saveMetal(p.previousDesiredMetalConsumptionBuffer)},
                {"previousDesiredEnergyConsumptionBuffer", saveEnergy(p.previousDesiredEnergyConsumptionBuffer)},
                {"metalProductionBuffer", saveMetal(p.metalProductionBuffer)},
                {"energyProductionBuffer", saveEnergy(p.energyProductionBuffer)},
                {"previousMetalProductionBuffer", saveMetal(p.previousMetalProductionBuffer)},
                {"previousEnergyProductionBuffer", saveEnergy(p.previousEnergyProductionBuffer)},
                {"metalRequestBuffer", saveMetal(p.metalRequestBuffer)},
                {"energyRequestBuffer", saveEnergy(p.energyRequestBuffer)},
                {"metalDebt", saveMetal(p.metalDebt)},
                {"energyDebt", saveEnergy(p.energyDebt)}};
        }

        GamePlayerInfo loadGamePlayerInfo(const json& j)
        {
            GamePlayerInfo p{
                loadOptional(j.at("name"), [](const json& v) { return v.get<std::string>(); }),
                loadEnum<GamePlayerType>(j.at("type")),
                PlayerColorIndex(j.at("color").get<unsigned int>()),
                loadEnum<GamePlayerStatus>(j.at("status")),
                j.at("side").get<std::string>(),
                loadMetal(j.at("metal")),
                loadEnergy(j.at("energy")),
                loadMetal(j.at("maxMetal")),
                loadEnergy(j.at("maxEnergy")),
                loadMetal(j.at("startingMetal")),
                loadEnergy(j.at("startingEnergy")),
                j.contains("teamId") ? loadOptional(j.at("teamId"), [](const json& v) { return v.get<int>(); }) : std::optional<int>()};
            p.metalStalled = j.at("metalStalled").get<bool>();
            p.energyStalled = j.at("energyStalled").get<bool>();
            p.unitsKilled = j.at("unitsKilled").get<unsigned int>();
            p.unitsLost = j.at("unitsLost").get<unsigned int>();
            p.desiredMetalConsumptionBuffer = loadMetal(j.at("desiredMetalConsumptionBuffer"));
            p.desiredEnergyConsumptionBuffer = loadEnergy(j.at("desiredEnergyConsumptionBuffer"));
            p.previousDesiredMetalConsumptionBuffer = loadMetal(j.at("previousDesiredMetalConsumptionBuffer"));
            p.previousDesiredEnergyConsumptionBuffer = loadEnergy(j.at("previousDesiredEnergyConsumptionBuffer"));
            p.metalProductionBuffer = loadMetal(j.at("metalProductionBuffer"));
            p.energyProductionBuffer = loadEnergy(j.at("energyProductionBuffer"));
            p.previousMetalProductionBuffer = loadMetal(j.at("previousMetalProductionBuffer"));
            p.previousEnergyProductionBuffer = loadEnergy(j.at("previousEnergyProductionBuffer"));
            p.metalRequestBuffer = loadMetal(j.at("metalRequestBuffer"));
            p.energyRequestBuffer = loadEnergy(j.at("energyRequestBuffer"));
            p.metalDebt = loadMetal(j.at("metalDebt"));
            p.energyDebt = loadEnergy(j.at("energyDebt"));
            return p;
        }

        // ---- features ---------------------------------------------------

        json saveMapFeature(const MapFeature& f)
        {
            return json{
                {"featureName", f.featureName.value},
                {"position", saveSimVector(f.position)},
                {"rotation", saveSimAngle(f.rotation)},
                {"velocity", saveSimVector(f.velocity)},
                {"reclaimProgress", f.reclaimProgress},
                {"hitPoints", f.hitPoints},
                {"burningUntil", saveOptional(f.burningUntil, [](GameTime t) { return saveGameTime(t); })},
                {"nextSpark", saveGameTime(f.nextSpark)}};
        }

        // ---- unit pieces ------------------------------------------------

        json saveMoveOperation(const UnitMesh::MoveOperation& op)
        {
            return json{
                {"targetPosition", saveSimScalar(op.targetPosition)},
                {"speed", saveSimScalar(op.speed)}};
        }

        UnitMesh::MoveOperation loadMoveOperation(const json& j)
        {
            return UnitMesh::MoveOperation(loadSimScalar(j.at("targetPosition")), loadSimScalar(j.at("speed")));
        }

        json saveTurnOperationUnion(const UnitMesh::TurnOperationUnion& op)
        {
            return match(
                op,
                [](const UnitMesh::TurnOperation& t) {
                    return json{
                        {"kind", "turn"},
                        {"targetAngle", saveSimAngle(t.targetAngle)},
                        {"speed", saveSimScalar(t.speed)}};
                },
                [](const UnitMesh::SpinOperation& s) {
                    return json{
                        {"kind", "spin"},
                        {"currentSpeed", saveSimScalar(s.currentSpeed)},
                        {"targetSpeed", saveSimScalar(s.targetSpeed)},
                        {"acceleration", saveSimScalar(s.acceleration)}};
                },
                [](const UnitMesh::StopSpinOperation& s) {
                    return json{
                        {"kind", "stopSpin"},
                        {"currentSpeed", saveSimScalar(s.currentSpeed)},
                        {"deceleration", saveSimScalar(s.deceleration)}};
                });
        }

        UnitMesh::TurnOperationUnion loadTurnOperationUnion(const json& j)
        {
            const auto& kind = j.at("kind").get_ref<const std::string&>();
            if (kind == "turn")
            {
                return UnitMesh::TurnOperation(loadSimAngle(j.at("targetAngle")), loadSimScalar(j.at("speed")));
            }
            if (kind == "spin")
            {
                return UnitMesh::SpinOperation(loadSimScalar(j.at("currentSpeed")), loadSimScalar(j.at("targetSpeed")), loadSimScalar(j.at("acceleration")));
            }
            if (kind == "stopSpin")
            {
                return UnitMesh::StopSpinOperation(loadSimScalar(j.at("currentSpeed")), loadSimScalar(j.at("deceleration")));
            }
            throw std::runtime_error("bad TurnOperationUnion kind: " + kind);
        }

        json saveUnitMesh(const UnitMesh& m)
        {
            return json{
                {"name", m.name},
                {"visible", m.visible},
                {"shaded", m.shaded},
                {"offset", saveSimVector(m.offset)},
                {"previousOffset", saveSimVector(m.previousOffset)},
                {"previousRotationX", saveSimAngle(m.previousRotationX)},
                {"previousRotationY", saveSimAngle(m.previousRotationY)},
                {"previousRotationZ", saveSimAngle(m.previousRotationZ)},
                {"rotationX", saveSimAngle(m.rotationX)},
                {"rotationY", saveSimAngle(m.rotationY)},
                {"rotationZ", saveSimAngle(m.rotationZ)},
                {"xMoveOperation", saveOptional(m.xMoveOperation, saveMoveOperation)},
                {"yMoveOperation", saveOptional(m.yMoveOperation, saveMoveOperation)},
                {"zMoveOperation", saveOptional(m.zMoveOperation, saveMoveOperation)},
                {"xTurnOperation", saveOptional(m.xTurnOperation, saveTurnOperationUnion)},
                {"yTurnOperation", saveOptional(m.yTurnOperation, saveTurnOperationUnion)},
                {"zTurnOperation", saveOptional(m.zTurnOperation, saveTurnOperationUnion)}};
        }

        UnitMesh loadUnitMesh(const json& j)
        {
            UnitMesh m;
            m.name = j.at("name").get<std::string>();
            m.visible = j.at("visible").get<bool>();
            m.shaded = j.at("shaded").get<bool>();
            m.offset = loadSimVector(j.at("offset"));
            m.previousOffset = loadSimVector(j.at("previousOffset"));
            m.previousRotationX = loadSimAngle(j.at("previousRotationX"));
            m.previousRotationY = loadSimAngle(j.at("previousRotationY"));
            m.previousRotationZ = loadSimAngle(j.at("previousRotationZ"));
            m.rotationX = loadSimAngle(j.at("rotationX"));
            m.rotationY = loadSimAngle(j.at("rotationY"));
            m.rotationZ = loadSimAngle(j.at("rotationZ"));
            m.xMoveOperation = loadOptional(j.at("xMoveOperation"), loadMoveOperation);
            m.yMoveOperation = loadOptional(j.at("yMoveOperation"), loadMoveOperation);
            m.zMoveOperation = loadOptional(j.at("zMoveOperation"), loadMoveOperation);
            m.xTurnOperation = loadOptional(j.at("xTurnOperation"), loadTurnOperationUnion);
            m.yTurnOperation = loadOptional(j.at("yTurnOperation"), loadTurnOperationUnion);
            m.zTurnOperation = loadOptional(j.at("zTurnOperation"), loadTurnOperationUnion);
            return m;
        }

        // ---- the COB virtual machine ------------------------------------

        template <typename T>
        std::vector<T> stackToVector(const std::stack<T>& s)
        {
            // std::stack exposes only the top, so drain a copy and flip it:
            // the result runs bottom first, ready to be pushed back in order.
            std::vector<T> result;
            auto copy = s;
            while (!copy.empty())
            {
                result.push_back(copy.top());
                copy.pop();
            }
            std::reverse(result.begin(), result.end());
            return result;
        }

        json saveCobFunction(const CobFunction& f)
        {
            return json{
                {"instructionIndex", f.instructionIndex},
                {"locals", f.locals},
                {"localCount", f.localCount}};
        }

        CobFunction loadCobFunction(const json& j)
        {
            CobFunction f(j.at("instructionIndex").get<unsigned int>());
            f.locals = j.at("locals").get<std::vector<int>>();
            f.localCount = j.at("localCount").get<unsigned int>();
            return f;
        }

        json saveCobThread(const CobThread& t)
        {
            json callStack = json::array();
            for (const auto& f : stackToVector(t.callStack))
            {
                callStack.push_back(saveCobFunction(f));
            }
            return json{
                {"name", t.name},
                {"stack", stackToVector(t.stack)},
                {"signalMask", t.signalMask},
                {"callStack", callStack},
                {"returnValue", t.returnValue},
                {"returnLocals", t.returnLocals}};
        }

        std::unique_ptr<CobThread> loadCobThread(const json& j)
        {
            auto t = std::make_unique<CobThread>(j.at("name").get<std::string>(), j.at("signalMask").get<unsigned int>());
            for (const auto& v : j.at("stack").get<std::vector<int>>())
            {
                t->stack.push(v);
            }
            for (const auto& fj : j.at("callStack"))
            {
                t->callStack.push(loadCobFunction(fj));
            }
            t->returnValue = j.at("returnValue").get<int>();
            t->returnLocals = j.at("returnLocals").get<std::vector<int>>();
            return t;
        }

        json saveBlockedCondition(const CobEnvironment::BlockedStatus::Condition& c)
        {
            return match(
                c,
                [](const CobEnvironment::BlockedStatus::Move& m) {
                    return json{{"kind", "move"}, {"object", m.object}, {"axis", saveEnum(m.axis)}};
                },
                [](const CobEnvironment::BlockedStatus::Turn& t) {
                    return json{{"kind", "turn"}, {"object", t.object}, {"axis", saveEnum(t.axis)}};
                });
        }

        CobEnvironment::BlockedStatus::Condition loadBlockedCondition(const json& j)
        {
            const auto& kind = j.at("kind").get_ref<const std::string&>();
            auto object = j.at("object").get<unsigned int>();
            auto axis = loadEnum<CobAxis>(j.at("axis"));
            if (kind == "move")
            {
                return CobEnvironment::BlockedStatus::Move(object, axis);
            }
            if (kind == "turn")
            {
                return CobEnvironment::BlockedStatus::Turn(object, axis);
            }
            throw std::runtime_error("bad BlockedStatus kind: " + kind);
        }

        /**
         * The index of a thread in the environment's threads vector, or null
         * for a pointer that no longer names a live thread. The scheduler
         * queues and a weapon's aim state both hold raw CobThread pointers,
         * which is why the whole VM serializes through these indices.
         */
        json saveCobThreadRef(const CobEnvironment& env, const CobThread* thread)
        {
            for (std::size_t i = 0; i < env.threads.size(); ++i)
            {
                if (env.threads[i].get() == thread)
                {
                    return i;
                }
            }
            return json();
        }

        const CobThread* loadCobThreadRef(const json& j, const CobEnvironment& env)
        {
            if (j.is_null())
            {
                return nullptr;
            }
            return env.threads.at(j.get<std::size_t>()).get();
        }

        json saveCobEnvironment(const CobEnvironment& env)
        {
            json threads = json::array();
            for (const auto& t : env.threads)
            {
                threads.push_back(saveCobThread(*t));
            }

            json readyQueue = json::array();
            for (const auto* t : env.readyQueue)
            {
                readyQueue.push_back(saveCobThreadRef(env, t));
            }

            json blockedQueue = json::array();
            for (const auto& [status, t] : env.blockedQueue)
            {
                blockedQueue.push_back(json{
                    {"condition", saveBlockedCondition(status.condition)},
                    {"thread", saveCobThreadRef(env, t)}});
            }

            json sleepingQueue = json::array();
            for (const auto& [wakeTime, t] : env.sleepingQueue)
            {
                sleepingQueue.push_back(json{
                    {"wakeTime", wakeTime.value},
                    {"thread", saveCobThreadRef(env, t)}});
            }

            json finishedQueue = json::array();
            for (const auto* t : env.finishedQueue)
            {
                finishedQueue.push_back(saveCobThreadRef(env, t));
            }

            return json{
                {"statics", env._statics},
                {"threads", threads},
                {"readyQueue", readyQueue},
                {"blockedQueue", blockedQueue},
                {"sleepingQueue", sleepingQueue},
                {"finishedQueue", finishedQueue}};
        }

        void loadCobEnvironmentInto(const json& j, CobEnvironment& env)
        {
            env._statics = j.at("statics").get<std::vector<int>>();

            env.threads.clear();
            env.readyQueue.clear();
            env.blockedQueue.clear();
            env.sleepingQueue.clear();
            env.finishedQueue.clear();

            for (const auto& tj : j.at("threads"))
            {
                env.threads.push_back(loadCobThread(tj));
            }

            for (const auto& r : j.at("readyQueue"))
            {
                env.readyQueue.push_back(const_cast<CobThread*>(loadCobThreadRef(r, env)));
            }
            for (const auto& b : j.at("blockedQueue"))
            {
                env.blockedQueue.emplace_back(
                    CobEnvironment::BlockedStatus(loadBlockedCondition(b.at("condition"))),
                    const_cast<CobThread*>(loadCobThreadRef(b.at("thread"), env)));
            }
            for (const auto& s : j.at("sleepingQueue"))
            {
                env.sleepingQueue.emplace_back(
                    CobTime(s.at("wakeTime").get<int>()),
                    const_cast<CobThread*>(loadCobThreadRef(s.at("thread"), env)));
            }
            for (const auto& f : j.at("finishedQueue"))
            {
                env.finishedQueue.push_back(const_cast<CobThread*>(loadCobThreadRef(f, env)));
            }
        }

        // ---- weapons ----------------------------------------------------

        json saveUnitWeaponAttackTarget(const UnitWeaponAttackTarget& t, const SaveContext& ctx)
        {
            return match(
                t,
                [&](const UnitId& id) { return json{{"kind", "unit"}, {"target", saveUnitIdRef(id, ctx)}}; },
                [&](const SimVector& v) { return json{{"kind", "position"}, {"target", saveSimVector(v)}}; },
                [&](const ProjectileId& id) { return json{{"kind", "projectile"}, {"target", saveProjectileIdRef(id, ctx)}}; });
        }

        UnitWeaponAttackTarget loadUnitWeaponAttackTarget(const json& j, const LoadContext& ctx)
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
            if (kind == "projectile")
            {
                return loadProjectileIdRef(j.at("target"), ctx);
            }
            throw std::runtime_error("bad UnitWeaponAttackTarget kind: " + kind);
        }

        json saveUnitWeaponState(const UnitWeaponState& s, const SaveContext& ctx, const CobEnvironment& env)
        {
            return match(
                s,
                [](const UnitWeaponStateIdle&) { return json{{"kind", "idle"}}; },
                [&](const UnitWeaponStateAttacking& a) {
                    auto attackInfo = match(
                        a.attackInfo,
                        [](const UnitWeaponStateAttacking::IdleInfo&) { return json{{"kind", "idle"}}; },
                        [&](const UnitWeaponStateAttacking::AimInfo& i) {
                            return json{
                                {"kind", "aim"},
                                {"thread", saveCobThreadRef(env, i.thread)},
                                {"lastHeading", saveSimAngle(i.lastHeading)},
                                {"lastPitch", saveSimAngle(i.lastPitch)}};
                        },
                        [](const UnitWeaponStateAttacking::FireInfo& i) {
                            return json{
                                {"kind", "fire"},
                                {"heading", saveSimAngle(i.heading)},
                                {"pitch", saveSimAngle(i.pitch)},
                                {"targetPosition", saveSimVector(i.targetPosition)},
                                {"firingPiece", saveOptional(i.firingPiece, [](int p) { return json(p); })},
                                {"burstsFired", i.burstsFired},
                                {"readyTime", saveGameTime(i.readyTime)}};
                        });
                    return json{
                        {"kind", "attacking"},
                        {"target", saveUnitWeaponAttackTarget(a.target, ctx)},
                        {"attackInfo", attackInfo}};
                });
        }

        UnitWeaponState loadUnitWeaponState(const json& j, const LoadContext& ctx, const CobEnvironment& env)
        {
            const auto& kind = j.at("kind").get_ref<const std::string&>();
            if (kind == "idle")
            {
                return UnitWeaponStateIdle();
            }
            if (kind == "attacking")
            {
                UnitWeaponStateAttacking a(loadUnitWeaponAttackTarget(j.at("target"), ctx));
                const auto& ij = j.at("attackInfo");
                const auto& infoKind = ij.at("kind").get_ref<const std::string&>();
                if (infoKind == "idle")
                {
                    a.attackInfo = UnitWeaponStateAttacking::IdleInfo();
                }
                else if (infoKind == "aim")
                {
                    a.attackInfo = UnitWeaponStateAttacking::AimInfo{
                        loadCobThreadRef(ij.at("thread"), env),
                        loadSimAngle(ij.at("lastHeading")),
                        loadSimAngle(ij.at("lastPitch"))};
                }
                else if (infoKind == "fire")
                {
                    a.attackInfo = UnitWeaponStateAttacking::FireInfo{
                        loadSimAngle(ij.at("heading")),
                        loadSimAngle(ij.at("pitch")),
                        loadSimVector(ij.at("targetPosition")),
                        loadOptional(ij.at("firingPiece"), [](const json& v) { return v.get<int>(); }),
                        ij.at("burstsFired").get<int>(),
                        loadGameTime(ij.at("readyTime"))};
                }
                else
                {
                    throw std::runtime_error("bad AttackInfo kind: " + infoKind);
                }
                return a;
            }
            throw std::runtime_error("bad UnitWeaponState kind: " + kind);
        }

        json saveUnitWeapon(const UnitWeapon& w, const SaveContext& ctx, const CobEnvironment& env)
        {
            return json{
                {"weaponType", w.weaponType},
                {"readyTime", saveGameTime(w.readyTime)},
                {"ballisticZOffset", saveSimScalar(w.ballisticZOffset)},
                {"stockedRounds", w.stockedRounds},
                {"queuedRounds", w.queuedRounds},
                {"stockpileProgress", w.stockpileProgress},
                {"stockpileStepDelay", w.stockpileStepDelay},
                {"state", saveUnitWeaponState(w.state, ctx, env)}};
        }

        UnitWeapon loadUnitWeapon(const json& j, const LoadContext& ctx, const CobEnvironment& env)
        {
            UnitWeapon w;
            w.weaponType = j.at("weaponType").get<std::string>();
            w.readyTime = loadGameTime(j.at("readyTime"));
            w.ballisticZOffset = loadSimScalar(j.at("ballisticZOffset"));
            w.stockedRounds = j.at("stockedRounds").get<int>();
            w.queuedRounds = j.at("queuedRounds").get<int>();
            w.stockpileProgress = j.at("stockpileProgress").get<int>();
            w.stockpileStepDelay = j.at("stockpileStepDelay").get<int>();
            w.state = loadUnitWeaponState(j.at("state"), ctx, env);
            return w;
        }

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
                [&](const AttackOrder& a) { return json{{"kind", "attack"}, {"target", saveAttackTarget(a.target, ctx)}}; },
                [](const BuildOrder& b) { return json{{"kind", "build"}, {"unitType", b.unitType}, {"position", saveSimVector(b.position)}}; },
                [](const BuggerOffOrder& b) { return json{{"kind", "buggerOff"}, {"rect", saveDiscreteRect(b.rect)}}; },
                [&](const CompleteBuildOrder& c) { return json{{"kind", "completeBuild"}, {"target", saveUnitIdRef(c.target, ctx)}}; },
                [&](const GuardOrder& g) { return json{{"kind", "guard"}, {"target", saveUnitIdRef(g.target, ctx)}}; },
                [&](const ReclaimOrder& r) { return json{{"kind", "reclaim"}, {"target", saveReclaimTarget(r.target, ctx)}}; },
                [&](const RepairOrder& r) { return json{{"kind", "repair"}, {"target", saveUnitIdRef(r.target, ctx)}}; },
                [](const PatrolOrder& p) { return json{{"kind", "patrol"}, {"destination", saveSimVector(p.destination)}}; },
                [&](const CaptureOrder& c) { return json{{"kind", "capture"}, {"target", saveUnitIdRef(c.target, ctx)}}; },
                [&](const LoadOrder& l) { return json{{"kind", "load"}, {"target", saveUnitIdRef(l.target, ctx)}}; },
                [](const UnloadOrder& u) { return json{{"kind", "unload"}, {"destination", saveSimVector(u.destination)}}; });
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
                return match(
                    loadAttackTarget(j.at("target"), ctx),
                    [](const UnitId& id) { return AttackOrder(id); },
                    [](const SimVector& v) { return AttackOrder(v); });
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
            if (kind == "capture")
            {
                return CaptureOrder(loadUnitIdRef(j.at("target"), ctx));
            }
            if (kind == "load")
            {
                return LoadOrder(loadUnitIdRef(j.at("target"), ctx));
            }
            if (kind == "unload")
            {
                return UnloadOrder(loadSimVector(j.at("destination")));
            }
            throw std::runtime_error("bad UnitOrder kind: " + kind);
        }

        // ---- behaviour and factory state --------------------------------

        json saveUnitCreationStatus(const UnitCreationStatus& s, const SaveContext& ctx)
        {
            return match(
                s,
                [](const UnitCreationStatusPending&) { return json{{"kind", "pending"}}; },
                [](const UnitCreationStatusFailed&) { return json{{"kind", "failed"}}; },
                [&](const UnitCreationStatusDone& d) { return json{{"kind", "done"}, {"unitId", saveUnitIdRef(d.unitId, ctx)}}; });
        }

        UnitCreationStatus loadUnitCreationStatus(const json& j, const LoadContext& ctx)
        {
            const auto& kind = j.at("kind").get_ref<const std::string&>();
            if (kind == "pending")
            {
                return UnitCreationStatusPending();
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
                return UnitBehaviorStateReclaiming{
                    loadReclaimTarget(j.at("target"), ctx),
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
                [](const UnitState::LifeStateDead& d) { return json{{"kind", "dead"}, {"leaveCorpse", d.leaveCorpse}}; });
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
                return UnitState::LifeStateDead{j.at("leaveCorpse").get<bool>()};
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

        json saveUnitState(const UnitState& u, const SaveContext& ctx)
        {
            json pieces = json::array();
            for (const auto& p : u.pieces)
            {
                pieces.push_back(saveUnitMesh(p));
            }

            json orders = json::array();
            for (const auto& o : u.orders)
            {
                orders.push_back(saveUnitOrder(o, ctx));
            }

            json weapons = json::array();
            for (const auto& w : u.weapons)
            {
                weapons.push_back(saveOptional(w, [&](const UnitWeapon& weapon) { return saveUnitWeapon(weapon, ctx, *u.cobEnvironment); }));
            }

            json carriedUnits = json::array();
            for (const auto& id : u.carriedUnits)
            {
                carriedUnits.push_back(saveUnitIdRef(id, ctx));
            }

            json buildQueue = json::array();
            for (const auto& [unitType, count] : u.buildQueue)
            {
                buildQueue.push_back(json{{"unitType", unitType}, {"count", count}});
            }

            return json{
                {"unitType", u.unitType},
                {"pieces", pieces},
                {"position", saveSimVector(u.position)},
                {"previousPosition", saveSimVector(u.previousPosition)},
                {"cobEnvironment", saveCobEnvironment(*u.cobEnvironment)},
                {"owner", u.owner.value},
                {"rotation", saveSimAngle(u.rotation)},
                {"previousRotation", saveSimAngle(u.previousRotation)},
                {"physics", saveUnitPhysicsInfo(u.physics, ctx)},
                {"hitPoints", u.hitPoints},
                {"lifeState", saveLifeState(u.lifeState)},
                {"orders", orders},
                {"behaviourState", saveUnitBehaviorState(u.behaviourState, ctx)},
                {"navigationState", saveNavigationStateInfo(u.navigationState, ctx)},
                {"buildOrderUnitId", saveOptional(u.buildOrderUnitId, [&](UnitId id) { return saveUnitIdRef(id, ctx); })},
                {"inBuildStance", u.inBuildStance},
                {"yardOpen", u.yardOpen},
                {"inCollision", u.inCollision},
                {"weapons", weapons},
                {"fireOrders", saveEnum(u.fireOrders)},
                {"moveOrders", saveEnum(u.moveOrders)},
                {"cobBusy", u.cobBusy},
                {"buggerOffActive", u.buggerOffActive},
                {"armored", u.armored},
                {"kills", u.kills},
                {"sfxOccupyState", u.sfxOccupyState},
                {"buildTimeCompleted", u.buildTimeCompleted},
                {"reclaimProgress", u.reclaimProgress},
                {"captureProgress", u.captureProgress},
                {"selfDestructTime", saveOptional(u.selfDestructTime, [](GameTime t) { return saveGameTime(t); })},
                {"paralyzedUntil", saveOptional(u.paralyzedUntil, [](GameTime t) { return saveGameTime(t); })},
                {"moveRateBand", u.moveRateBand},
                {"carriedBy", saveOptional(u.carriedBy, [&](UnitId id) { return saveUnitIdRef(id, ctx); })},
                {"carriedPiece", u.carriedPiece},
                {"carriedUnits", carriedUnits},
                {"transportScriptTarget", saveOptional(u.transportScriptTarget, [&](UnitId id) { return saveUnitIdRef(id, ctx); })},
                {"transportScriptStartedAt", saveGameTime(u.transportScriptStartedAt)},
                {"airWorkOrbit", saveOptional(u.airWorkOrbit, saveAirWorkOrbitState)},
                {"airLoiter", saveOptional(u.airLoiter, saveAirLoiterState)},
                {"slowFacePoint", saveOptional(u.slowFacePoint, saveSimVector)},
                {"activated", u.activated},
                {"isSufficientlyPowered", u.isSufficientlyPowered},
                {"cloakRequested", u.cloakRequested},
                {"cloaked", u.cloaked},
                {"cloakSuppressedUntil", saveGameTime(u.cloakSuppressedUntil)},
                {"energyProductionBuffer", saveEnergy(u.energyProductionBuffer)},
                {"metalProductionBuffer", saveMetal(u.metalProductionBuffer)},
                {"previousEnergyProductionBuffer", saveEnergy(u.previousEnergyProductionBuffer)},
                {"previousMetalProductionBuffer", saveMetal(u.previousMetalProductionBuffer)},
                {"previousEnergyConsumptionBuffer", saveEnergy(u.previousEnergyConsumptionBuffer)},
                {"previousMetalConsumptionBuffer", saveMetal(u.previousMetalConsumptionBuffer)},
                {"energyConsumptionBuffer", saveEnergy(u.energyConsumptionBuffer)},
                {"metalConsumptionBuffer", saveMetal(u.metalConsumptionBuffer)},
                {"energyRequestBuffer", saveEnergy(u.energyRequestBuffer)},
                {"metalRequestBuffer", saveMetal(u.metalRequestBuffer)},
                {"energyDebt", saveEnergy(u.energyDebt)},
                {"metalDebt", saveMetal(u.metalDebt)},
                {"buildQueue", buildQueue},
                {"factoryState", saveFactoryBehaviorState(u.factoryState, ctx)}};
        }

        /**
         * Fills a unit that was emplaced with its pieces and a fresh
         * CobEnvironment. The environment is overwritten first so weapon aim
         * state can resolve its thread reference against the restored
         * threads.
         */
        void loadUnitStateInto(const json& j, UnitState& u, const LoadContext& ctx)
        {
            loadCobEnvironmentInto(j.at("cobEnvironment"), *u.cobEnvironment);

            u.unitType = j.at("unitType").get<std::string>();
            u.position = loadSimVector(j.at("position"));
            u.previousPosition = loadSimVector(j.at("previousPosition"));
            u.owner = PlayerId(j.at("owner").get<unsigned int>());
            u.rotation = loadSimAngle(j.at("rotation"));
            u.previousRotation = loadSimAngle(j.at("previousRotation"));
            u.physics = loadUnitPhysicsInfo(j.at("physics"), ctx);
            u.hitPoints = j.at("hitPoints").get<unsigned int>();
            u.lifeState = loadLifeState(j.at("lifeState"));
            u.orders.clear();
            for (const auto& oj : j.at("orders"))
            {
                u.orders.push_back(loadUnitOrder(oj, ctx));
            }
            u.behaviourState = loadUnitBehaviorState(j.at("behaviourState"), ctx);
            u.navigationState = loadNavigationStateInfo(j.at("navigationState"), ctx);
            u.buildOrderUnitId = loadOptional(j.at("buildOrderUnitId"), [&](const json& v) { return loadUnitIdRef(v, ctx); });
            u.inBuildStance = j.at("inBuildStance").get<bool>();
            u.yardOpen = j.at("yardOpen").get<bool>();
            u.inCollision = j.at("inCollision").get<bool>();
            const auto& weaponsJson = j.at("weapons");
            for (std::size_t i = 0; i < u.weapons.size(); ++i)
            {
                u.weapons[i] = loadOptional(weaponsJson.at(i), [&](const json& wj) { return loadUnitWeapon(wj, ctx, *u.cobEnvironment); });
            }
            u.fireOrders = loadEnum<UnitFireOrders>(j.at("fireOrders"));
            u.moveOrders = loadEnum<UnitMovementOrders>(j.at("moveOrders"));
            u.cobBusy = j.at("cobBusy").get<bool>();
            u.buggerOffActive = j.at("buggerOffActive").get<bool>();
            u.armored = j.at("armored").get<bool>();
            u.kills = j.at("kills").get<unsigned int>();
            u.sfxOccupyState = j.at("sfxOccupyState").get<int>();
            u.buildTimeCompleted = j.at("buildTimeCompleted").get<unsigned int>();
            u.reclaimProgress = j.at("reclaimProgress").get<unsigned int>();
            u.captureProgress = j.at("captureProgress").get<unsigned int>();
            u.selfDestructTime = loadOptional(j.at("selfDestructTime"), loadGameTime);
            u.paralyzedUntil = loadOptional(j.at("paralyzedUntil"), loadGameTime);
            u.moveRateBand = j.at("moveRateBand").get<unsigned int>();
            u.carriedBy = loadOptional(j.at("carriedBy"), [&](const json& v) { return loadUnitIdRef(v, ctx); });
            u.carriedPiece = j.at("carriedPiece").get<std::string>();
            u.carriedUnits.clear();
            for (const auto& cj : j.at("carriedUnits"))
            {
                u.carriedUnits.push_back(loadUnitIdRef(cj, ctx));
            }
            u.transportScriptTarget = loadOptional(j.at("transportScriptTarget"), [&](const json& v) { return loadUnitIdRef(v, ctx); });
            u.transportScriptStartedAt = loadGameTime(j.at("transportScriptStartedAt"));
            u.airWorkOrbit = loadOptional(j.at("airWorkOrbit"), loadAirWorkOrbitState);
            u.airLoiter = loadOptional(j.at("airLoiter"), loadAirLoiterState);
            u.slowFacePoint = loadOptional(j.at("slowFacePoint"), loadSimVector);
            u.activated = j.at("activated").get<bool>();
            u.isSufficientlyPowered = j.at("isSufficientlyPowered").get<bool>();
            u.cloakRequested = j.at("cloakRequested").get<bool>();
            u.cloaked = j.at("cloaked").get<bool>();
            u.cloakSuppressedUntil = loadGameTime(j.at("cloakSuppressedUntil"));
            u.energyProductionBuffer = loadEnergy(j.at("energyProductionBuffer"));
            u.metalProductionBuffer = loadMetal(j.at("metalProductionBuffer"));
            u.previousEnergyProductionBuffer = loadEnergy(j.at("previousEnergyProductionBuffer"));
            u.previousMetalProductionBuffer = loadMetal(j.at("previousMetalProductionBuffer"));
            u.previousEnergyConsumptionBuffer = loadEnergy(j.at("previousEnergyConsumptionBuffer"));
            u.previousMetalConsumptionBuffer = loadMetal(j.at("previousMetalConsumptionBuffer"));
            u.energyConsumptionBuffer = loadEnergy(j.at("energyConsumptionBuffer"));
            u.metalConsumptionBuffer = loadMetal(j.at("metalConsumptionBuffer"));
            u.energyRequestBuffer = loadEnergy(j.at("energyRequestBuffer"));
            u.metalRequestBuffer = loadMetal(j.at("metalRequestBuffer"));
            u.energyDebt = loadEnergy(j.at("energyDebt"));
            u.metalDebt = loadMetal(j.at("metalDebt"));
            u.buildQueue.clear();
            for (const auto& bj : j.at("buildQueue"))
            {
                u.buildQueue.emplace_back(bj.at("unitType").get<std::string>(), bj.at("count").get<int>());
            }
            u.factoryState = loadFactoryBehaviorState(j.at("factoryState"), ctx);
        }

        // ---- projectiles ------------------------------------------------

        json saveProjectile(const Projectile& p, const SaveContext& ctx)
        {
            json damage = json::object();
            for (const auto& [unitType, amount] : p.damage)
            {
                damage[unitType] = amount;
            }

            return json{
                {"weaponType", p.weaponType},
                {"owner", p.owner.value},
                {"attacker", saveOptional(p.attacker, [&](UnitId id) { return saveUnitIdRef(id, ctx); })},
                {"position", saveSimVector(p.position)},
                {"previousPosition", saveSimVector(p.previousPosition)},
                {"origin", saveSimVector(p.origin)},
                {"velocity", saveSimVector(p.velocity)},
                {"lastSmoke", saveGameTime(p.lastSmoke)},
                {"damage", damage},
                {"dieOnFrame", saveOptional(p.dieOnFrame, [](GameTime t) { return saveGameTime(t); })},
                {"damageRadius", saveSimScalar(p.damageRadius)},
                {"edgeEffectiveness", saveSimScalar(p.edgeEffectiveness)},
                {"groundBounce", p.groundBounce},
                {"isDead", p.isDead},
                {"createdAt", saveGameTime(p.createdAt)},
                {"targetUnit", saveOptional(p.targetUnit, [&](UnitId id) { return saveUnitIdRef(id, ctx); })},
                {"targetProjectile", saveOptional(p.targetProjectile, [&](ProjectileId id) { return saveProjectileIdRef(id, ctx); })},
                {"targetPosition", saveOptional(p.targetPosition, saveSimVector)},
                {"heading", saveSimAngle(p.heading)},
                {"pitch", saveSimAngle(p.pitch)},
                {"speed", saveSimScalar(p.speed)},
                {"motorOutFrame", saveOptional(p.motorOutFrame, [](GameTime t) { return saveGameTime(t); })},
                {"secondPhase", p.secondPhase},
                {"motorOut", p.motorOut}};
        }

        void loadProjectileInto(const json& j, Projectile& p, const LoadContext& ctx)
        {
            p.weaponType = j.at("weaponType").get<std::string>();
            p.owner = PlayerId(j.at("owner").get<unsigned int>());
            p.attacker = loadOptional(j.at("attacker"), [&](const json& v) { return loadUnitIdRef(v, ctx); });
            p.position = loadSimVector(j.at("position"));
            p.previousPosition = loadSimVector(j.at("previousPosition"));
            p.origin = loadSimVector(j.at("origin"));
            p.velocity = loadSimVector(j.at("velocity"));
            p.lastSmoke = loadGameTime(j.at("lastSmoke"));
            p.damage.clear();
            for (const auto& [unitType, amount] : j.at("damage").items())
            {
                p.damage[unitType] = amount.get<unsigned int>();
            }
            p.dieOnFrame = loadOptional(j.at("dieOnFrame"), loadGameTime);
            p.damageRadius = loadSimScalar(j.at("damageRadius"));
            p.edgeEffectiveness = loadSimScalar(j.at("edgeEffectiveness"));
            p.groundBounce = j.at("groundBounce").get<bool>();
            p.isDead = j.at("isDead").get<bool>();
            p.createdAt = loadGameTime(j.at("createdAt"));
            p.targetUnit = loadOptional(j.at("targetUnit"), [&](const json& v) { return loadUnitIdRef(v, ctx); });
            p.targetProjectile = loadOptional(j.at("targetProjectile"), [&](const json& v) { return loadProjectileIdRef(v, ctx); });
            p.targetPosition = loadOptional(j.at("targetPosition"), loadSimVector);
            p.heading = loadSimAngle(j.at("heading"));
            p.pitch = loadSimAngle(j.at("pitch"));
            p.speed = loadSimScalar(j.at("speed"));
            p.motorOutFrame = loadOptional(j.at("motorOutFrame"), loadGameTime);
            p.secondPhase = j.at("secondPhase").get<bool>();
            p.motorOut = j.at("motorOut").get<bool>();
        }

        /**
         * Puts a loaded unit back onto the map the way tryAddUnit and the
         * air transitions would have left it: a carried unit holds no ground,
         * a flying unit is in the flying set, a ground unit stamps its
         * footprint, a building stamps its yardmap.
         */
        void restoreUnitOccupancy(GameSimulation& sim, UnitId unitId, const UnitState& unit)
        {
            if (unit.carriedBy)
            {
                return;
            }

            const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);

            if (unitDefinition.isMobile && isFlying(unit.physics))
            {
                sim.flyingUnitsSet.insert(unitId);
                return;
            }

            auto footprintRect = sim.computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            auto footprintRegion = sim.occupiedGrid.tryToRegion(footprintRect);
            if (!footprintRegion)
            {
                throw std::runtime_error("loaded unit's footprint does not fit the map");
            }

            if (unitDefinition.isMobile)
            {
                sim.occupiedGrid.forEach(*footprintRegion, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
            }
            else
            {
                if (!unitDefinition.yardMap)
                {
                    throw std::runtime_error("loaded building has no yardmap in its definition");
                }
                sim.occupiedGrid.forEach2(footprintRegion->x, footprintRegion->y, *unitDefinition.yardMap, [&](auto& cell, const auto& yardMapCell) {
                    cell.buildingInfo = OccupiedCellBuildingInfo{unitId, isPassable(yardMapCell, unit.yardOpen)};
                });
            }
        }
    }

    nlohmann::json saveSimulationToJson(const GameSimulation& sim)
    {
        SaveContext ctx;
        {
            uint32_t i = 0;
            for (const auto& [id, _] : sim.units)
            {
                ctx.units.insert({id, i++});
            }
        }
        {
            uint32_t i = 0;
            for (const auto& [id, _] : sim.features)
            {
                ctx.features.insert({FeatureId(id.value), i++});
            }
        }
        {
            uint32_t i = 0;
            for (const auto& [id, _] : sim.projectiles)
            {
                ctx.projectiles.insert({id, i++});
            }
        }

        json j;
        j["version"] = 1;

        // The wind range is a construction-time constant; it travels with the
        // save only so the load can check it was given the right map.
        j["minWindSpeed"] = sim.minWindSpeed;
        j["maxWindSpeed"] = sim.maxWindSpeed;

        j["gameTime"] = saveGameTime(sim.gameTime);

        {
            std::ostringstream ss;
            ss << sim.rng;
            j["rng"] = ss.str();
        }

        j["gameStatus"] = saveWinStatus(sim.gameStatus);
        j["currentWindGenerationFactor"] = saveSimScalar(sim.currentWindGenerationFactor);
        j["tidalStrength"] = sim.tidalStrength;
        j["nextWindSpeedChange"] = saveGameTime(sim.nextWindSpeedChange);
        j["featureRegrowthCursor"] = sim.featureRegrowthCursor;

        json players = json::array();
        for (const auto& p : sim.players)
        {
            players.push_back(saveGamePlayerInfo(p));
        }
        j["players"] = players;

        json features = json::array();
        for (const auto& [_, f] : sim.features)
        {
            features.push_back(saveMapFeature(f));
        }
        j["features"] = features;

        json units = json::array();
        for (const auto& [_, u] : sim.units)
        {
            units.push_back(saveUnitState(u, ctx));
        }
        j["units"] = units;

        json projectiles = json::array();
        for (const auto& [_, p] : sim.projectiles)
        {
            projectiles.push_back(saveProjectile(p, ctx));
        }
        j["projectiles"] = projectiles;

        json pathRequests = json::array();
        for (const auto& r : sim.pathRequests)
        {
            pathRequests.push_back(saveUnitIdRef(r.unitId, ctx));
        }
        j["pathRequests"] = pathRequests;

        // Empty between ticks (spawnNewUnits drains it), but carried along so
        // a mid-tick snapshot would not lose anything.
        json unitCreationRequests = json::array();
        for (const auto& id : sim.unitCreationRequests)
        {
            unitCreationRequests.push_back(saveUnitIdRef(id, ctx));
        }
        j["unitCreationRequests"] = unitCreationRequests;

        return j;
    }

    void loadSimulationFromJson(const nlohmann::json& j, GameSimulation& sim)
    {
        if (j.at("version").get<int>() != 1)
        {
            throw std::runtime_error("unsupported save version");
        }
        if (j.at("minWindSpeed").get<int>() != sim.minWindSpeed || j.at("maxWindSpeed").get<int>() != sim.maxWindSpeed)
        {
            throw std::runtime_error("save was made for a map with a different wind range");
        }
        if (!sim.players.empty())
        {
            throw std::runtime_error("loadSimulationFromJson needs a simulation with no players added");
        }
        {
            auto it = sim.units.begin();
            if (it != sim.units.end())
            {
                throw std::runtime_error("loadSimulationFromJson needs a simulation with no units spawned");
            }
        }

        for (const auto& pj : j.at("players"))
        {
            sim.addPlayer(loadGamePlayerInfo(pj));
        }

        sim.gameTime = loadGameTime(j.at("gameTime"));
        {
            std::istringstream ss(j.at("rng").get<std::string>());
            ss >> sim.rng;
        }
        sim.gameStatus = loadWinStatus(j.at("gameStatus"));
        sim.currentWindGenerationFactor = loadSimScalar(j.at("currentWindGenerationFactor"));
        sim.tidalStrength = j.at("tidalStrength").get<int>();
        sim.nextWindSpeedChange = loadGameTime(j.at("nextWindSpeedChange"));
        sim.featureRegrowthCursor = j.at("featureRegrowthCursor").get<int>();

        // The fresh sim has the map's initial features standing. Sweep them
        // all away -- grid cells included -- and put the saved ones down in
        // saved order, so the loaded feature array iterates the way the saved
        // one did. Replacing the whole VectorMap resets its free list, which
        // is what makes the handed-out ids dense again. The metal and geo
        // grids are left alone: only permanent features write them, permanent
        // features are never removed, so the saved set puts back exactly what
        // the initial set wrote.
        sim.occupiedGrid.forEachIndexed([](const auto&, OccupiedCell& cell) { cell.featureId = std::nullopt; });
        sim.features = VectorMap<MapFeature, FeatureIdTag>();

        LoadContext ctx;

        for (const auto& fj : j.at("features"))
        {
            MapFeature f;
            f.featureName = FeatureDefinitionId(fj.at("featureName").get<unsigned int>());
            f.position = loadSimVector(fj.at("position"));
            f.rotation = loadSimAngle(fj.at("rotation"));
            auto id = sim.addFeature(std::move(f));
            if (!id)
            {
                throw std::runtime_error("could not place a loaded feature");
            }
            auto& placed = sim.features.tryGet(*id)->get();
            // Older saves predate sinking wreckage; theirs simply sit still.
            placed.velocity = fj.contains("velocity") ? loadSimVector(fj.at("velocity")) : SimVector(0_ss, 0_ss, 0_ss);
            placed.reclaimProgress = fj.at("reclaimProgress").get<unsigned int>();
            placed.hitPoints = fj.at("hitPoints").get<unsigned int>();
            placed.burningUntil = loadOptional(fj.at("burningUntil"), loadGameTime);
            placed.nextSpark = loadGameTime(fj.at("nextSpark"));
            ctx.features.push_back(*id);
        }

        // Units and projectiles go in two passes: first every slot is claimed
        // so the id tables are complete, then the state is filled in, so a
        // reference can point forwards as well as backwards.
        const auto& unitsJson = j.at("units");
        for (const auto& uj : unitsJson)
        {
            auto unitType = uj.at("unitType").get<std::string>();
            auto scriptIt = sim.unitScriptDefinitions.find(unitType);
            if (scriptIt == sim.unitScriptDefinitions.end())
            {
                throw std::runtime_error("no script loaded for unit type " + unitType);
            }

            std::vector<UnitMesh> pieces;
            for (const auto& pj : uj.at("pieces"))
            {
                pieces.push_back(loadUnitMesh(pj));
            }

            auto env = std::make_unique<CobEnvironment>(&scriptIt->second);
            ctx.units.push_back(UnitId(sim.units.emplace(pieces, std::move(env))));
        }

        const auto& projectilesJson = j.at("projectiles");
        for (std::size_t i = 0; i < projectilesJson.size(); ++i)
        {
            ctx.projectiles.push_back(ProjectileId(sim.projectiles.emplace()));
        }

        {
            std::size_t i = 0;
            for (const auto& uj : unitsJson)
            {
                loadUnitStateInto(uj, sim.units.tryGet(ctx.units[i])->get(), ctx);
                ++i;
            }
        }

        {
            std::size_t i = 0;
            for (const auto& pj : projectilesJson)
            {
                loadProjectileInto(pj, sim.projectiles.tryGet(ctx.projectiles[i])->get(), ctx);
                ++i;
            }
        }

        for (const auto& [id, unit] : sim.units)
        {
            restoreUnitOccupancy(sim, id, unit);
        }

        sim.pathRequests.clear();
        for (const auto& rj : j.at("pathRequests"))
        {
            sim.pathRequests.push_back(PathRequest{loadUnitIdRef(rj, ctx)});
        }

        sim.unitCreationRequests.clear();
        for (const auto& rj : j.at("unitCreationRequests"))
        {
            sim.unitCreationRequests.push_back(loadUnitIdRef(rj, ctx));
        }

        sim.events.clear();

        sim.updateVisibility();
    }
}
