x = 10
y = 10
vx = 0
vy = 0
player_speed = 100

enemies = {}

function setup_enemies(count)
    for i = 1, count do
        local enemy = {}
        enemy.x = math.random(1, 1920)
        enemy.y = math.random(1, 1080)
        enemy.vx = 0
        enemy.vy = 0
        enemy.rand_timer = 0
        table.insert(enemies, enemy)
    end
end

function tick_enemies(delta)
    for i = 1, #enemies do
        local enemy = enemies[i]
        enemy.x = enemy.x + enemy.vx * delta
        enemy.y = enemy.y + enemy.vy * delta
        enemy.rand_timer = enemy.rand_timer - delta
        if enemy.rand_timer <= 0 then
            enemy.vx = math.random(-10, 10)
            enemy.vy = math.random(-10, 10)
            enemy.rand_timer = math.random(1, 3)
        end
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
    for i = 1, #enemies do
        local enemy = enemies[i]
        app.window.render(texture, enemy.x, enemy.y, 68, 33, 8, 15)
    end

    app.window.render(texture, x, y, 49, 32, 13, 15)
end)

setup_enemies(100)

return error_code.Success
