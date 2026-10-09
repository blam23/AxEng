local enemies = ax.import("enemy")
local chunk_size = 1024
local tile_size = 32
local chunk_screen_margin = 30
local chunk = {}
chunk.__index = chunk

function chunk:new(cx, cy, order)
    local instance = setmetatable({}, chunk)
    instance.x = cx
    instance.y = cy
    instance.order = order
    instance.visible = false
    instance.desired_visible = false
    instance.status = "pending"
    instance.enemies = enemies:new()
    instance.debug_color = ax.vec4_hsla_to_rgba(vec4:new(math.random() * 360, 0.6, 0.7, 1.0))
    instance.debug_color_fill = instance.debug_color:copy()
    instance.debug_color_fill.a = 0.1
    instance.reparent_list = {}
    instance.tile_count_x = chunk_size / tile_size
    instance.tile_count_y = chunk_size / tile_size
    return instance
end

function chunk:request()
    self.task = bg.submit("generate_chunk", { cx = self.x, cy = self.y })
    self.status = "generating"
end

function chunk:poll()
    if self.status ~= "generating" then return end
    local status = self.task:status()
    if status == "succeeded" then
        local result = self.task:take_result()
        self.task = nil
        local ok, terrain = pcall(app.sprites.attach_batch, "tilesheet", result.sprites, self.order)
        if not ok then
            log.error("Chunk (" .. self.x .. ", " .. self.y .. ") attachment failed: " .. tostring(terrain))
            self.status = "failed"
            return
        end
        self.noise = result.noise
        self.terrain = terrain
        self.status = "staging"
    elseif status == "failed" or status == "cancelled" then
        log.error("Chunk (" .. self.x .. ", " .. self.y .. ") " .. status .. ": " .. self.task:error())
        self.task = nil
        self.status = "failed"
    end
end

function chunk:prepare(enemy_budget, deadline)
    if self.status ~= "staging" then return 0 end
    if self.terrain:error() ~= "" then
        log.error("Chunk (" .. self.x .. ", " .. self.y .. ") staging failed: " .. self.terrain:error())
        self.status = "failed"
        self.terrain:release()
        self.noise = nil
        return 0
    end
    local prepared = self.enemies:prepare(enemy_budget, deadline)
    if self.terrain:ready() and self.enemies:ready() then
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
    self.enemies:setup(enemy_count, self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size)
end

function chunk:show()
    if self.visible or self.status ~= "ready" then return end
    self.visible = true
    self.terrain:set_visible(true)
    self.enemies:show()
    self.enemies:enable()
end

function chunk:hide()
    if not self.visible then return end
    self.visible = false
    self.terrain:set_visible(false)
    self.enemies:hide()
    self.enemies:disable()
end

function chunk:release()
    if self.task then self.task:cancel() end
    if self.terrain then self.terrain:release() end
    self.enemies:release()
    self.task = nil
    self.terrain = nil
    self.noise = nil
    self.visible = false
    self.status = "released"
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
