local draw_helpers = ax.import("draw_helpers")

local options = {}
options.back_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

options.stop = function()
end

options.start = function()
end

local pressed = false

options.tick = function(delta)
    options.back_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

    local mx, my = mouse.get_position()
    
    if mx > 140 and mx < 430 and my > 640 and my < 720 then
        options.back_color = {r = 0.8, g = 0.0, b = 0.0, a = 1}
        mouse.set_cursor(app.window.handle, mouse.cursors.pointing)

        if not pressed and mouse.is_pressed(mouse.buttons.left) then
            change_scene("menu")
            pressed = true
        end
    else
        mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    end

    if not mouse.is_pressed(mouse.buttons.left) then
        pressed = false
    end
end

options.render = function(delta, pass)
    draw_helpers.shadowed_string(50, 50, "Options", {r = 0.5, g = 0.0, b = 0.7, a = 1}, {x = 5, y = 5}, true)
    draw_helpers.shadowed_string(150, 650, "Back", options.back_color, {x = 3, y = 3}, true)
end

return options