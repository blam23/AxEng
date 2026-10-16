local draw_helpers = ax.import("draw_helpers")
local texture = app.res.get_texture("tower")
local enemies = {}
enemies.__index = enemies

function enemies:new()
    local instance = setmetatable({}, enemies)
    instance.visible = false
    instance.processing = false
    instance.pending_count = 0
    instance.high_detail = true
    instance.lod_cursor = nil
    instance.group = app.sprites.group()
    return instance
end

local wizard_sprite_area = { 49, 32, 13, 15 }
local wizard_clothes_area = { 80, 32, 13, 15 }
local wizard_face_area = { 114, 32, 13, 15 }
local wizard_faces = 6
local lizard_sprite_area = { 68, 33, 8, 15 }
local shadow_sprite_area = { 176, 208, 16, 16 }
local projectile_sprite_area = { 112, 240, 4, 4}

local first_names = { "Bob", "Alice", "Charlie", "Diana", "Eve", "Frank", "Gustav", "John", "Trogdor" }
local last_names = { "Smith", "Johnson", "Williams", "Brown", "Jones", "Miller", "Davis", "The Burninator"}

--local sprite_updates = {}
function enemies:setup(num_enemies, x, y, w, h)
    self.pending_count = num_enemies
    self.spawn_cursor = 0
end

function enemies:set_spawn_data(spawn_data)
    self.spawn_data = spawn_data
end

function enemies:ready()
    return self.pending_count == 0
end

function enemies:prepare(budget, deadline)
    local prepared = 0
    while self.pending_count > 0 and prepared < budget and app.clock() < deadline and self.spawn_data do
        local data_index = self.spawn_cursor * 8 + 1
        local enemy = {}
        enemy.x = self.spawn_data:get(data_index)
        enemy.y = self.spawn_data:get(data_index + 1)
        enemy.r = 0
        enemy.vx = 0
        enemy.vy = 0
        enemy.vr = 0
        enemy.rand_timer = 0
        enemy.offset = self.spawn_data:get(data_index + 3)
        enemy.face = self.spawn_data:get(data_index + 2)
        enemy.clothes_color = vec4:new(
            self.spawn_data:get(data_index + 4),
            self.spawn_data:get(data_index + 5),
            self.spawn_data:get(data_index + 6),
            self.spawn_data:get(data_index + 7)
        )
        table.insert(self, enemy)
        self:prepare_sprite(enemy, self.high_detail)
        self.pending_count = self.pending_count - 1
        self.spawn_cursor = self.spawn_cursor + 1
        prepared = prepared + 1
    end
    if self.pending_count == 0 then
        self.spawn_data = nil
    end
    return prepared
end

function enemies:hide()
    self.visible = false
    self.group:set_visible(false)
end

function enemies:disable()
    self.processing = false
end
function enemies:enable()
    self.processing = true
end

function enemies:remove(enemy)
    for i, e in ipairs(self) do
        if e == enemy then
            if enemy.sprite then
                app.sprites.free(enemy.sprite)
            end
            if enemy.clothes_sprite then
                app.sprites.free(enemy.clothes_sprite)
            end
            if enemy.shadow_sprite then
                app.sprites.free(enemy.shadow_sprite)
            end
            enemy.sprite = nil
            enemy.clothes_sprite = nil
            enemy.shadow_sprite = nil
            if self.lod_cursor and i < self.lod_cursor then
                self.lod_cursor = self.lod_cursor - 1
            end
            table.remove(self, i)
            break
        end
    end
end

function enemies:add(enemy)
    table.insert(self, enemy)
    if enemy.sprite then
        app.sprites.transfer(enemy.sprite, self.group)
        if enemy.clothes_sprite then app.sprites.transfer(enemy.clothes_sprite, self.group) end
        if enemy.shadow_sprite then app.sprites.transfer(enemy.shadow_sprite, self.group) end
        if enemy.high_detail ~= self.high_detail then
            self.lod_cursor = self.lod_cursor or #self
        end
    else
        self:prepare_sprite(enemy, self.high_detail)
    end
end

function enemies:swap_to(enemy, new_chunk)
    for i, e in ipairs(self) do
        if e == enemy then
            if self.lod_cursor and i < self.lod_cursor then
                self.lod_cursor = self.lod_cursor - 1
            end
            table.remove(self, i)
            break
        end
    end
    new_chunk.enemies:add(enemy)
end

function enemies:show()
    self.visible = true
    self.group:set_visible(true)
end

