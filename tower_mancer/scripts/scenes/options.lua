local draw_helpers = ax.import("draw_helpers")
local scene_manager = ax.import("scene_manager")
local scene = ax.import("scene")

local options = scene:new(false)
options.back_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

function options:stop()
end

function options:start()
end

local pressed = false

function options:tick(delta)
    self.back_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

    local mx, my = mouse.get_position()
    
    if mx > 140 and mx < 430 and my > 640 and my < 720 then
        self.back_color = {r = 0.8, g = 0.0, b = 0.0, a = 1}
        mouse.set_cursor(app.window.handle, mouse.cursors.pointing)

        if not pressed and mouse.is_pressed(mouse.buttons.left) then
            scene_manager:change_scene("menu")
            pressed = true
        end
    else
        mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    end

    if not mouse.is_pressed(mouse.buttons.left) then
        pressed = false
    end
end

function options:render(delta, pass)
    draw_helpers.shadowed_string(50, 50, "Options", {r = 0.5, g = 0.0, b = 0.7, a = 1}, {x = 5, y = 5}, true)
    draw_helpers.shadowed_string(150, 650, "Back", self.back_color, {x = 3, y = 3}, true)
end

return options
