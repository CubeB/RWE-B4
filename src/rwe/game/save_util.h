#pragma once

#include <nlohmann/json.hpp>
#include <rwe/sim/GameSimulation.h>

namespace rwe
{
    /**
     * Serializes the mutable state of a simulation to json, and restores it
     * into a freshly built simulation for the same map.
     *
     * The save holds everything the sim mutates as it ticks: players, units
     * (orders, behaviour, navigation, physics, weapons, the COB VM's statics,
     * threads and scheduler queues, piece animation state, the streaming
     * economy buffers), features, projectiles, the RNG, the wind, the game
     * clock and the outstanding path requests. Ids are not stored raw:
     * VectorMap ids carry slot and generation, so the save rewrites every
     * UnitId, FeatureId and ProjectileId reference as a dense index into the
     * saved array, and the load hands out fresh ids in the same order. A
     * reference to something that no longer existed at save time is restored
     * as an id that fails lookup, which is how it behaved before the save.
     *
     * Deliberately not saved:
     * - Definitions, terrain, LOS tables, the metal and geo grids and the
     *   movement class data: load-time constants the caller reconstructs by
     *   building the fresh sim for the same map and mod.
     * - occupiedGrid and flyingUnitsSet: rebuilt from the loaded units and
     *   features through the same writes the add paths make.
     * - playerVisibility: derived; loadSimulationFromJson finishes with
     *   updateVisibility(), and radar contacts refill on the next tick.
     * - events: scene-facing, drained by GameScene, never hashed.
     * - aiControllers, aiPlayerOrder, aiPendingCommands: AI blackboard state
     *   intentionally restarts. The caller re-registers its AI controllers
     *   after loading; they rebuild their plans from what they observe.
     * - A weapon aim thread that had already been killed by a COB signal is
     *   restored as a null thread reference, which fails to reap exactly the
     *   way the dangling pointer it replaces did.
     */
    /**
     * The explored grid as alternating run lengths, starting with unexplored.
     *
     * Exposed for its own test: the grid is the one piece of visibility a load
     * cannot recompute, and an encoder that loses or gains a cell would be
     * invisible inside a whole-simulation round trip.
     */
    nlohmann::json saveExploredGrid(const Grid<unsigned char>& grid);
    void loadExploredGrid(const nlohmann::json& j, Grid<unsigned char>& grid);

    nlohmann::json saveSimulationToJson(const GameSimulation& sim);

    /**
     * Restores a save into `sim`, which must be freshly constructed for the
     * same map with the same wind range and with all definitions loaded
     * (unitDefinitions, weaponDefinitions, unitModelDefinitions,
     * unitScriptDefinitions, featureDefinitions, movement classes, losTables,
     * terrain), the map's initial features placed, and no players added and
     * no units spawned.
     *
     * The map's initial features are deleted and replaced by the saved ones,
     * so the feature array comes out in the saved order whatever the map
     * placed. Throws std::runtime_error if the save is malformed or does not
     * fit the simulation it is being loaded into.
     */
    void loadSimulationFromJson(const nlohmann::json& j, GameSimulation& sim);

    /**
     * Sweeps a played simulation back to what loadSimulationFromJson wants:
     * no players, no units, no projectiles, nothing of theirs left on the
     * grids or in the derived caches. The definitions, terrain and the
     * map's permanent grids stay, because the load does not rebuild them.
     *
     * This is what lets a save be restored into the simulation that made it
     * instead of into a freshly built one -- the replay viewer's keyframes
     * do exactly that, and rebuilding the scene from disk for each of them
     * is the slow path they exist to avoid.
     */
    void clearSimulationForLoad(GameSimulation& sim);
}