function enemies:prepare_sprite(enemy, high_detail)
    enemy.sprite = enemy.sprite or app.sprites.allocate(self.group)
    enemy.sprite.z = 1
    enemy.sprite.scale.x = 2
    enemy.sprite.scale.y = 2

    if high_detail then
        local face_start = wizard_face_area[1] + (enemy.face - 1) * (wizard_face_area[3] + 1)
        app.sprites.setup(
            enemy.sprite, texture, enemy.x, enemy.y,
            face_start, wizard_face_area[2], wizard_face_area[3], wizard_face_area[4]
        )
        enemy.sprite.tint = vec4:new(1, 1, 1, 1)

        enemy.clothes_sprite = enemy.clothes_sprite or app.sprites.allocate(self.group)
        enemy.shadow_sprite = enemy.shadow_sprite or app.sprites.allocate(self.group)
        app.sprites.setup(
            enemy.clothes_sprite, texture, enemy.x, enemy.y,
            wizard_clothes_area[1], wizard_clothes_area[2], wizard_clothes_area[3], wizard_clothes_area[4]
        )
        enemy.shadow_sprite.scale.x = 2
        enemy.shadow_sprite.scale.y = 1
        enemy.shadow_sprite.z = 0
        enemy.clothes_sprite.z = 2
        enemy.clothes_sprite.scale.x = 2
        enemy.clothes_sprite.scale.y = 2
        enemy.shadow_off_y = wizard_face_area[4] * enemy.sprite.scale.y - (shadow_sprite_area[4] / 2) + 1
        enemy.shadow_off_x = (wizard_face_area[3] / 2) * enemy.sprite.scale.x - shadow_sprite_area[3]
        app.sprites.setup(
            enemy.shadow_sprite, texture, enemy.x + enemy.shadow_off_x, enemy.y + enemy.shadow_off_y,
            shadow_sprite_area[1], shadow_sprite_area[2], shadow_sprite_area[3], shadow_sprite_area[4]
        )
        enemy.clothes_sprite.tint = enemy.clothes_color
    else
        if enemy.clothes_sprite then
            app.sprites.free(enemy.clothes_sprite)
            enemy.clothes_sprite = nil
        end
        if enemy.shadow_sprite then
            app.sprites.free(enemy.shadow_sprite)
            enemy.shadow_sprite = nil
        end
        app.sprites.setup(
            enemy.sprite, texture, enemy.x, enemy.y,
            wizard_sprite_area[1], wizard_sprite_area[2], wizard_sprite_area[3], wizard_sprite_area[4]
        )
        enemy.sprite.tint = enemy.clothes_color
    end

    enemy.high_detail = high_detail
end

function enemies:set_detail(high_detail)
    if self.high_detail == high_detail then return end
    self.high_detail = high_detail
    self.lod_cursor = #self > 0 and 1 or nil
end

function enemies:process_lod_changes(budget, deadline)
    if not self.lod_cursor then return 0 end
    local processed = 0
    while self.lod_cursor <= #self and processed < budget and app.clock() < deadline do
        local enemy = self[self.lod_cursor]
        if enemy.high_detail ~= self.high_detail then
            self:prepare_sprite(enemy, self.high_detail)
        end
        self.lod_cursor = self.lod_cursor + 1
        processed = processed + 1
    end
    if self.lod_cursor > #self then
        self.lod_cursor = nil
    end
    return processed
end

function enemies:release_step(enemy_budget)
    self.visible = false
    self.processing = false
    self.high_detail = true
    self.pending_count = 0
    self.spawn_data = nil
    self.lod_cursor = nil

    local released = 0
    while #self > 0 and released < enemy_budget do
        local enemy = self[#self]
        if enemy.sprite then
            app.sprites.free(enemy.sprite)
            enemy.sprite = nil
        end
        if enemy.clothes_sprite then
            app.sprites.free(enemy.clothes_sprite)
            enemy.clothes_sprite = nil
        end
        if enemy.shadow_sprite then
            app.sprites.free(enemy.shadow_sprite)
            enemy.shadow_sprite = nil
        end
        self[#self] = nil
        released = released + 1
    end

    if #self > 0 then return false end
    self.group:release()
    return true
end

function enemies:release()
    self:release_step(math.huge)
end

function enemies:tick(chunk, delta)
    if not self.processing then
        return
    end

    for i = 1, #self do
        local enemy = self[i]
        local ex = enemy.x
        local ey = enemy.y
        enemy.r = math.sin((app.clock() + enemy.offset) * 3.0) * 0.2
        enemy.x = ex + enemy.vx * delta
        enemy.y = ey + enemy.vy * delta
        if not chunk:contains_point(enemy.x, enemy.y) then
            chunk:reparent(enemy)
        end
        enemy.rand_timer = enemy.rand_timer - delta
        if enemy.rand_timer <= 0 then
            enemy.vx = math.random(-100, 100) / 10.0
            enemy.vy = math.random(-100, 100) / 10.0
            enemy.rand_timer = math.random(10, 30) / 10.0
        end

        if self.visible then
            app.sprites.update_position(enemy.sprite, enemy.x, enemy.y, enemy.y, enemy.r)
            if enemy.clothes_sprite then
                app.sprites.update_position(enemy.clothes_sprite, enemy.x, enemy.y, enemy.y, enemy.r)
            end
            if enemy.shadow_sprite then
                app.sprites.update_position(enemy.shadow_sprite, enemy.x + enemy.shadow_off_x, enemy.y + enemy.shadow_off_y, enemy.y + enemy.shadow_off_y - 1000, 0)
            end
        end
    end
end

function enemies:debug_render(delta, pass)
    if not self.processing then
        return
    end
    for i = 1, #self do
        local enemy = self[i]
        pass.debug_rect_outline(enemy.x, enemy.y, wizard_sprite_area[3] * enemy.sprite.scale.x, wizard_sprite_area[4] * enemy.sprite.scale.y, enemy.clothes_color, 1.0)
    end
end

return enemies
