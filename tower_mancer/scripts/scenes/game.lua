local math_helpers = ax.import("math_helpers")
local scene = ax.import("scene")
local game = scene:new(true)

local enemies = ax.import("enemy")
local tower = {}

local texture = app.res.get_texture("tower")
local grass_texture = app.res.get_texture("grass")

local tower_sprite_area = { 0, 0, 48, 48 }
local shadow_sprite_area = { 176, 208, 16, 16 }

local grass_tiles = {}

function game:stop()
end

function game:start()
    mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    game:setup_grass()
    game:setup_tower()
    
    enemies:setup(200)
end

function game:tick(delta)
    enemies:tick(delta)
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
            grass_tiles[#grass_tiles].sprite.z = -2
            grass_tiles[#grass_tiles].sprite.scale.x = 2
            grass_tiles[#grass_tiles].sprite.scale.y = 2
            y = y + 128
        end
        x = x + 128
    end
end

function game:setup_tower()
    tower.sprite = app.sprites.allocate()
    tower.sprite.scale.x = 2
    tower.sprite.scale.y = 2
    tower.sprite.z = (1080 / 2) + (tower_sprite_area[4] * tower.sprite.scale.y)
    app.sprites.setup(tower.sprite, texture, (1920 / 2) - 48, (1080 / 2) - 48, tower_sprite_area[1], tower_sprite_area[2], tower_sprite_area[3], tower_sprite_area[4])
end

return game
