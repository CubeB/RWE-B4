#pragma once

#include <random>
#include <rwe/sim/GameSimulation.h>
#include <string>

namespace rwe
{
    // MOD-11: the second subsystem contract. It owns the four wind values and
    // is the first provider that draws from the simulation's random stream.
    struct WindInput
    {
        GameTime now;
        GameTime nextChange;
        SimAngle direction;
        SimScalar generationFactor;
        SimVector vector;
        int minSpeed;
        int maxSpeed;
    };

    struct WindState
    {
        bool changed = false;
        GameTime nextChange;
        SimAngle direction;
        SimScalar generationFactor;
        SimVector vector;
        int speed = 0;
    };

    class WindProvider
    {
    public:
        virtual ~WindProvider() = default;
        virtual std::string id() const = 0;
        virtual WindState update(const WindInput& input, std::minstd_rand& rng) const = 0;
    };

    class NativeWindProvider final : public WindProvider
    {
    public:
        std::string id() const override;
        WindState update(const WindInput& input, std::minstd_rand& rng) const override;
    };
}
