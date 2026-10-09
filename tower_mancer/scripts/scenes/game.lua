local draw_helpers = ax.import("draw_helpers")
local math_helpers = ax.import("math_helpers")
local scene = ax.import("scene")
local game = scene:new(true)
local chunk = ax.import("chunk")

local tower = ax.import("tower")

local texture = app.res.get_texture("tower")
local grass_texture = app.res.get_texture("grass")

local shadow_sprite_area = { 176, 208, 16, 16 }

local grass_tiles = {}

local chunks = {}

function game:stop()
end

function game:start()
    app.window.set_clear_color(0.36, 0.53, 0.22, 1.0)

    mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    game:setup_grass()
    tower:setup(app.window.width/2, app.window.height/2)

    for y = -20, 20 do
        for x= -20, 20 do
            table.insert(chunks, chunk:new(x, y))
        end
    end

    for _, c in ipairs(chunks) do
        c:setup(50)
        if c:onscreen() then
            c:show()
        end
    end
end

local camera = app.window.camera
local zoom = 1.0
function game:tick(delta)
    tower:tick(delta)

    -- local mx, my = ax.viewport_mouse_position()
    for _, c in ipairs(chunks) do
        c:tick(delta)
        if c:onscreen() then
            if not c.visible then
                c:show()
            end
        else
            if c.visible then
                c:hide()
            end
        end
    end

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
    for _, c in ipairs(chunks) do
        if c.visible then
            c:render(delta, pass)
        end
    end
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
