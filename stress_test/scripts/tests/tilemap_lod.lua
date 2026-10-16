-- Cache LOD targets and render each resolution only the first time it is requested.
local base_test = ax.import("base_test")
local test = base_test:new()

local CHUNK_SIZE = 1024
local TILE_SIZE = 32
local TILE_COUNT = CHUNK_SIZE / TILE_SIZE
local LOD_SIZES = { 256, 512, 1024 }
local LOD_INTERVAL = 180
local FRAME_COUNT = 1000
local MIN_DURATION_SECONDS = 5

local texture = app.res.get_texture("test")
local targets = {}
local built_sizes = {}
local sprite_group = nil
local map_sprite = nil
local current_target = nil
local update_callback = nil
local size_index = 0
local frame = 0
local time = 0
local run_started_at = 0
local completed = false

test.frame_sample_limit = FRAME_COUNT - 1

local function rebuild_tilemap(size)
    local target = targets[size]
    if not built_sizes[size] then
        local render_scale = size / CHUNK_SIZE
        local tile_pixels = TILE_SIZE * render_scale
        local sprite_scale = 2 * render_scale
        local submitted = app.window.render_to(target, function(canvas)
            for i = 1, TILE_COUNT * TILE_COUNT do
                local tile_x = (i - 1) % TILE_COUNT
                local tile_y = math.floor((i - 1) / TILE_COUNT)
                local source_x = (tile_x % 2) * 16
                local source_y = (tile_y % 2) * 16
                local tint_r = 0.45 + (tile_x % 4) * 0.12
                local tint_g = 0.4 + (tile_y % 4) * 0.13
                local tint_b = 0.55 + ((tile_x + tile_y) % 3) * 0.12
                canvas:draw_ui_texture(
                    texture,
                    tile_x * tile_pixels,
                    tile_y * tile_pixels,
                    0, source_x, source_y, 16, 16, 0,
                    tint_r, tint_g, tint_b, 1,
                    sprite_scale, sprite_scale
                )
            end
        end)
        if not submitted then
            error("Failed to submit tilemap LOD stress render.")
        end
        built_sizes[size] = true
    end

    current_target = target
    app.sprites.setup(
        map_sprite, target,
        -CHUNK_SIZE / 2, -CHUNK_SIZE / 2,
        0, 0, size, size
    )
    map_sprite.scale.x = CHUNK_SIZE / size
    map_sprite.scale.y = CHUNK_SIZE / size
end

function test:setup()
    frame = 0
    size_index = 0
    time = 0
    completed = false
    current_target = nil
    targets = {}
    built_sizes = {}
    sprite_group = app.sprites.group()
    sprite_group:set_visible(true)
    map_sprite = app.sprites.allocate(sprite_group)
    for _, size in ipairs(LOD_SIZES) do
        targets[size] = app.res.create_temp_texture("stress_tilemap_lod_" .. size, size, size)
        assert(targets[size].valid, "Failed to create tilemap LOD stress target.")
    end
end

function test:run()
    run_started_at = app.clock()
    update_callback = app.on_update.subscribe(function()
        frame = frame + 1
        if frame <= FRAME_COUNT then
            time = time + 1.0 / 60.0
            if frame == 1 then
                size_index = 1
                rebuild_tilemap(LOD_SIZES[size_index])
            elseif frame % LOD_INTERVAL == 0 then
                size_index = size_index % #LOD_SIZES + 1
                rebuild_tilemap(LOD_SIZES[size_index])
            end
        end

        if not completed and frame >= FRAME_COUNT
            and app.clock() - run_started_at >= MIN_DURATION_SECONDS then
            completed = true
            test.on_complete:fire(time)
        end
    end)
end

function test:teardown()
    if not update_callback then
        log.error("Unable to unsubscribe tilemap LOD stress callback.")
        return
    end

    app.call_deferred(function()
        app.on_update.unsubscribe(update_callback)
        if sprite_group then
            sprite_group:release()
        end
        for _, target in pairs(targets) do
            if target.valid then app.res.release_temp_texture(target) end
        end
        sprite_group = nil
        map_sprite = nil
        current_target = nil
        targets = {}
        built_sizes = {}
        update_callback = nil
    end)
end

return test
