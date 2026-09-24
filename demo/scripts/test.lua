x = 10
y = 10
vx = 0
vy = 0
player_speed = 100

enemies = {}

function setup_enemies(count)
    for i = 1, count do
        local enemy = {}
        enemy.x = math.random(-200, 1920 + 200)
        enemy.y = math.random(-200, 1080 + 200)
        enemy.r = math.random() * 2 * math.pi
        enemy.vx = 0
        enemy.vy = 0
        enemy.vr = 0
        enemy.rand_timer = 0
        enemy.sprite = app.sprites.allocate()
        --app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, 68, 33, 8, 15)
        app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, 94, 240, 14, 14)
        app.sprites.setup(enemy.sprite, texture2, enemy.x, enemy.y)
        --app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, 153, 51, 10, 25)
        --app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, 112, 240, 4, 4)
        local randScale = (math.random() + 0.1)
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
        --enemy.x = enemy.x + enemy.vx * delta
        --enemy.y = enemy.y + enemy.vy * delta
        enemy.r = enemy.r + enemy.vr * delta * math.random()
        --enemy.rand_timer = enemy.rand_timer - delta
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

local function tick(delta)

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
end

app.on_update.subscribe(tick)

app.window.on_render.subscribe(function(delta, pass)
    app.window.render(texture, x, y, 49, 32, 13, 15)
end)


setup_enemies(15000)

app.window.set_clear_color(1.0, 1.0, 1.0, 1.0)

return error_code.Success
