local math_helpers = ax.import("math_helpers")

local game = {}

local player = {
    x = 10,
    y = 10,
    r = 0,
    vx = 0,
    vy = 0,
    speed = 200,
}
local enemies = {}

local texture = app.res.get_texture("tower")
local player_sprite_area = { 49, 32, 13, 15 }
local enemy_sprite_area = { 68, 33, 8, 15 }

game.stop = function()
end

game.start = function()
    game.setup_player()
    game.setup_enemies(200)
end

game.tick = function(delta)
    game.tick_enemies(delta)

    player.vx = 0
    player.vy = 0
    if keyboard.is_pressed(ax.key_map.w) then
        player.vy = -1
    end
    if keyboard.is_pressed(ax.key_map.a) then
        player.vx = -1
    end
    if keyboard.is_pressed(ax.key_map.s) then
        player.vy = 1
    end
    if keyboard.is_pressed(ax.key_map.d) then
        player.vx = 1
    end

    player.vx, player.vy = math_helpers.normalize(player.vx, player.vy)

    player.x = player.x + player.vx * delta * player.speed
    player.y = player.y + player.vy * delta * player.speed
end

game.render = function(delta, pass)
    app.sprites.update_position(player.sprite, player.x, player.y, player.r)
    app.sprites.update_position(player.shadow_sprite, player.x + 5, player.y + 5, player.r)
end

game.setup_player = function()
    player.shadow_sprite = app.sprites.allocate()
    app.sprites.setup(player.shadow_sprite, texture, player.x + 5, player.y + 5, player_sprite_area[1], player_sprite_area[2], player_sprite_area[3], player_sprite_area[4])
    player.shadow_sprite.scale.x = 5.0
    player.shadow_sprite.scale.y = 5.0
    player.shadow_sprite.tint.x = 0.0
    player.shadow_sprite.tint.y = 0.0
    player.shadow_sprite.tint.z = 0.0
    player.shadow_sprite.tint.w = 0.7

    player.sprite = app.sprites.allocate()
    app.sprites.setup(player.sprite, texture, player.x, player.y, player_sprite_area[1], player_sprite_area[2], player_sprite_area[3], player_sprite_area[4])
    player.sprite.scale.x = 5.0
    player.sprite.scale.y = 5.0
end

game.setup_enemies = function(count)
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
        app.sprites.setup(enemy.sprite, texture, enemy.x, enemy.y, enemy_sprite_area[1], enemy_sprite_area[2], enemy_sprite_area[3], enemy_sprite_area[4])
        local randScale = (math.random() + 0.75) * 4
        enemy.sprite.scale.x = randScale
        enemy.sprite.scale.y = randScale
        enemy.sprite.tint.x = math.random() + 0.2
        enemy.sprite.tint.y = math.random() + 0.2
        enemy.sprite.tint.z = math.random() + 0.2
        enemy.sprite.tint.w = 0.4
        table.insert(enemies, enemy)
    end
end

game.tick_enemies = function(delta)
    for i = 1, #enemies do
        local enemy = enemies[i]
        local ex = enemy.x
        local ey = enemy.y

        if (player.x - ex) ^ 2 + (player.y - ey) ^ 2 < 10000 then
            local evx = player.x - ex
            local evy = player.y - ey
            evx, evy = math_helpers.normalize(evx, evy)
            enemy.vx = evx * 100
            enemy.vy = evy * 100
            enemy.r = math.atan(evy, evx)
        end

        enemy.x = ex + enemy.vx * delta
        enemy.y = ey + enemy.vy * delta
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


return game