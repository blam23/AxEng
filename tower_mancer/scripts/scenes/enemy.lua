local math_helpers = ax.import("math_helpers")
local scene = ax.import("scene")

local texture = app.res.get_texture("tower")
local enemies = {}

local wizard_sprite_area = { 49, 32, 13, 15 }
local lizard_sprite_area = { 68, 33, 8, 15 }
local shadow_sprite_area = { 176, 208, 16, 16 }

function enemies:setup(count)
    for i = 1, count do
        local enemy = {}
        enemy.x = math.random(-400, 1920 + 100)
        enemy.y = math.random(-400, 1080 + 100)
        enemy.r = 0
        enemy.vx = 0
        enemy.vy = 0
        enemy.vr = 0
        enemy.rand_timer = 0
        enemy.sprite = app.sprites.allocate()
        enemy.shadow_sprite = app.sprites.allocate()
        local sprite_area = ((math.random() > 0.5) and lizard_sprite_area  or wizard_sprite_area)
        app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, sprite_area[1], sprite_area[2], sprite_area[3], sprite_area[4])
        enemy.shadow_sprite.scale.x = 2
        enemy.shadow_sprite.scale.y = 1
        enemy.shadow_sprite.z = 0
        enemy.sprite.z = 1
        enemy.sprite.scale.x = 2
        enemy.sprite.scale.y = 2
        enemy.shadow_off_y = sprite_area[4] * enemy.sprite.scale.y - (shadow_sprite_area[4]/2)
        enemy.shadow_off_x = (sprite_area[3] / 2) * enemy.sprite.scale.x - (shadow_sprite_area[3])
        app.sprites.setup(enemy.shadow_sprite, texture, enemy.x + enemy.shadow_off_x, enemy.y + enemy.shadow_off_y, shadow_sprite_area[1], shadow_sprite_area[2], shadow_sprite_area[3], shadow_sprite_area[4])
        --enemy.sprite.tint.x = math.random() + 0.2
        --enemy.sprite.tint.y = math.random() + 0.2
        --enemy.sprite.tint.z = math.random() + 0.2
        --enemy.sprite.tint.w = 1.0
        table.insert(enemies, enemy)
    end
end

function enemies:tick(delta)
    for i = 1, #enemies do
        local enemy = enemies[i]
        local ex = enemy.x
        local ey = enemy.y
        enemy.r = math.sin((time + i) * 3.0) * 0.2
        enemy.x = ex + enemy.vx * delta
        enemy.y = ey + enemy.vy * delta
        enemy.rand_timer = enemy.rand_timer - delta
        if enemy.rand_timer <= 0 then
            enemy.vx = math.random(-100, 100) / 10.0
            enemy.vy = math.random(-100, 100) / 10.0
            enemy.rand_timer = math.random(10, 30) / 10.0
        end
        app.sprites.update_position(enemy.sprite, enemy.x, enemy.y, enemy.r)
        app.sprites.update_position(enemy.shadow_sprite, enemy.x + enemy.shadow_off_x, enemy.y + enemy.shadow_off_y)
        enemy.sprite.z = enemy.y + enemy.shadow_off_y
    end
end

return enemies
