local base_test = ax.import("base_test")
local test = base_test:new()

local texture = app.res.get_texture("test")
local sprites = {}

function test:setup()
    math.randomseed(0574355)
    sprites:setup(10000)
end

function test:run()
    test.update_callback = app.on_update.subscribe(function(delta)
        sprites:tick(delta)
    end)
end

function test:teardown()
    if test.update_callback then
        app.call_deferred(function()
            app.on_update.unsubscribe(test.update_callback)
            for i = 1, #sprites do
                app.sprites.free(sprites[i].sprite)
            end
            sprites = {}
        end)
    else
        log.error("Unable to unsubscribe update callback")
    end
end

function random_sprite_area()
    return {
        math.random(0, texture.width - 1),
        math.random(0, texture.height - 1),
        math.random(16, 32),
        math.random(16, 32)
    }
end

function sprites:setup(num_sprites)
    local sprite_idx = 0
    for i = 1, num_sprites do
        local sprite = {}
        sprite.x = math.random(-app.window.width / 2.0, app.window.width / 2.0)
        sprite.y = math.random(-app.window.height / 2.0, app.window.height / 2.0)
        sprite.r = 0
        sprite.vx = 0
        sprite.vy = 0
        sprite.vr = 0
        sprite.rand_timer = math.random()
        sprite.sprite = app.sprites.allocate()
        local sprite_area = random_sprite_area()
        app.sprites.setup(sprite.sprite, texture, sprite.x, sprite.y, sprite_area[1], sprite_area[2], sprite_area[3], sprite_area[4])
        sprite.sprite.z = 1
        sprite.sprite.tint.x = math.random() * 0.5 + 0.5
        sprite.sprite.tint.y = math.random() * 0.5 + 0.5
        sprite.sprite.tint.z = math.random() * 0.5 + 0.5
        sprite.sprite.tint.w = math.random() * 0.5 + 0.5
        table.insert(sprites, sprite)
    end
end

local time = 0
local frame = 0
function sprites:tick(delta)
    time = time + delta
    frame = frame + 1
    for i = 1, #sprites do
        local sprite = sprites[i]
        sprite.r = math.sin((time + i) * 3.0) * 0.2
        sprite.x = sprite.x + sprite.vx * delta
        sprite.y = sprite.y + sprite.vy * delta
        sprite.rand_timer = sprite.rand_timer - delta
        if sprite.rand_timer <= 0 then
            sprite.vx = math.random(-100, 100) / 10.0
            sprite.vy = math.random(-100, 100) / 10.0
            sprite.vr = math.random(-100, 100) / 10.0
            sprite.rand_timer = math.random(10, 30) / 10.0
        end
    end

    app.sprites.update_positions(sprites)

    if frame == 1000 then
        test.on_complete:fire(time)
    end
end

return test