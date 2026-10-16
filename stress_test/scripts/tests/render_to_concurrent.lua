local SHARED_NAME = "stress_render_to_concurrent_done"
local TARGET_COUNT = 2
local BACKGROUND_BATCH_COUNT = 1000
local BACKGROUND_DRAWS_PER_BATCH = 24
local BACKGROUND_TARGET_PREFIX = "stress_render_to_concurrent_background_"

if app.thread == "background" then
    local done = app.get_or_create_shared(SHARED_NAME)
    local source = app.res.get_texture("test")
    local targets = {}

    for i = 1, TARGET_COUNT do
        targets[i] = app.res.create_temp_texture(BACKGROUND_TARGET_PREFIX .. i, 256, 256)
    end

    local ok, err = pcall(function()
        assert(source.valid, "Missing source texture for background render-to stress")
        for batch_index = 1, BACKGROUND_BATCH_COUNT do
            local target = targets[((batch_index - 1) % TARGET_COUNT) + 1]
            assert(target.valid, "Missing background render target")
            local submitted = app.window.render_to(target, function(canvas)
                for draw_index = 1, BACKGROUND_DRAWS_PER_BATCH do
                    local x = (draw_index * 23 + batch_index) % 224
                    local y = (draw_index * 13 + batch_index * 3) % 224
                    canvas:draw_texture(source, x, y)
                end
                canvas:draw_shapes({
                    type = "circle",
                    x = batch_index % 240,
                    y = (batch_index * 7) % 240,
                    radius = 6,
                    fill = { 0.9, 0.3, 0.2, 0.7 },
                    screen_space = true,
                })
            end)
            assert(submitted, "Background render-to batch was rejected")
        end
    end)

    if not ok then
        log.error("Background render-to stress failed: " .. tostring(err))
    end
    done:set("failed", not ok)
    app.signal(done, "complete")
    app.release_shared(done)
    return error_code.Success
end

local base_test = ax.import("base_test")
local test = base_test:new()

local MAIN_DRAWS_PER_BATCH = 32
local FRAME_COUNT = 1000
local FIXED_DELTA = 1.0 / 60.0
local targets = {}
local source = nil
local done = nil
local worker_finished = false
local time = 0
local frame = 0
local finalizing = false
local final_frame = 0

function test:setup()
    targets = {}
    source = app.res.get_texture("test")
    worker_finished = false
    time = 0
    frame = 0
    finalizing = false
    final_frame = 0
    done = app.get_or_create_shared(SHARED_NAME)
    done:set("failed", false)
    app.on_signal(done, "complete", function()
        worker_finished = true
    end)

    for i = 1, TARGET_COUNT do
        targets[i] = app.res.create_temp_texture("stress_render_to_concurrent_main_" .. i, 256, 256)
        if not targets[i].valid then
            log.error("Failed to create concurrent stress render target " .. i)
        else
            local initialized = app.window.render_to(targets[i], function(canvas)
                canvas:draw_shapes({
                    type = "rectangle",
                    x = 128,
                    y = 128,
                    width = 256,
                    height = 256,
                    fill = { 0.08 * i, 0.04, 0.12, 1 },
                    screen_space = true,
                })
            end)
            if not initialized then
                log.error("Failed to initialize concurrent stress render target " .. i)
            end
        end
    end
end

local function queue_final_image()
    for i = 1, TARGET_COUNT do
        local target = targets[i]
        local cleared = app.window.render_to(target, function(canvas)
            canvas:draw_shapes({
                type = "rectangle",
                x = 128,
                y = 128,
                width = 256,
                height = 256,
                fill = { 0.05 * i, 0.08, 0.16, 1 },
                screen_space = true,
            })
        end)
        if not cleared then
            log.error("Failed to clear final concurrent render target " .. i)
        end
        local drawn = app.window.render_to(target, function(canvas)
            for draw_index = 1, 12 do
                local x = (draw_index * 17 + i * 23) % 240
                local y = (draw_index * 29 + i * 31) % 240
                canvas:draw_texture(source, x, y)
            end
            canvas:draw_shapes({
                { type = "circle", x = 64 * i, y = 96, radius = 20 + i, fill = { 0.9, 0.3, 0.2, 1 }, screen_space = true },
                { type = "rectangle", x = 180, y = 180, width = 36, height = 24, rotation = i * 0.2, fill = { 0.2, 0.8, 0.4, 1 }, screen_space = true },
            })
        end)
        if not drawn then
            log.error("Failed to draw final concurrent render target " .. i)
        end
    end
end

function test:run()
    bg.run_script("test_render_to_concurrent")

    test.update_callback = app.on_update.subscribe(function()
        if frame < FRAME_COUNT then
            frame = frame + 1
            time = time + FIXED_DELTA

            local target = targets[(frame % TARGET_COUNT) + 1]
            local submitted = app.window.render_to(target, function(canvas)
                for draw_index = 1, MAIN_DRAWS_PER_BATCH do
                    local x = (draw_index * 19 + frame * 5) % 224
                    local y = (draw_index * 11 + frame * 7) % 224
                    canvas:draw_ui_texture(source, x, y)
                end
                canvas:draw_shapes({
                    { type = "rectangle", x = frame % 240, y = 24, width = 14, height = 18, fill = { 0.2, 0.5, 1, 0.8 } },
                    { type = "line", x1 = 0, y1 = frame % 240, x2 = 240, y2 = 240, thickness = 2, fill = { 1, 1, 0, 1 } },
                })
            end)
            if not submitted then
                log.error("Main-thread render-to batch was rejected")
            end
        elseif worker_finished then
            if not finalizing then
                finalizing = true
                queue_final_image()
            else
                final_frame = final_frame + 1
            end
        end

        for i = 1, TARGET_COUNT do
            app.window.render_ui(targets[i], 16 + (i - 1) * 264, 300)
        end

        if final_frame == 1 then
            if done:get("failed") then
                log.error("Background render-to stress reported a failure")
            end
            test.on_complete:fire(time)
        end
    end)
end

function test:teardown()
    if not test.update_callback then
        log.error("Unable to unsubscribe concurrent render-to update callback")
        return
    end

    app.call_deferred(function()
        app.on_update.unsubscribe(test.update_callback)
        for i = 1, #targets do
            if targets[i].valid then
                app.res.release_temp_texture(targets[i])
            end
        end
        app.release_shared(done)
        targets = {}
        source = nil
        done = nil
    end)
end

return test
