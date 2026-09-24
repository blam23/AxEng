x = 10
y = 10
vx = 0
vy = 0
player_speed = 100

enemies = {}

function setup_enemies(count)
    for i = 1, count do
        local enemy = {}
        enemy.x = math.random(-400, 1920 + 100)
        enemy.y = math.random(-400, 1080 + 100)
        enemy.r = math.random() * 2 * math.pi
        enemy.vx = 0
        enemy.vy = 0
        enemy.vr = 0
        enemy.rand_timer = 0
        enemy.sprite = app.sprites.allocate()
        --app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, 68, 33, 8, 15)
        app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, 94, 240, 14, 14)
        --app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, 153, 51, 10, 25)
        --app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, 112, 240, 4, 4)
        --app.sprites.setup(enemy.sprite, texture2, enemy.x, enemy.y)
        local randScale = (math.random() + 0.75) * 20.0
        enemy.sprite.scale.x = randScale
        enemy.sprite.scale.y = randScale
        enemy.sprite.rotation = math.random() * 2 * math.pi
        enemy.sprite.tint.x = math.random() + 0.2
        enemy.sprite.tint.y = math.random() + 0.2
        enemy.sprite.tint.z = math.random() + 0.2
        enemy.sprite.tint.w = 0.1
        table.insert(enemies, enemy)
    end
end

function tick_enemies(delta)
    for i = 1, #enemies do
        local enemy = enemies[i]
        enemy.x = enemy.x + enemy.vx * delta
        enemy.y = enemy.y + enemy.vy * delta
        enemy.r = enemy.r + enemy.vr * delta * math.random()
        enemy.rand_timer = enemy.rand_timer - delta
        if enemy.rand_timer <= 0 then
            enemy.vx = math.random(-100, 100) / 10.0
            enemy.vy = math.random(-100, 100) / 10.0
            enemy.vr = math.random(-100, 100) / 100.0
            enemy.rand_timer = math.random(10, 30) / 10.0
        end
        app.sprites.update_position(enemy.sprite, enemy.x, enemy.y, enemy.r)
    end
end

function esc_key_event(pressed, mods)
    if pressed then
        window.request_close(app.window.handle)
    end
end

local down = {}
function any_key_event(key, pressed, mods)
    down[key] = pressed
end

keyboard.subscribe_key(ax.key_map.esc, esc_key_event)
keyboard.subscribe_all(any_key_event)

texture = app.res.get_texture("tower")
texture2 = app.res.get_texture("icon")
font_texture = app.res.get_texture("font")

time = 0
local function tick(delta)
    time = time + delta

    tick_enemies(delta)

    vx = 0
    vy = 0
    if down[ax.key_map.w] then
        vy = -100
    end
    if down[ax.key_map.a] then
        vx = -100
    end
    if down[ax.key_map.s] then
        vy = 100
    end
    if down[ax.key_map.d] then
        vx = 100
    end

    x = x + vx * delta
    y = y + vy * delta

    app.sprites.update_position(player_sprite, x, y, math.sin(time))
    app.sprites.update_position(player_shadow_sprite, x + 5, y + 5, math.sin(time))
end

function draw_string(x, y, text, rotation, color, scale, bold)
    local y_offset = 0
    local x_offset = 0
    for i = 1, #text do
        local c = string.byte(text, i)
        local stride = 16
        local height = 16
        local lowercase = false
        if c > 64 and c < 91 then
            c = c - 65
        elseif c > 96 and c < 123 then
            c = c - 97
            lowercase = true
        elseif c > 47 and c < 58 then
            c = c - 22 -- (48-26)
        elseif c == 10 then
            c = 999
            y_offset = y_offset + 1
            x_offset = -((stride + 2.0) * scale.x)
        else
            c = 999
        end
        local row = math.floor(c / 16) 
        local col = c % 16
        if lowercase then
            row = 3
            col = c % 32
            stride = 8
            height = 32
        end
        if bold then
            row = row + 5
        end
        if c ~= 999 then
            app.window.render(
                font_texture, x + x_offset, y + (18 * y_offset * scale.y),
                stride * col, 16 * row, stride, height,
                rotation, 0,
                color.r, color.g, color.b, color.a,
                scale.x, scale.y
            )
        end
        x_offset = x_offset + ((stride + 2.0) * scale.x)
    end
end

function draw_shadowed_string(x,y, text, color, scale, bold)
    draw_string(x + (scale.x), y + (scale.y), text, 0, {r = 0, g = 0, b = 0, a = 0.2}, scale, bold)
    draw_string(x, y, text, 0, color, scale, bold)
end

app.on_update.subscribe(tick)

app.window.on_render.subscribe(function(delta, pass)
    draw_shadowed_string(50, 50, "ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n\n0123456789", {r = 0.7, g = 1.2, b = 1.2, a = 1}, {x = 2, y = 2}, false)
    draw_shadowed_string(50, 350, "ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n\n0123456789", {r = 0.7, g = 1.2, b = 1.2, a = 1}, {x = 2, y = 2}, true)
    draw_shadowed_string(50, 550, "You can feel your tongue\nin your mouth", {r = 0.5, g = 0.0, b = 0.7, a = 1}, {x = 5, y = 5}, true)
end)

setup_enemies(500)

player_shadow_sprite = app.sprites.allocate()
app.sprites.setup(player_shadow_sprite, texture, x + 5, y + 5, 49, 32, 13, 15)
player_shadow_sprite.scale.x = 5.0
player_shadow_sprite.scale.y = 5.0
player_shadow_sprite.tint.x = 0.0
player_shadow_sprite.tint.y = 0.0
player_shadow_sprite.tint.z = 0.0
player_shadow_sprite.tint.w = 0.7

player_sprite = app.sprites.allocate()
app.sprites.setup(player_sprite, texture, x, y, 49, 32, 13, 15)
player_sprite.scale.x = 5.0
player_sprite.scale.y = 5.0

app.window.set_clear_color(1.0, 1.0, 1.0, 1.0)

return error_code.Success
