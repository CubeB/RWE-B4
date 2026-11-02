-- Cost multiplier: the first mod SDK's worked example.
--
-- Preview it without launching a game:
--   rwe_setup --preview-mods examples/mod-cost-multiplier/rwe_mod.json \
--       --data-path <your data directory> \
--       --option com.example.costmultiplier:multiplier=1.5
--
-- rwe.unit_names() is sorted by name, so every peer sees the same order.
-- Values are rounded once when the script finishes; a failed script changes
-- nothing at all.

local multiplier = rwe.options.multiplier

for _, name in ipairs(rwe.unit_names()) do
    local metal = rwe.unit_get(name, "MetalCost")
    local energy = rwe.unit_get(name, "EnergyCost")

    rwe.unit_set(name, "MetalCost", metal * multiplier)
    rwe.unit_set(name, "EnergyCost", energy * multiplier)
end

rwe.log("scaled metal and energy costs by " .. multiplier)
