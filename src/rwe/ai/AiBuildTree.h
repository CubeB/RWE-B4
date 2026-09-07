#pragma once

#include <map>
#include <set>
#include <string>

namespace rwe
{
    class BuilderGuisDatabase;

    /**
     * What each builder is allowed to build.
     *
     * The engine enforces no tech tree at all: UnitBehaviorService takes any
     * type name a BuildOrder carries and puts up a nanoframe, and nothing
     * asks whether this builder could have ordered it. What constrains a
     * player is the build menu -- `<unitname><n>.GUI`, whose gadget names are
     * the unit types that page offers -- and the loader already parses those
     * for the human's panels. So the tree is in the data, is already read,
     * and the AI simply never consulted it.
     *
     * Without it the AI cheats in small ways that are hard to see and quite
     * unfair: the commander's pages stop at the level-one plants, yet the AI
     * had it building Defenders, which no player can do. With it, the same
     * table is also what makes level two possible, because the advanced
     * constructor is the only thing that reaches fusion and the moho
     * extractor. See docs/ai-architecture-proposal.md §15.2.
     *
     * Read-only data derived from the game files, identical on every peer and
     * never mutated after load, so it is no more part of the simulation's
     * state than the unit definitions are.
     */
    struct AiBuildTree
    {
        /** Buildable unit types by builder type, both as the data names them. */
        std::map<std::string, std::set<std::string>> buildableBy;

        /**
         * Whether the tree has anything to say about this builder. A builder
         * with no build menu in the data -- a mod's own, or a unit whose GUI
         * files are missing -- is unknown, and callers let it build anything
         * rather than nothing, so missing data cannot silently produce an AI
         * that sits still.
         */
        bool knows(const std::string& builderType) const;

        /** True when the builder may build the type, or when the tree does not know the builder. */
        bool canBuild(const std::string& builderType, const std::string& unitType) const;
    };

    /**
     * Reads the tree out of the loaded build menus.
     *
     * A page's gadgets are a mixture of build buttons and controls (the page
     * turners, the order buttons, the panel itself), and the data does not
     * mark which is which in any way that survives parsing. What separates
     * them is that a build button is named after a unit: every gadget whose
     * name matches a defined unit type is taken as one, and everything else
     * is a control. `unitTypes` is that set, normally the keys of
     * `GameSimulation::unitDefinitions`.
     */
    AiBuildTree buildTreeFromBuilderGuis(const BuilderGuisDatabase& guis, const std::set<std::string>& unitTypes);
}
