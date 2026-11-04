-- A regenerating shield on every unit: damage is taken from the shield
-- first, and the shield refills at a fixed rate per tick up to its capacity.
-- MOD-SDK-016's worked example of runtime state.
--
-- Everything that decides a future hit is in the engine's store, never in
-- a Lua local or global: the store is saved, hashed and restored, and Lua
-- variables are not. That is what makes a saved and resumed game take the
-- same damage as an uninterrupted one. A unit's entries are deleted with
-- the unit, so a new unit built in its slot starts with a full shield.
--
-- Regeneration is worked out when a unit is hit, from the tick of its last
-- hit, rather than by walking every unit each tick. The result is the same
-- and costs nothing while nobody is shooting.

local capacity = rwe.options.capacity
local regen = rwe.options.regen_per_tick

function hook_damage_v1(victim, attacker, amount, paralyzer, prev)
    -- A paralyzer hit buys stun time, not hit points; leave it alone.
    if paralyzer then
        return prev
    end

    local now = rwe.tick()
    local shield = rwe.unit_get(victim, "shield")
    if shield == nil then
        shield = capacity
    else
        local since = now - rwe.unit_get(victim, "at")
        shield = math.min(capacity, shield + since * regen)
    end

    local absorbed = math.min(shield, prev)
    rwe.unit_set(victim, "shield", shield - absorbed)
    rwe.unit_set(victim, "at", now)
    return prev - absorbed
end
