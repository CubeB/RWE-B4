-- MOD-10 presentation HUD example.
--
-- A presentation mod draws and reads; it never changes the simulation. Every
-- query below goes through the filtered player view, so units the player
-- cannot see are absent from rwe.units(), and rwe.contacts() carries only an
-- id, a position and whether the contact is sonar. There is no rwe.sim, no
-- rwe.units_all, no health on a contact and no player stockpile but the
-- viewer's own.
--
-- Reload while playing: type +ReloadPresentation in the message bar. A
-- replacement that fails to parse leaves this HUD running.
--
-- Keys: click the box to collapse or expand it.

local expanded = rwe.options.expanded
if expanded == nil then
    expanded = true
end

local panelX, panelY = 4, 4
local panelWidth = 164
local lineHeight = 11

local function fmt(value)
    return string.format("%d", value)
end

local function drawPanel()
    local resources = rwe.resources()
    local own = rwe.my_units()
    local selected = rwe.selected()
    local contacts = rwe.contacts()

    local height = expanded and (4 + lineHeight * 4) or (4 + lineHeight)
    rwe.draw_rect(panelX, panelY, panelWidth, height, { r = 0, g = 0, b = 0, a = 190 })
    rwe.draw_rect(panelX, panelY, panelWidth, 1, { r = 90, g = 130, b = 160, a = 255 })
    rwe.draw_text(panelX + 4, panelY + 10,
        "M " .. fmt(resources.metal) .. " / " .. fmt(resources.max_metal),
        { r = 255, g = 230, b = 120, a = 255 })

    if not expanded then
        return
    end

    rwe.draw_text(panelX + 4, panelY + 10 + lineHeight,
        "E " .. fmt(resources.energy) .. " / " .. fmt(resources.max_energy),
        { r = 140, g = 220, b = 255, a = 255 })

    local stalled = ""
    if resources.metal_stalled then stalled = stalled .. " M" end
    if resources.energy_stalled then stalled = stalled .. " E" end
    rwe.draw_text(panelX + 4, panelY + 10 + lineHeight * 2,
        "Units " .. fmt(#own) .. "   Selected " .. fmt(#selected) .. stalled,
        { r = 210, g = 210, b = 210, a = 255 })

    local radar, sonar = 0, 0
    for _, contact in ipairs(contacts) do
        if contact.sonar then
            sonar = sonar + 1
        else
            radar = radar + 1
        end
    end
    rwe.draw_text(panelX + 4, panelY + 10 + lineHeight * 3,
        "Contacts R " .. fmt(radar) .. "  S " .. fmt(sonar),
        { r = 170, g = 255, b = 170, a = 255 })
end

function on_frame(dt)
    drawPanel()
end

function on_mouse_down(x, y, button)
    if button ~= 0 then
        return false
    end
    local height = expanded and (4 + lineHeight * 4) or (4 + lineHeight)
    if x >= panelX and x < panelX + panelWidth and y >= panelY and y < panelY + height then
        expanded = not expanded
        return true
    end
    return false
end

function on_key_down(key)
    -- Left mouse button is 0 in SDL's numbering; keyboard keys arrive as the
    -- same codes SDL sends the game. The example only logs, so it never steals
    -- a key from the native interface.
    return false
end
