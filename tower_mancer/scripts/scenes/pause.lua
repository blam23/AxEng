local draw_helpers = ax.import("draw_helpers")
local scene_manager = ax.import("scene_manager")
local scene = ax.import("scene")

local paused = scene:new(false)
paused.back_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

local pressed = false
local pause_texture = app.res.get_texture("paused")

function paused:tick(delta)
    self.back_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

    local mx, my = mouse.get_position()
    
    if mx > 140 and mx < 430 and my > 640 and my < 720 then
        self.back_color = {r = 0.8, g = 0.0, b = 0.0, a = 1}
        mouse.set_cursor(app.window.handle, mouse.cursors.pointing)

        if not pressed and mouse.is_pressed(mouse.buttons.left) then
            scene_manager:unpause()
            pressed = true
        end
    else
        mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    end

    if not mouse.is_pressed(mouse.buttons.left) then
        pressed = false
    end
end

function paused:render(delta, pass)
    local x = 0
    local y = 0
    while x < app.window.width do
        y = 0
        while y < app.window.height do
            app.window.render_ui(pause_texture, x, y, 1900)
            y = y + 128
        end
        x = x + 128
    end

    draw_helpers.ui_shadowed_string(50, 50, "Paused", 0, {r = 0.5, g = 0.0, b = 0.7, a = 1}, {x = 5, y = 5}, true, 2000)
    draw_helpers.ui_shadowed_string(150, 650, "Back", 0, self.back_color, {x = 3, y = 3}, true, 2000)
end

return paused
