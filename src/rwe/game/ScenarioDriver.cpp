#include "ScenarioDriver.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <iostream>
#include <rwe/game/GameScene.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/ui/UiPanel.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    namespace
    {
        ScenarioOutcome& outcome()
        {
            static ScenarioOutcome value;
            return value;
        }

        std::map<std::string, ScenarioBuilder>& registry()
        {
            static std::map<std::string, ScenarioBuilder> value;
            return value;
        }

        std::optional<ScenarioBuilder> findBuilder(const std::string& name)
        {
            auto it = registry().find(name);
            if (it == registry().end())
            {
                return std::nullopt;
            }
            return it->second;
        }
    }

    const ScenarioOutcome& scenarioOutcome()
    {
        return outcome();
    }

    void resetScenarioOutcome()
    {
        outcome() = ScenarioOutcome{};
    }

    void registerScenario(const std::string& name, ScenarioBuilder builder)
    {
        registry()[name] = std::move(builder);
    }

    std::vector<std::string> scenarioNames()
    {
        std::vector<std::string> names;
        names.reserve(registry().size());
        for (const auto& [name, _] : registry())
        {
            names.push_back(name);
        }
        return names;
    }

    Scenario::Scenario(ScenarioDriver& driver) : driver(driver)
    {
    }

    void Scenario::at(unsigned int tick, std::function<void(ScenarioDriver&)> step)
    {
        driver.addAt(tick, std::move(step));
    }

    void Scenario::after(unsigned int tick, std::function<void(ScenarioDriver&)> step)
    {
        driver.addAfter(tick, std::move(step));
    }

    void Scenario::endAt(unsigned int tick)
    {
        driver.setEndTick(tick);
    }

    ScenarioDriver::ScenarioDriver(GameScene& scene, const std::string& name) : scene(scene), name(name)
    {
        resetScenarioOutcome();

        parkedMouse = Point(
            scene.sceneContext.viewport->width() / 2,
            scene.sceneContext.viewport->height() / 2);
        scene.mousePositionOverride = parkedMouse;

        if (!scene.sceneContext.sceneManager->isHeadless())
        {
            recordFailure("a scenario must run headless; only then is there one update per tick");
            scene.sceneContext.sceneManager->requestExit();
            return;
        }

        auto builder = findBuilder(name);
        if (!builder)
        {
            recordFailure("no scenario called '" + name + "' is registered");
            scene.sceneContext.sceneManager->requestExit();
            return;
        }

        Scenario scenario(*this);
        (*builder)(scenario);
        outcome().started = true;

        maxFrames = endTick + 30u * static_cast<unsigned int>(SimTicksPerSecond);

        if (!hasStep)
        {
            recordFailure("scenario registered no steps");
            scene.sceneContext.sceneManager->requestExit();
        }
    }

    ScenarioDriver::~ScenarioDriver() = default;

    void ScenarioDriver::addAt(unsigned int tick, std::function<void(ScenarioDriver&)> step)
    {
        beforeSteps[tick].push_back(std::move(step));
        hasStep = true;
        endTick = std::max(endTick, tick);
    }

    void ScenarioDriver::addAfter(unsigned int tick, std::function<void(ScenarioDriver&)> step)
    {
        afterSteps[tick].push_back(std::move(step));
        hasStep = true;
        endTick = std::max(endTick, tick);
    }

    void ScenarioDriver::setEndTick(unsigned int tick)
    {
        hasStep = true;
        endTick = std::max(endTick, tick);
    }

    void ScenarioDriver::beforeTick(unsigned int tick)
    {
        // Counted before the tick guard, because a frame that does not advance
        // the simulation still comes through here and the count is what
        // catches a pause the scenario is waiting out.
        ++framesSeen;
        if (maxFrames > 0 && framesSeen > maxFrames)
        {
            if (!finished)
            {
                recordFailure(
                    "did not reach tick " + std::to_string(endTick) + " within "
                    + std::to_string(maxFrames) + " frames; the simulation clock may be paused");
                finished = true;
                scene.sceneContext.sceneManager->requestExit();
            }
            return;
        }

        // A frame that does not advance the simulation still calls this, so a
        // step would otherwise fire once per frame for one tick's worth of
        // game. Frame rate is a property of one machine and must not decide
        // what a scenario does.
        if (lastBefore == tick)
        {
            return;
        }
        lastBefore = tick;
        visitedBefore.insert(tick);
        currentTick = tick;

        auto it = beforeSteps.find(tick);
        if (it == beforeSteps.end())
        {
            return;
        }
        for (const auto& step : it->second)
        {
            runStep(step);
            if (finished)
            {
                return;
            }
        }
    }

    void ScenarioDriver::afterTick(unsigned int tick)
    {
        if (lastAfter == tick)
        {
            return;
        }
        lastAfter = tick;
        visitedAfter.insert(tick);
        currentTick = tick;

        if (!finished)
        {
            if (auto it = afterSteps.find(tick); it != afterSteps.end())
            {
                for (const auto& step : it->second)
                {
                    runStep(step);
                    if (finished)
                    {
                        return;
                    }
                }
            }
        }

        if (tick >= endTick)
        {
            finish();
        }
    }

    void ScenarioDriver::runStep(const std::function<void(ScenarioDriver&)>& step)
    {
        if (finished)
        {
            return;
        }

        try
        {
            step(*this);
        }
        catch (const std::exception& e)
        {
            recordFailure(std::string("threw: ") + e.what());
        }

        if (!outcome().failures.empty())
        {
            // Fail fast: a scenario that has already broken an invariant can
            // easily take the next step somewhere undefined, and the failure
            // it would report then is noise on top of the first one.
            finished = true;
            scene.sceneContext.sceneManager->requestExit();
        }
    }

    void ScenarioDriver::recordFailure(const std::string& message)
    {
        auto text = name + ": tick " + std::to_string(currentTick) + ": " + message;
        LOG_ERROR << "Scenario failure -- " << text;
        std::cerr << "SCENARIO-FAIL " << text << "\n";
        outcome().failures.push_back(text);
    }

    void ScenarioDriver::finish()
    {
        if (finished)
        {
            return;
        }

        // Reaching the end tick is not enough on its own: a step registered at
        // a tick that was never visited would simply not have run, and an
        // assertion that never ran looks exactly like one that held. Name every
        // such tick instead of reporting a pass.
        std::vector<unsigned int> skipped;
        for (const auto& [tick, _] : beforeSteps)
        {
            if (visitedBefore.find(tick) == visitedBefore.end())
            {
                skipped.push_back(tick);
            }
        }
        for (const auto& [tick, _] : afterSteps)
        {
            if (visitedAfter.find(tick) == visitedAfter.end())
            {
                skipped.push_back(tick);
            }
        }
        if (!skipped.empty())
        {
            std::sort(skipped.begin(), skipped.end());
            for (auto tick : skipped)
            {
                recordFailure("no step ran at tick " + std::to_string(tick));
            }
            finished = true;
            scene.sceneContext.sceneManager->requestExit();
            return;
        }

        finished = true;
        outcome().reachedEnd = true;
        LOG_INFO << "Scenario '" << name << "' reached tick " << endTick;
        scene.sceneContext.sceneManager->requestExit();
    }

    void ScenarioDriver::require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            recordFailure(message);
        }
    }

    std::optional<UnitId> ScenarioDriver::commander(int index) const
    {
        std::vector<UnitId> commanders;
        for (const auto& [unitId, unit] : scene.simulation.units)
        {
            if (scene.simulation.unitDefinitions.at(unit.unitType).commander)
            {
                commanders.push_back(unitId);
            }
        }
        std::sort(commanders.begin(), commanders.end(), [](UnitId a, UnitId b) { return a.value < b.value; });

        if (index < 0 || static_cast<std::size_t>(index) >= commanders.size())
        {
            return std::nullopt;
        }
        return commanders[static_cast<std::size_t>(index)];
    }

    std::size_t ScenarioDriver::selectedCount() const
    {
        return scene.selectedUnits.size();
    }

    std::optional<UnitId> ScenarioDriver::selectedUnit() const
    {
        return scene.getSingleSelectedUnit();
    }

    std::optional<SimVector> ScenarioDriver::cameraPosition() const
    {
        const auto& position = scene.worldCameraState.position;
        return SimVector(floatToSimScalar(position.x), floatToSimScalar(position.y), floatToSimScalar(position.z));
    }

    bool ScenarioDriver::isPaused() const
    {
        return scene.paused;
    }

    void ScenarioDriver::setMouse(const Point& p)
    {
        scene.mousePositionOverride = p;
    }

    void ScenarioDriver::updateHover()
    {
        // The frame's own hover pass runs after the input at the top of
        // update, so a click delivered here would otherwise act on wherever
        // the cursor was a frame ago.
        scene.hoveredUnit = scene.getUnitUnderCursor();
        scene.hoveredFeature = scene.getFeatureUnderCursor();
    }

    std::optional<Point> ScenarioDriver::screenPositionOf(UnitId unitId) const
    {
        auto unit = scene.tryGetUnit(unitId);
        if (!unit)
        {
            return std::nullopt;
        }

        auto matrix = computeViewProjectionMatrix(
            scene.worldCameraState,
            scene.worldViewport.width(),
            scene.worldViewport.height());
        auto clip = matrix * simVectorToFloat(unit->get().position);
        // toViewportSpace is relative to the viewport's own top left, and the
        // input handlers work in frame coordinates, so the world viewport's
        // inset goes back on. See AbstractViewport::toOtherViewport.
        auto inViewport = scene.worldViewport.toViewportSpace(clip.x, clip.y);
        return Point(
            inViewport.x + scene.worldViewport.x(),
            inViewport.y + scene.worldViewport.y());
    }

    void ScenarioDriver::select(UnitId unitId)
    {
        auto p = screenPositionOf(unitId);
        if (!p)
        {
            require(false, "select: unit " + std::to_string(unitId.value) + " is gone");
            return;
        }
        clickAt(p->x, p->y);
    }

    void ScenarioDriver::clickAt(int x, int y)
    {
        setMouse(Point(x, y));
        updateHover();
        scene.onMouseDown(MouseButtonEvent(x, y, MouseButtonEvent::MouseButton::Left));
        scene.onMouseUp(MouseButtonEvent(x, y, MouseButtonEvent::MouseButton::Left));
        setMouse(parkedMouse);
    }

    std::optional<SimVector> ScenarioDriver::unitPosition(UnitId unitId) const
    {
        auto unit = scene.tryGetUnit(unitId);
        if (!unit)
        {
            return std::nullopt;
        }
        return unit->get().position;
    }

    void ScenarioDriver::moveOrder(UnitId unitId, const SimVector& destination)
    {
        scene.localPlayerIssueUnitOrder(unitId, MoveOrder(destination));
    }

    std::optional<SimVector> ScenarioDriver::placeWaterBuildAndCheckFeedback(const std::string& unitType)
    {
        // BUILD-GRID-001/002, docs/PLAYTEST-BUILD-GRID-FINDINGS.md.
        // Setup uses shipped terrain and the real placement permission query;
        // the action under test is GameScene's click-to-order/feedback path.
        auto builder = commander(0);
        require(builder.has_value(), "a local commander can place the water building");
        if (!builder)
        {
            return std::nullopt;
        }
        select(*builder);
        require(scene.selectedUnits.contains(*builder), "the placement builder is selected");
        // Reveal the fixture's terrain: a distant black-map site is correctly
        // refused by the real placement query before its height is relevant.
        for (auto& cell : scene.simulation.explored.getVector())
        {
            cell |= scene.simulation.losGroupBitFor(scene.localPlayerId);
        }
        const auto& definition = scene.simulation.unitDefinitions.at(unitType);
        auto mc = scene.simulation.getAdHocMovementClass(definition.movementCollisionInfo);
        const auto& heights = scene.simulation.terrain.getHeightMap();
        auto expectedY = scene.simulation.terrain.getSeaLevel() - SimScalar(definition.waterLine);
        for (int y = 16; y + static_cast<int>(mc.footprintZ) < heights.getHeight() - 8; y += 8)
        {
            for (int x = 16; x + static_cast<int>(mc.footprintX) < heights.getWidth() - 2; x += 8)
            {
                DiscreteRect rect(x, y, mc.footprintX, mc.footprintZ);
                if (!scene.simulation.canBeBuiltAtAsSeenBy(mc, definition.yardMap, definition.yardMapContainsGeo,
                        x, y, definition.waterLine, scene.localPlayerId))
                {
                    continue;
                }
                auto site = scene.simulation.terrain.heightmapIndexToWorldCorner(x, y);
                site.x += SimScalar(mc.footprintX * 8u);
                site.z += SimScalar(mc.footprintZ * 8u);
                site.y = expectedY;
                auto ground = scene.simulation.terrain.getHeightAt(site.x, site.z);
                auto delta = site - scene.simulation.getUnitState(*builder).position;
                delta.y = 0_ss;
                if (expectedY == ground || delta.lengthSquared() < 600_ss * 600_ss)
                {
                    continue;
                }
                require(scene.simulation.computeBuildHeight(definition, rect) == expectedY,
                    "the shipped water-only yard previews at sea level minus draft");
                scene.setCameraPosition(Vector3f(site.x.value, 0.0f, site.z.value));
                scene.cursorMode.next(BuildCursorMode{unitType});
                // Park the legal hover rectangle a real frame would produce.
                // This avoids making mouse-ray intersection the fixture's subject.
                scene.hoverBuildInfo = HoverBuildInfo{rect, true};
                clickAt(scene.worldViewport.x() + scene.worldViewport.width() / 2,
                    scene.worldViewport.y() + scene.worldViewport.height() / 2);
                const auto ghost = std::find_if(scene.localBuildGhosts.begin(), scene.localBuildGhosts.end(), [&](const auto& value) {
                    return value.builderId == *builder && value.unitType == unitType && value.position.x == site.x && value.position.z == site.z;
                });
                require(ghost != scene.localBuildGhosts.end(), "the click immediately creates local placement feedback");
                if (ghost != scene.localBuildGhosts.end())
                {
                    require(ghost->position.y == expectedY, "placement feedback retains the preview height instead of the seabed");
                }
                return site;
            }
        }
        require(false, "a distant legal water-only site exists in the shipped map");
        return std::nullopt;
    }

    std::optional<SimVector> ScenarioDriver::queuedBuildPosition(UnitId builder, const std::string& unitType) const
    {
        for (const auto& order : scene.simulation.getUnitState(builder).orders)
        {
            if (const auto* build = std::get_if<BuildOrder>(&order); build && build->unitType == unitType)
            {
                return build->position;
            }
        }
        return std::nullopt;
    }

    void ScenarioDriver::peerGameSpeed(int peerSpeedLevel)
    {
        scene.setPeerGameSpeedLevel(peerSpeedLevel);
    }

    void ScenarioDriver::peerPaused(bool paused)
    {
        scene.setPeerPaused(paused);
    }

    void ScenarioDriver::clickGadget(const std::string& name)
    {
        auto centre = gadgetCentre(name);
        if (!centre)
        {
            require(false, "clickGadget: no gadget called '" + name + "' on the current panel");
            return;
        }
        clickAt(centre->x, centre->y);
    }

    void ScenarioDriver::key(int keyCode)
    {
        keyDown(keyCode);
        keyUp(keyCode);
    }

    void ScenarioDriver::keyDown(int keyCode)
    {
        SDL_KeyboardEvent e{};
        e.key = keyCode;
        e.scancode = SDL_GetScancodeFromKey(keyCode, nullptr);

        e.type = SDL_EVENT_KEY_DOWN;
        e.down = true;
        scene.onKeyDown(e);
    }

    void ScenarioDriver::keyUp(int keyCode)
    {
        SDL_KeyboardEvent e{};
        e.key = keyCode;
        e.scancode = SDL_GetScancodeFromKey(keyCode, nullptr);
        e.type = SDL_EVENT_KEY_UP;
        e.down = false;
        scene.onKeyUp(e);
    }

    bool ScenarioDriver::isCameraTracking() const
    {
        return std::holds_alternative<GameScene::CameraControlStateTrackingUnit>(scene.cameraControlState);
    }

    std::optional<UnitId> ScenarioDriver::spawnNearCamera(const std::string& unitType, float offsetX, float offsetZ)
    {
        auto camera = scene.worldCameraState.position;
        auto x = camera.x + offsetX;
        auto z = camera.z + offsetZ;
        auto y = simScalarToFloat(scene.simulation.terrain.getHeightAt(floatToSimScalar(x), floatToSimScalar(z)));
        return scene.spawnCompletedUnit(unitType, scene.localPlayerId, SimVector(floatToSimScalar(x), floatToSimScalar(y), floatToSimScalar(z)));
    }

    UiStagedButton* ScenarioDriver::findStagedButton(const std::string& name)
    {
        auto* panel = scene.currentPanel.get();
        if (panel == nullptr)
        {
            return nullptr;
        }

        if (auto withPrefix = scene.findWithSidePrefix<UiStagedButton>(*panel, name))
        {
            return &withPrefix->get();
        }
        if (auto exact = panel->find<UiStagedButton>(name))
        {
            return &exact->get();
        }
        return nullptr;
    }

    UiComponent* ScenarioDriver::findGadget(const std::string& name)
    {
        return findStagedButton(name);
    }

    std::optional<Point> ScenarioDriver::gadgetCentre(const std::string& name)
    {
        auto* panel = scene.currentPanel.get();
        auto* button = findGadget(name);
        if (panel == nullptr || button == nullptr)
        {
            return std::nullopt;
        }

        // A panel owns its x as its own offset, and the slide moves that, so
        // the click is aimed where the button is drawn now rather than where
        // its gui file put it. The panel then subtracts its own x on the way
        // in, which is what makes the button's own coordinate line up.
        return Point(
            panel->getX() + button->getX() + static_cast<int>(button->getWidth()) / 2,
            panel->getY() + button->getY() + static_cast<int>(button->getHeight()) / 2);
    }

    GadgetState ScenarioDriver::gadget(const std::string& name)
    {
        auto* button = findStagedButton(name);
        if (button == nullptr)
        {
            return GadgetState{};
        }
        return GadgetState{true, button->isToggledOn()};
    }
}
