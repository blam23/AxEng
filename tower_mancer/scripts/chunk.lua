local enemies = ax.import("enemy")

local chunk_size = 1024
local tile_size = 32
local tile_scale = 2
local tile_texture_size = tile_size / tile_scale

chunk = {}
chunk.__index = chunk

local tilesheet = app.res.get_texture("tilesheet")

function chunk:new(x, y)
    local instance = setmetatable({}, chunk)
    instance.x = x
    instance.y = y
    instance.visible = true
    instance.enemies = enemies.new()
    instance.debug_color = ax.vec4_hsla_to_rgba(vec4:new(math.random() * 360, 0.6, 0.7, 1.0))
    instance.debug_color_fill = instance.debug_color:copy()
    instance.debug_color_fill.a = 0.1
    instance.reparent_list = {}
    instance.tiles = {}
    instance.tile_count_x = chunk_size / tile_size
    instance.tile_count_y = chunk_size / tile_size
    for y = 0, instance.tile_count_y - 1 do
        for x = 0, instance.tile_count_x - 1 do
            local tile = {
                x = x,
                y = y,
                tx = math.random(0, 1),
                ty = 0,
            }
            if tile.tx == 0 then
                tile.hue = math.random(50,112)
                tile.sat = 0.3
            else
                tile.hue = 0
                tile.sat = 0.0
            end
            table.insert(instance.tiles, tile)
        end
    end
    return instance
end

function chunk:reparent(enemy)
    table.insert(self.reparent_list, enemy)
end

function chunk:add_enemy(enemy)
    self.enemies:add(enemy)
end

function chunk:remove_enemy(enemy)
    self.enemies:remove(enemy)
end

local chunk_screen_margin = 30
function chunk:onscreen()
    return app.window.screen_contains_region((self.x * chunk_size) - chunk_screen_margin, (self.y * chunk_size) - chunk_screen_margin, chunk_size + chunk_screen_margin * 2, chunk_size + chunk_screen_margin * 2)
end

function chunk:contains_point(px, py)
    local left = self.x * chunk_size
    local right = left + chunk_size
    local top = self.y * chunk_size
    local bottom = top + chunk_size
    return px >= left and px <= right and py >= top and py <= bottom
end

function chunk:setup(scene, enemy_count)
    self.scene = scene
    self.enemies:setup(enemy_count, self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size)
end

function chunk:show()
    self.visible = true

    -- re-register sprites
    self.enemies:show()
    self.enemies:enable()
    for _, tile in ipairs(self.tiles) do
        tile.sprite = app.sprites.allocate()
        app.sprites.setup
        (
            tile.sprite,
            tilesheet,
            tile.x * tile_size + self.x * chunk_size,
            tile.y * tile_size + self.y * chunk_size,
            tile.tx * tile_texture_size,
            tile.ty * tile_texture_size,
            tile_texture_size,
            tile_texture_size
        )
        tile.sprite.z = (self.y * chunk_size) - 2000
        tile.sprite.scale.x = tile_scale
        tile.sprite.scale.y = tile_scale
        tile.sprite.tint = ax.vec4_hsla_to_rgba(vec4:new(tile.hue, tile.sat, 0.5, 1.0))
    end
end

function chunk:hide()
    self.visible = false
    self.enemies:hide()
    self.enemies:disable()
    for _, tile in ipairs(self.tiles) do
        app.sprites.free(tile.sprite)
    end
end

function chunk:tick(delta)
    self.reparent_list = {}
    self.enemies:tick(self, delta)

    for _, enemy in ipairs(self.reparent_list) do
        local new_chunk = self.scene:get_chunk(math.floor(enemy.x / chunk_size), math.floor(enemy.y / chunk_size))
        if new_chunk and new_chunk ~= self then
            self.enemies:swap_to(enemy, new_chunk)
        end
    end
end

local debug_chunk_outline_thickness = 2.0
function chunk:render(delta, pass)
    if (debug.enabled) then
        pass.debug_rect_fill(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size, self.debug_color_fill)
        pass.debug_rect_outline(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size, self.debug_color, debug_chunk_outline_thickness)
        pass.debug_rect_outline(self.x * chunk_size - chunk_screen_margin, self.y * chunk_size - chunk_screen_margin, chunk_size + chunk_screen_margin * 2, chunk_size + chunk_screen_margin * 2, self.debug_color, 1.0)
        self.enemies:debug_render(delta, pass)
    end
end

return chunk
