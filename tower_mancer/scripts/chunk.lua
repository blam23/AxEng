local enemies = ax.import("enemy")
local chunk = {}
chunk.size = 128
chunk.tile_size = 32
local chunk_size = chunk.size
local tile_size = chunk.tile_size
local chunk_screen_margin = 30
local tile_count = math.floor(chunk_size / tile_size)
local cached_tilemap_sizes = { [256] = true, [512] = true, [1024] = true }
local tilesheet = app.res.get_texture("tilesheet")
chunk.__index = chunk

local function release_texture(texture)
    if texture and texture.valid then
        app.res.release_temp_texture(texture)
    end
end

local function release_tilemap_cache(instance)
    if not instance.tilemap_cache then return end
    -- Alternate LOD textures are retained only while the chunk is visible.
    for _, texture in pairs(instance.tilemap_cache) do
        release_texture(texture)
    end
    instance.tilemap_cache = {}
end

function chunk:new(cx, cy, order)
    local instance = setmetatable({}, chunk)
    instance.x = cx
    instance.y = cy
    instance.order = order
    instance.visible = false
    instance.desired_visible = false
    instance.status = "pending"
    instance.enemies = enemies:new()
    instance.tilemap_group = app.sprites.group()
    instance.tilemap_sprite = app.sprites.allocate(instance.tilemap_group)
    instance.debug_color = ax.vec4_hsla_to_rgba(vec4:new(math.random() * 360, 0.6, 0.7, 1.0))
    instance.debug_color_fill = instance.debug_color:copy()
    instance.debug_color_fill.a = 0.1
    instance.reparent_list = {}
    instance.tile_count_x = tile_count
    instance.tile_count_y = tile_count
    return instance
end

function chunk:needs_tilemap_lod()
    if not self.noise or not self.tilemap_size then return false end
    if self.tilemap_pending and self.pending_tilemap_size == self.tilemap_size then return false end
    if self.tilemap_failed_size == self.tilemap_size then return false end
    return self.tilemap_lod ~= self.tilemap_size
end

function chunk:process_tilemap_lod()
    if not self:needs_tilemap_lod() then return false end
    local size = self.tilemap_size

    if not tilesheet.valid then
        log.error("Chunk (" .. self.x .. ", " .. self.y .. ") cannot render its tilemap: missing tilesheet")
        self.tilemap_failed_size = self.tilemap_size
        if not self.tilemap then self.status = "failed" end
        return false
    end

    self.tilemap_cache = self.tilemap_cache or {}
    local cached = self.tilemap_cache[size]
    if cached then
        self.tilemap_cache[size] = nil
        self.pending_tilemap = cached
        self.pending_tilemap_size = size
        self.tilemap_pending = true
        self.tilemap_failed_size = nil
        return true
    end

    self.tilemap_generation = (self.tilemap_generation or 0) + 1
    local name = "tower_chunk_" .. self.x .. "_" .. self.y .. "_" .. self.tilemap_generation
    local target = app.res.create_temp_texture(name, size, size)
    if not target.valid then
        log.error("Chunk (" .. self.x .. ", " .. self.y .. ") cannot create a " .. size .. "x" .. size .. " tilemap")
        self.tilemap_failed_size = size
        if not self.tilemap then self.status = "failed" end
        return false
    end

    local render_scale = size / chunk_size
    local tile_pixels = tile_size * render_scale
    local sprite_scale = 2 * render_scale
    local submitted = app.window.render_to(target, function(canvas)
        for i = 1, tile_count * tile_count do
            local tile_x = (i - 1) % tile_count
            local tile_y = math.floor((i - 1) / tile_count)
            local texture_region_x = self.noise:get(i) < 0.4 and 16 or 0
            local color_index = (i - 1) * 4 + 1
            canvas:draw_ui_texture(
                tilesheet,
                tile_x * tile_pixels,
                tile_y * tile_pixels,
                0, texture_region_x, 0, 16, 16, 0,
                self.colors:get(color_index),
                self.colors:get(color_index + 1),
                self.colors:get(color_index + 2),
                self.colors:get(color_index + 3),
                sprite_scale,
                sprite_scale
            )
        end
    end)
    if not submitted then
        log.error("Chunk (" .. self.x .. ", " .. self.y .. ") failed to submit its tilemap render")
        release_texture(target)
        self.tilemap_failed_size = size
        if not self.tilemap then self.status = "failed" end
        return false
    end

    local superseded = self.pending_tilemap
    self.pending_tilemap = target
    self.pending_tilemap_size = size
    self.tilemap_pending = true
    self.tilemap_failed_size = nil
    release_texture(superseded)
    return true
end

function chunk:set_tilemap_lod(size)
    if self.tilemap_size == size then return end
    self.tilemap_size = size
    self.tilemap_failed_size = nil
end

function chunk:request()
    self.task = bg.submit("generate_chunk", {
        cx = self.x,
        cy = self.y,
        enemy_count = self.enemy_count,
        chunk_size = chunk_size,
        tile_count = tile_count,
    })
    self.status = "generating"
