local draw_helpers = ax.import("draw_helpers")

local menu = {}

menu.stop = function()
end

menu.start = function()
end

local pressed = false

menu.tick = function(delta)
    menu.play_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}
    menu.options_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

    local mx, my = mouse.get_position()
    
    if mx > 140 and mx < 360 and my > 240 and my < 320 then
        menu.play_color = {r = 0.8, g = 0.0, b = 0.0, a = 1}
        mouse.set_cursor(app.window.handle, mouse.cursors.pointing)
        if not pressed and mouse.is_pressed(mouse.buttons.left) then
            change_scene("game")
            pressed = true
        end
    elseif mx > 140 and mx < 430 and my > 340 and my < 420 then
        menu.options_color = {r = 0.8, g = 0.0, b = 0.0, a = 1}
        mouse.set_cursor(app.window.handle, mouse.cursors.pointing)
        if not pressed and mouse.is_pressed(mouse.buttons.left) then
            change_scene("options")
            pressed = true
        end
    else
        mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    end

    if not mouse.is_pressed(mouse.buttons.left) then
        pressed = false
    end
end

menu.render = function(delta, pass)
    draw_helpers.shadowed_string(50, 50, "TOWER MANCER", {r = 0.5, g = 0.0, b = 0.7, a = 1}, {x = 5, y = 5}, true)
    draw_helpers.shadowed_string(150, 250, "Play", menu.play_color, {x = 3, y = 3}, true)
    draw_helpers.shadowed_string(150, 350, "Options", menu.options_color, {x = 3, y = 3}, true)
end

return menu