local draw_helpers = ax.import("draw_helpers")
local texture = app.res.get_texture("tower")
local enemies = {}
enemies.__index = enemies

function enemies:new()
    local instance = setmetatable({}, enemies)
    instance.visible = false
    instance.processing = false
    instance.pending_count = 0
    instance.spawn_hue = 0
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
    self.spawn_region = { x = x, y = y, w = w, h = h }
end

function enemies:ready()
    return self.pending_count == 0
end

function enemies:prepare(budget, deadline)
    local prepared = 0
    local region = self.spawn_region
    while self.pending_count > 0 and prepared < budget and app.clock() < deadline do
        local enemy = {}
        enemy.x = math.random(region.x, region.x + region.w)
        enemy.y = math.random(region.y, region.y + region.h)
        enemy.r = 0
        enemy.vx = 0
        enemy.vy = 0
        enemy.vr = 0
        enemy.rand_timer = 0
        enemy.time = 0
        enemy.face = math.random(1, wizard_faces)
        local hsla_clothes = vec4:new(self.spawn_hue, math.random(3,6)*0.1, math.random(1,3)*0.1, 1.0)
        enemy.clothes_color = ax.vec4_hsla_to_rgba(hsla_clothes)
        table.insert(self, enemy)
        self.spawn_hue = (self.spawn_hue + math.random(1,5)) % 360
        self:prepare_sprite(enemy)
        self.pending_count = self.pending_count - 1
        prepared = prepared + 1
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
            table.remove(self, i)
            break
        end
    end
end

function enemies:add(enemy)
    table.insert(self, enemy)
    if enemy.sprite then
        app.sprites.transfer(enemy.sprite, self.group)
        app.sprites.transfer(enemy.clothes_sprite, self.group)
        app.sprites.transfer(enemy.shadow_sprite, self.group)
    else
        self:prepare_sprite(enemy)
    end
end

function enemies:swap_to(enemy, new_chunk)
    for i, e in ipairs(self) do
        if e == enemy then
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

function enemies:prepare_sprite(enemy)
    enemy.sprite = app.sprites.allocate(self.group)
    enemy.clothes_sprite = app.sprites.allocate(self.group)
    enemy.shadow_sprite = app.sprites.allocate(self.group)

    local sprite_area = wizard_face_area
    local face_start = sprite_area[1] + (enemy.face - 1) * (sprite_area[3] + 1)
    app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, face_start, sprite_area[2], sprite_area[3], sprite_area[4])
    app.sprites.setup(enemy.clothes_sprite, texture, enemy.x, enemy.y, wizard_clothes_area[1], wizard_clothes_area[2], wizard_clothes_area[3], wizard_clothes_area[4])
    enemy.shadow_sprite.scale.x = 2
    enemy.shadow_sprite.scale.y = 1
    enemy.shadow_sprite.z = 0
    enemy.sprite.z = 1
    enemy.sprite.scale.x = 2
    enemy.sprite.scale.y = 2
    enemy.clothes_sprite.z = 2
    enemy.clothes_sprite.scale.x = 2
    enemy.clothes_sprite.scale.y = 2
    enemy.shadow_off_y = sprite_area[4] * enemy.sprite.scale.y - (shadow_sprite_area[4]/2) + 1
    enemy.shadow_off_x = (sprite_area[3] / 2) * enemy.sprite.scale.x - (shadow_sprite_area[3])
    app.sprites.setup(enemy.shadow_sprite, texture, enemy.x + enemy.shadow_off_x, enemy.y + enemy.shadow_off_y, shadow_sprite_area[1], shadow_sprite_area[2], shadow_sprite_area[3], shadow_sprite_area[4])
    enemy.clothes_sprite.tint = enemy.clothes_color
end

function enemies:release()
    self.group:release()
    self.visible = false
    self.processing = false
    for _, enemy in ipairs(self) do
        enemy.sprite = nil
        enemy.clothes_sprite = nil
        enemy.shadow_sprite = nil
    end
end

function enemies:tick(chunk, delta)
    if not self.processing then
        return
    end

    for i = 1, #self do
        local enemy = self[i]
        local ex = enemy.x
        local ey = enemy.y
        enemy.r = math.sin((enemy.time + i) * 3.0) * 0.2
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
        enemy.time = enemy.time + delta

        if self.visible then
            app.sprites.update_position(enemy.sprite, enemy.x, enemy.y, enemy.y, enemy.r)
            app.sprites.update_position(enemy.clothes_sprite, enemy.x, enemy.y, enemy.y, enemy.r)
            app.sprites.update_position(enemy.shadow_sprite, enemy.x + enemy.shadow_off_x, enemy.y + enemy.shadow_off_y, enemy.y + enemy.shadow_off_y - 1000, 0)
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
