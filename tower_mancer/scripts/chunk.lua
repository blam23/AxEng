local enemies = ax.import("enemy")

local chunk_size = 1024
local tile_size = 32
local tile_scale = 2
local tile_texture_size = tile_size / tile_scale

chunk = {}
chunk.__index = chunk

local tilesheet = app.res.get_texture("tilesheet")

function chunk:new(cx, cy)
    local noise_scale = 50.0
    local lacunarity = 2.0
    local gain = 0.5
    local octaves = 6
    local amplitude_sum = (1.0 - gain ^ octaves) / (1.0 - gain)

    local instance = setmetatable({}, chunk)
    instance.x = cx
    instance.y = cy
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
            local nx = (x + cx * instance.tile_count_x) / noise_scale
            local ny = (y + cy * instance.tile_count_y) / noise_scale
            local raw_noise = noise.fbm(nx, ny, lacunarity, gain, octaves)
            local normalized_noise = math.max(
                0.0,
                math.min(1.0, 0.5 + 0.5 * raw_noise / amplitude_sum)
            )
            local tile = {
                x = x,
                y = y,
                noise = normalized_noise,
                ty = 0,
            }
            tile.tx = (tile.noise < 0.4) and 1 or 0
            print("Hue: " .. (normalized_noise * 720))
            if tile.tx == 0 then
                tile.hue = normalized_noise * 720
                tile.sat = 0.4
            else
                tile.hue = normalized_noise * 720
                tile.sat = normalized_noise
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
    if self.visible and debug.show_noise then
        for x = 1, self.tile_count_x do
            for y = 1, self.tile_count_y do
                local tile = self.tiles[(y - 1) * self.tile_count_x + x]
                local color = vec4:new(tile.noise, tile.noise, tile.noise, 0.6)
                pass.debug_rect_fill(
                    (self.x * chunk_size) + (x - 1) * tile_size,
                    (self.y * chunk_size) + (y - 1) * tile_size,
                    tile_size,
                    tile_size,
                    color
                )
            end
        end
    end
    if debug.enabled then
        pass.debug_rect_fill(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size, self.debug_color_fill)
        pass.debug_rect_outline(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size, self.debug_color, debug_chunk_outline_thickness)
        pass.debug_rect_outline(self.x * chunk_size - chunk_screen_margin, self.y * chunk_size - chunk_screen_margin, chunk_size + chunk_screen_margin * 2, chunk_size + chunk_screen_margin * 2, self.debug_color, 1.0)
        self.enemies:debug_render(delta, pass)
    end
end

return chunk
