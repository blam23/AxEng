local draw_helpers = ax.import("draw_helpers")
local math_helpers = ax.import("math_helpers")
local scene = ax.import("scene")
local game = scene:new(true)

local enemies = ax.import("enemy")
local tower = ax.import("tower")

local texture = app.res.get_texture("tower")
local grass_texture = app.res.get_texture("grass")

local shadow_sprite_area = { 176, 208, 16, 16 }

local grass_tiles = {}

function game:stop()
end

function game:start()
    mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    game:setup_grass()
    tower:setup(app.window.width/2, app.window.height/2)
    enemies:setup(10000)
end

local camera = app.window.camera
local zoom = 1.0
function game:tick(delta)
    tower:tick(delta)
    enemies:tick(delta)

    if keyboard.is_pressed(ax.key_map.w) then
        camera:translate(vec2:new(0, -200 * delta))
    end
    if keyboard.is_pressed(ax.key_map.s) then
        camera:translate(vec2:new(0, 200 * delta))
    end
    if keyboard.is_pressed(ax.key_map.a) then
        camera:translate(vec2:new(-200 * delta, 0))
    end
    if keyboard.is_pressed(ax.key_map.d) then
        camera:translate(vec2:new(200 * delta, 0))
    end

    if keyboard.is_pressed(ax.key_map.q) then
        zoom = zoom * 1.01
        camera.zoom = zoom
    end
    if keyboard.is_pressed(ax.key_map.e) then
        zoom = zoom / 1.01
        camera.zoom = zoom
    end
end

function game:render(delta, pass)
    draw_helpers.ui_string(5, 5, "000001a", 0, {r = 0.5, g = 1.0, b = 0.7, a = 0.4}, {x = 2, y = 2}, true, 3000)
end

function game:setup_grass()
    local x = 0
    while x < app.window.width do
        local y = 0
        while y < app.window.height do
            table.insert(grass_tiles, {
                x = x,
                y = y,
                sprite = app.sprites.allocate()
            })
            app.sprites.setup(grass_tiles[#grass_tiles].sprite, grass_texture, x, y, 0, 0, 64, 64)
            grass_tiles[#grass_tiles].sprite.z = -2000
            grass_tiles[#grass_tiles].sprite.scale.x = 2
            grass_tiles[#grass_tiles].sprite.scale.y = 2
            y = y + 128
        end
        x = x + 128
    end
end

return game