end

function chunk:poll()
    if self.tilemap_pending then
        self.tilemap_pending = false
        if self.pending_tilemap_size == self.tilemap_size then
            local previous = self.tilemap
            local previous_size = self.tilemap_lod
            self.tilemap = self.pending_tilemap
            self.tilemap_lod = self.pending_tilemap_size
            if self.tilemap then
                local width = self.tilemap.width
                local height = self.tilemap.height
                app.sprites.setup(
                    self.tilemap_sprite, self.tilemap,
                    self.x * chunk_size, self.y * chunk_size,
                    0, 0, width, height
                )
                self.tilemap_sprite.scale.x = chunk_size / width
                self.tilemap_sprite.scale.y = chunk_size / height
                self.tilemap_sprite.z = self.y * chunk_size - 2000
            end
            if previous and self.visible and cached_tilemap_sizes[previous_size] then
                local replaced = self.tilemap_cache[previous_size]
                self.tilemap_cache[previous_size] = previous
                release_texture(replaced)
            else
                release_texture(previous)
            end
        else
            release_texture(self.pending_tilemap)
        end
        self.pending_tilemap = nil
        self.pending_tilemap_size = nil
    end
    if self.status ~= "generating" then return end
    local status = self.task:status()
    if status == "succeeded" then
        local result = self.task:take_result()
        self.task = nil
        self.noise = result.noise
        self.colors = result.colors
        self.enemies:set_spawn_data(result.enemies)
        self.status = "staging"
    elseif status == "failed" or status == "cancelled" then
        log.error("Chunk (" .. self.x .. ", " .. self.y .. ") " .. status .. ": " .. self.task:error())
        self.task = nil
        self.status = "failed"
    end
end

function chunk:prepare(enemy_budget, deadline, prepare_enemies)
    if self.status ~= "staging" then return 0 end
    local prepared = 0
    if prepare_enemies then
        prepared = self.enemies:prepare(enemy_budget, deadline)
    end
    if self.tilemap and not self.tilemap_pending and self.enemies:ready() then
        self.status = "ready"
    end
    return prepared
end

function chunk:reparent(enemy)
    table.insert(self.reparent_list, enemy)
end

function chunk:onscreen()
    return app.window.screen_contains_region(
        self.x * chunk_size - chunk_screen_margin, self.y * chunk_size - chunk_screen_margin,
        chunk_size + chunk_screen_margin * 2, chunk_size + chunk_screen_margin * 2)
end

function chunk:contains_point(px, py)
    local left = self.x * chunk_size
    local top = self.y * chunk_size
    return px >= left and px <= left + chunk_size and py >= top and py <= top + chunk_size
end

function chunk:setup(scene, enemy_count)
    self.scene = scene
    self.enemy_count = enemy_count
    self.enemies:setup(enemy_count, self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size)
end

function chunk:show()
    if self.visible or self.status ~= "ready" then return end
    self.visible = true
    self.tilemap_group:set_visible(true)
    self.enemies:show()
    self.enemies:enable()
end

function chunk:hide()
    if not self.visible then return end
    self.visible = false
    self.tilemap_group:set_visible(false)
    release_tilemap_cache(self)
    self.enemies:hide()
    self.enemies:disable()
end

function chunk:release(enemy_budget)
    if self.status == "released" then return true end
    if self.status ~= "releasing" then
        if self.task then self.task:cancel() end
        self.tilemap_group:release()
        release_texture(self.tilemap)
        release_texture(self.pending_tilemap)
        release_tilemap_cache(self)
        self.task = nil
        self.tilemap = nil
        self.pending_tilemap = nil
        self.pending_tilemap_size = nil
        self.tilemap_pending = false
        self.noise = nil
        self.colors = nil
        self.visible = false
        self.status = "releasing"
    end

    local complete = self.enemies:release_step(enemy_budget or math.huge)
    if not complete then return false end
    self.status = "released"
    return true
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

function chunk:render(delta, pass)
    if not self.visible then return end
    if debug.show_noise then
        for y = 0, self.tile_count_y - 1 do
            for x = 0, self.tile_count_x - 1 do
                local value = self.noise:get(y * self.tile_count_x + x + 1)
                pass.debug_rect_fill(self.x * chunk_size + x * tile_size, self.y * chunk_size + y * tile_size,
                    tile_size, tile_size, vec4:new(value, value, value, 0.6))
            end
        end
    end
    if debug.enabled then
        pass.debug_rect_fill(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size, self.debug_color_fill)
        pass.debug_rect_outline(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size, self.debug_color, 2.0)
        pass.debug_rect_outline(self.x * chunk_size - chunk_screen_margin, self.y * chunk_size - chunk_screen_margin,
            chunk_size + chunk_screen_margin * 2, chunk_size + chunk_screen_margin * 2, self.debug_color, 1.0)
        self.enemies:debug_render(delta, pass)
    end
end

return chunk
