local draw_helpers = ax.import("draw_helpers")
local texture = app.res.get_texture("tower")
local enemies = {}

local wizard_sprite_area = { 49, 32, 13, 15 }
local wizard_clothes_area = { 80, 32, 13, 15 }
local wizard_face_area = { 114, 32, 13, 15 }
local wizard_faces = 6
local lizard_sprite_area = { 68, 33, 8, 15 }
local shadow_sprite_area = { 176, 208, 16, 16 }
local projectile_sprite_area = { 112, 240, 4, 4}

local first_names = { "Bob", "Alice", "Charlie", "Diana", "Eve", "Frank", "Gustav", "John", "Trogdor" }
local last_names = { "Smith", "Johnson", "Williams", "Brown", "Jones", "Miller", "Davis", "The Burninator"}

local update_sections = {}
local section_count = 3

--local sprite_updates = {}
function enemies:setup(num_enemies)
    local sprite_idx = 0
    for i = 1, num_enemies do
        local enemy = {}
        enemy.name = first_names[math.random(1, #first_names)] .. " " .. last_names[math.random(1, #last_names)]
        enemy.name_color = { r = math.random() + 0.5, g = math.random() + 0.5, b = math.random() + 0.5, a = 1.0 }
        enemy.x = math.random(-5000, 5000)
        enemy.y = math.random(-5000, 5000)
        enemy.r = 0
        enemy.vx = 0
        enemy.vy = 0
        enemy.vr = 0
        enemy.rand_timer = 0
        enemy.sprite = app.sprites.allocate()
        enemy.shadow_sprite = app.sprites.allocate()
        enemy.clothes_sprite = app.sprites.allocate()
        local face = math.random(1, wizard_faces)
        local sprite_area = wizard_face_area--((math.random() > 0.5) and lizard_sprite_area  or wizard_sprite_area)
        local face_start = sprite_area[1] + (face - 1) * (sprite_area[3] + 1)
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
        local hsla_clothes = vec4:new(math.random() * 360, 0.3, 0.4, 1.0)
        --local hsla_skin = vec4:new(math.random() * 360, math.random() * 0.4 + 0.2, (math.random() * 0.3) + 0.6, 1.0)
        enemy.clothes_sprite.tint = ax.vec4_hsla_to_rgba(hsla_clothes)
        --enemy.sprite.tint = ax.vec4_hsla_to_rgba(hsla_skin)

        --sprite_idx = sprite_idx + 1
        --sprite_updates[sprite_idx] = { sprite = enemy.sprite, x = enemy.x, y = enemy.y, r = enemy.r }
        --sprite_idx = sprite_idx + 1
        --sprite_updates[sprite_idx] = { sprite = enemy.shadow_sprite, x = enemy.x + enemy.shadow_off_x, y = enemy.y + enemy.shadow_off_y, r = 0 }
        table.insert(enemies, enemy)
    end

    local start = 0
    local end_ = math.floor(#enemies / section_count)
    for i = 1, section_count do
        update_sections[i] = { start, end_ }
        start = end_ + 1
        end_ = math.floor(#enemies / section_count * (i + 1))
    end

    ax.print_table(update_sections)
end

local last_found = {}
local last_ticks = {}
for i = 1, section_count do
    last_ticks[i] = 0
end
local frame_number = 0

function enemies:tick(delta)

    local mx, my = ax.viewport_mouse_position()

    local found = {}
    --local sprite_idx = 0

    frame_number = frame_number + 1

    last_ticks[frame_number % section_count + 1] = delta
    local sum_delta = 0
    for i = 1, section_count do
        sum_delta = sum_delta + last_ticks[i]
    end

    local loop_start = update_sections[frame_number % section_count + 1][1] + 1
    local loop_end = update_sections[frame_number % section_count + 1][2]

    for i = loop_start, loop_end do
        local enemy = enemies[i]
        local ex = enemy.x
        local ey = enemy.y
        enemy.r = math.sin((time + i) * 3.0) * 0.2
        enemy.x = ex + enemy.vx * sum_delta
        enemy.y = ey + enemy.vy * sum_delta
        enemy.rand_timer = enemy.rand_timer - sum_delta
        if enemy.rand_timer <= 0 then
            enemy.vx = math.random(-100, 100) / 10.0
            enemy.vy = math.random(-100, 100) / 10.0
            enemy.rand_timer = math.random(10, 30) / 10.0
        end

        -- sprite_idx = sprite_idx + 1
        -- sprite_updates[sprite_idx].x = enemy.x
        -- sprite_updates[sprite_idx].y = enemy.y
        -- sprite_updates[sprite_idx].r = enemy.r
        -- sprite_idx = sprite_idx + 1
        -- sprite_updates[sprite_idx].x = enemy.x + enemy.shadow_off_x
        -- sprite_updates[sprite_idx].y = enemy.y + enemy.shadow_off_y

        app.sprites.update_position(enemy.sprite, enemy.x, enemy.y, enemy.y, enemy.r)
        app.sprites.update_position(enemy.clothes_sprite, enemy.x, enemy.y, enemy.y, enemy.r)
        app.sprites.update_position(enemy.shadow_sprite, enemy.x + enemy.shadow_off_x, enemy.y + enemy.shadow_off_y, enemy.y + enemy.shadow_off_y - 1000, 0)
    end

    -- for i = 1, #enemies do
    --     local enemy = enemies[i]
    --     if mx > enemy.x and mx < enemy.x + 32 and my > enemy.y and my < enemy.y + 32 then
    --         found[enemy] = true
    --     end
    -- end

    -- --app.sprites.update_positions(sprite_updates)

    -- for enemy, _ in pairs(found) do
    --     local ex2, ey2 = app.window.viewport_to_global(enemy.x - (enemy.shadow_off_x * 0.5), enemy.y)
    --     ex2 = ex2 - (draw_helpers.string_width(enemy.name, 1.5) * 0.5)
    --     draw_helpers.ui_shadowed_string(ex2, ey2 - 20, enemy.name, 0, enemy.name_color, {x = 1.5, y = 1.5}, true, 2000)
    --     enemy.sprite.tint = vec4:new(1.0, 0.0, 0.0, 1.0)
    -- end

    -- for enemy, _ in pairs(last_found) do
    --     if not found[enemy] then
    --         enemy.sprite.tint = vec4:new(1.0, 1.0, 1.0, 1.0)
    --     end
    -- end

    -- last_found = found
end

return enemies
