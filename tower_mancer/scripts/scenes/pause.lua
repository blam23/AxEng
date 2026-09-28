local draw_helpers = ax.import("draw_helpers")

local paused = {}
paused.back_color = {r = 0.0, g = 0.8, b = 0.8, a = 1}

function paused:stop()
end

function paused:start()
end

local pressed = false

function paused:tick(delta)
end

local pause_texture = app.res.get_texture("paused")

function paused:render(delta, pass)
    local x = 0
    local y = 0
    while x < app.window.width do
        y = 0
        while y < app.window.height do
            app.window.render(pause_texture, x, y, 500)
            y = y + 128
        end
        x = x + 128
    end

    draw_helpers.shadowed_string(50, 50, "Paused", {r = 0.5, g = 0.0, b = 0.7, a = 1}, {x = 5, y = 5}, true, 501)
    draw_helpers.shadowed_string(150, 650, "Back", self.back_color, {x = 3, y = 3}, true, 501)
end

return paused