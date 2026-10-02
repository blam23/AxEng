local draw_helpers = ax.import("draw_helpers")
local scene_manager = ax.import("scene_manager")
local scene = ax.import("scene")

local menu = scene:new(false)
menu.play_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}
menu.options_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

local pressed = false

function menu:tick(delta)
    self.play_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}
    self.options_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

    local mx, my = mouse.get_position()
    
    if mx > 140 and mx < 360 and my > 240 and my < 320 then
        self.play_color = {r = 0.8, g = 0.0, b = 0.0, a = 1}
        mouse.set_cursor(app.window.handle, mouse.cursors.pointing)
        if not pressed and mouse.is_pressed(mouse.buttons.left) then
            scene_manager:change_scene("game")
            pressed = true
        end
    elseif mx > 140 and mx < 430 and my > 340 and my < 420 then
        self.options_color = {r = 0.8, g = 0.0, b = 0.0, a = 1}
        mouse.set_cursor(app.window.handle, mouse.cursors.pointing)
        if not pressed and mouse.is_pressed(mouse.buttons.left) then
            scene_manager:change_scene("options")
            pressed = true
        end
    else
        mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    end

    if not mouse.is_pressed(mouse.buttons.left) then
        pressed = false
    end
end

function menu:render(delta, pass)
    draw_helpers.ui_shadowed_string(50, 50, "TOWERMANCER", 0, {r = 0.5, g = 0.0, b = 0.7, a = 1}, {x = 5, y = 5}, true)
    draw_helpers.ui_shadowed_string(150, 250, "Play", 0, menu.play_color, {x = 3, y = 3}, true)
    draw_helpers.ui_shadowed_string(150, 350, "Options", 0, menu.options_color, {x = 3, y = 3}, true)
end

return menu
