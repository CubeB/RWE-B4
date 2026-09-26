#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <rwe/grid/Point.h>
#include <rwe/sim/UnitId.h>
#include <set>
#include <string>
#include <vector>

namespace rwe
{
    class GameScene;
    class ScenarioDriver;
    class UiStagedButton;

    /**
     * What a whole scenario run came to. Read by the `scenario` executable
     * after rwe::run returns, and why that executable can exit non-zero: the
     * run itself cannot return a status through the scene loop.
     */
    struct ScenarioOutcome
    {
        /** False if the named scenario was not registered, or it never started. */
        bool started{false};

        /** True once the scenario's end tick completed without the run being cut short. */
        bool reachedEnd{false};

        /** One line per failed require, in the order they were recorded. */
        std::vector<std::string> failures;
    };

    const ScenarioOutcome& scenarioOutcome();

    /**
     * Forgets the last run's outcome. The `scenario` executable calls this
     * before each rwe::run, because a run that never builds a GameScene (a
     * load failure, a quit event) leaves no driver to reset it and the
     * previous scenario's pass must not be read as this one's.
     */
    void resetScenarioOutcome();

    /**
     * A scenario's registration surface, handed to its builder before the
     * game starts. Steps are keyed by tick and run by the driver during a
     * real GameScene's update, so they see a game that is being played rather
     * than one being assembled.
     *
     * These are simulation ticks, and the clock stops while the game is
     * paused: a step keyed to a tick after a pause is stepped on the frame
     * clock and will never arrive. The driver fails the run on a frame budget
     * rather than waiting it out.
     */
    class Scenario
    {
    public:
        explicit Scenario(ScenarioDriver& driver);

        /** Runs at the top of the update whose tick is `tick`, before that tick runs. */
        void at(unsigned int tick, std::function<void(ScenarioDriver&)> step);

        /** Runs at the end of the same update, after the tick and any panel rebuild. */
        void after(unsigned int tick, std::function<void(ScenarioDriver&)> step);

        /**
         * Stops the run once `tick` has completed. By default the last tick any
         * step named, so a scenario that only asserts at tick 31 ends there.
         */
        void endAt(unsigned int tick);

    private:
        ScenarioDriver& driver;
    };

    using ScenarioBuilder = std::function<void(Scenario&)>;

    /** Registers a scenario under a name. Called by the executable before it runs one. */
    void registerScenario(const std::string& name, ScenarioBuilder builder);

    /** Every registered scenario's name, sorted, for --list and --all. */
    std::vector<std::string> scenarioNames();

    /** What a scenario can read of a gadget: whether it is there, and its toggle face. */
    struct GadgetState
    {
        bool found{false};
        bool toggledOn{false};
    };

    /**
     * Drives the real GameScene at chosen ticks for a headless scenario test.
     *
     * It calls the scene's own input handlers rather than pushing SDL events.
     * The input path needed one seam for that: a synthetic event carries its
     * own coordinates but cannot move the live cursor, so
     * GameScene::mousePositionOverride stands in for the cursor and is parked
     * while the driver is not clicking. Nothing outside a scenario run ever
     * sets it. Reading the panel needed GameScene to be a friend and
     * UiStagedButton to expose its toggle face; neither is used by a real
     * game.
     *
     * A friend of GameScene, so a scenario can select, spawn and read the
     * panel without any of it becoming part of the scene's public surface.
     */
    class ScenarioDriver
    {
    public:
        ScenarioDriver(GameScene& scene, const std::string& name);
        ~ScenarioDriver();

        ScenarioDriver(const ScenarioDriver&) = delete;
        ScenarioDriver& operator=(const ScenarioDriver&) = delete;

        /** Fires the steps registered for `tick`; see Scenario::at. */
        void beforeTick(unsigned int tick);

        /** Fires the steps registered for `tick` and ends the run at the end tick; see Scenario::after. */
        void afterTick(unsigned int tick);

        /** The index-th commander on the map, in UnitId order: 0 is the local player's. */
        std::optional<UnitId> commander(int index) const;

        /** A real left click at the unit's own screen position. */
        void select(UnitId unitId);

        /** A real left click at a frame coordinate. */
        void clickAt(int x, int y);

        /** A real left click at the centre of the named gadget on the live panel. */
        void clickGadget(const std::string& name);

        /** A keypress and release, through the scene's own handlers. */
        void key(int keyCode);

        /**
         * Spawns a finished unit of the given type near the camera, as a
         * scenario's setup rather than as anything under test. Nothing about
         * it goes through a UI handler.
         */
        std::optional<UnitId> spawnNearCamera(const std::string& unitType);

        /** Records a failure at the current step's tick if `condition` does not hold. */
        void require(bool condition, const std::string& message);

        /** The named gadget's toggle state on the live panel. */
        GadgetState gadget(const std::string& name);

        std::size_t selectedCount() const;

        bool isPaused() const;

        // --- the registration surface Scenario forwards to ---
        void addAt(unsigned int tick, std::function<void(ScenarioDriver&)> step);
        void addAfter(unsigned int tick, std::function<void(ScenarioDriver&)> step);
        void setEndTick(unsigned int tick);

    private:
        void runStep(const std::function<void(ScenarioDriver&)>& step);
        void recordFailure(const std::string& message);
        void finish();
        void setMouse(const Point& p);
        void updateHover();
        std::optional<Point> gadgetCentre(const std::string& name);
        UiStagedButton* findGadget(const std::string& name);
        std::optional<Point> screenPositionOf(UnitId unitId) const;

        GameScene& scene;
        std::string name;
        std::optional<unsigned int> lastBefore;
        std::optional<unsigned int> lastAfter;
        unsigned int currentTick{0};
        unsigned int endTick{0};
        bool hasStep{false};
        bool finished{false};
        std::map<unsigned int, std::vector<std::function<void(ScenarioDriver&)>>> beforeSteps;
        std::map<unsigned int, std::vector<std::function<void(ScenarioDriver&)>>> afterSteps;

        /**
         * Which ticks have actually been visited, so finish() can tell a step
         * that ran from one that was skipped. A paused game stops the scene
         * clock, so a tick after the pause can be registered and never arrive.
         */
        std::set<unsigned int> visitedBefore;
        std::set<unsigned int> visitedAfter;

        /**
         * Frames seen, and the budget past which the run is declared stuck.
         *
         * Ticks are simulation ticks and stop while the game is paused, so
         * "wait for tick N" can be a wait that never ends. The budget is the
         * end tick plus thirty seconds of frames: enough for a slow machine,
         * short enough that a stuck run fails instead of hanging a CI job.
         */
        unsigned int framesSeen{0};
        unsigned int maxFrames{0};

        /** Where the cursor is parked between clicks, so the edge-scroll test reads somewhere harmless. */
        Point parkedMouse{0, 0};
    };
}
