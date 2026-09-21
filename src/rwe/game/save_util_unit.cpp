#include "save_util.h"

#include "SaveJson.h"

#include <rwe/util/match.h>
#include <stdexcept>
#include <vector>

namespace rwe
{
    using nlohmann::json;

    /**
     * Per-type save and load helpers for a unit's pieces, its COB environment and its weapons.
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
            {"cached", m.cached},
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
        // Saves written before the flag existed carry no key; every piece
        // starts cached, which is the original's default too.
        m.cached = j.value("cached", true);
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
                    [](const UnitWeaponStateAttacking::AimedInfo& i) {
                        return json{
                            {"kind", "aimed"},
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
            else if (infoKind == "aimed")
            {
                a.attackInfo = UnitWeaponStateAttacking::AimedInfo{
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
}
