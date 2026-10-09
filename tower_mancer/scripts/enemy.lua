local draw_helpers = ax.import("draw_helpers")
local texture = app.res.get_texture("tower")
local enemies = {}
enemies.__index = enemies

function enemies:new()
    local instance = setmetatable({}, enemies)
    enemies.update_sections = {}
    enemies.section_count = 3
    enemies.frame_number = 0
    enemies.visible = false
    enemies.processing = false
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
    local sprite_idx = 0
    local hue = 0
    for i = 1, num_enemies do
        local enemy = {}
        enemy.x = math.random(x, x + w)
        enemy.y = math.random(y, y + h)
        enemy.r = 0
        enemy.vx = 0
        enemy.vy = 0
        enemy.vr = 0
        enemy.rand_timer = 0
        enemy.face = math.random(1, wizard_faces)
        local hsla_clothes = vec4:new(hue, math.random(5,8)*0.1, math.random(2,4)*0.1, 1.0)
        enemy.clothes_color = ax.vec4_hsla_to_rgba(hsla_clothes)
        table.insert(self, enemy)
        hue = hue + math.random(1,5)
        hue = hue % 360
    end
end

function enemies:hide()
    self.visible = false
    for i = 1, #self do
        local enemy = self[i]
        app.sprites.free(enemy.sprite)
        app.sprites.free(enemy.clothes_sprite)
        app.sprites.free(enemy.shadow_sprite)

        enemy.sprite = nil
        enemy.clothes_sprite = nil
        enemy.shadow_sprite = nil
    end
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
            table.remove(self, i)
            break
        end
    end
end

function enemies:add(enemy)
    table.insert(self, enemy)
    if self.visible and enemy.sprite == nil then
        enemy.sprite = app.sprites.allocate()
    end
    if self.visible and enemy.clothes_sprite == nil then
        enemy.clothes_sprite = app.sprites.allocate()
    end
    if self.visible and enemy.shadow_sprite == nil then
        enemy.shadow_sprite = app.sprites.allocate()
    end
end

function enemies:swap_to(enemy, new_chunk)
    for i, e in ipairs(self) do
        if e == enemy then
            table.remove(self, i)
            break
        end
    end
    new_chunk.enemies[#new_chunk.enemies + 1] = enemy

    if not new_chunk.visible then
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
    else
        if self.visible and enemy.sprite == nil then
            enemy.sprite = app.sprites.allocate()
        end
        if self.visible and enemy.clothes_sprite == nil then
            enemy.clothes_sprite = app.sprites.allocate()
        end
        if self.visible and enemy.shadow_sprite == nil then
            enemy.shadow_sprite = app.sprites.allocate()
        end
    end
end

function enemies:show()
    self.visible = true
    for i = 1, #self do
        local enemy = self[i]
        enemy.sprite = app.sprites.allocate()
        enemy.clothes_sprite = app.sprites.allocate()
        enemy.shadow_sprite = app.sprites.allocate()

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
end


function enemies:tick(chunk, delta)
    if not self.processing then
        return
    end

    for i = 1, #self do
        local enemy = self[i]
        local ex = enemy.x
        local ey = enemy.y
        enemy.r = math.sin((time + i) * 3.0) * 0.2
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
