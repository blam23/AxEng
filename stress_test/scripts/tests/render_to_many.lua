-- Repeatedly records large sprite and shape batches into several offscreen targets.
local base_test = ax.import("base_test")
local test = base_test:new()

local TARGET_COUNT = 3
local BATCHES_PER_FRAME = 2
local DRAWS_PER_BATCH = 48
local FRAME_COUNT = 1000
local FIXED_DELTA = 1.0 / 60.0

local targets = {}
local source = nil
local shapes = nil
local time = 0
local frame = 0

function test:setup()
    time = 0
    frame = 0
    targets = {}
    source = app.res.get_texture("test")
    shapes = app.shapes.list()

    for i = 1, TARGET_COUNT do
        targets[i] = app.res.create_temp_texture("stress_render_to_many_" .. i, 256, 256)
        if not targets[i].valid then
            log.error("Failed to create stress render target " .. i)
        else
            local initialized = app.window.render_to(targets[i], function(canvas)
                canvas:draw_shapes({
                    type = "rectangle",
                    x = 128,
                    y = 128,
                    width = 256,
                    height = 256,
                    fill = { 0.04 * i, 0.06, 0.1, 1 },
                    screen_space = true,
                })
            end)
            if not initialized then
                log.error("Failed to initialize stress render target " .. i)
            end
        end
    end

    for i = 1, 12 do
        shapes:add({
            type = "rectangle",
            x = (i * 19) % 240,
            y = (i * 31) % 240,
            width = 12 + (i % 5) * 3,
            height = 10 + (i % 4) * 4,
            rotation = i * 0.1,
            fill = { 0.3 + (i % 3) * 0.2, 0.7, 0.4, 0.8 },
            screen_space = true,
        })
    end
end

function test:run()
    test.update_callback = app.on_update.subscribe(function()
        time = time + FIXED_DELTA
        frame = frame + 1

        for batch_index = 1, BATCHES_PER_FRAME do
            local target_index = ((frame + batch_index - 2) % TARGET_COUNT) + 1
            local target = targets[target_index]
            local submitted = app.window.render_to(target, function(canvas)
                for draw_index = 1, DRAWS_PER_BATCH do
                    local x = (draw_index * 17 + frame * 3) % 224
                    local y = (draw_index * 29 + batch_index * 11) % 224
                    if draw_index % 4 == 0 then
                        canvas:draw_ui_texture(source, x, y, draw_index)
                    else
                        canvas:draw_texture(source, x, y, draw_index)
                    end
                end
                shapes:set_position(1, (frame * 5) % 224, (frame * 3) % 224)
                canvas:draw_shapes(shapes)
            end)
            if not submitted then
                log.error("Failed to submit render-to batch " .. batch_index)
            end
        end

        for i = 1, TARGET_COUNT do
            app.window.render_ui(targets[i], 16 + (i - 1) * 264, 16)
        end

        if frame == FRAME_COUNT then
            test.on_complete:fire(time)
        end
    end)
end

function test:teardown()
    if not test.update_callback then
        log.error("Unable to unsubscribe render-to update callback")
        return
    end

    app.call_deferred(function()
        app.on_update.unsubscribe(test.update_callback)
        for i = 1, #targets do
            if targets[i].valid then
                app.res.release_temp_texture(targets[i])
            end
        end
        targets = {}
        shapes = nil
        source = nil
    end)
end

return test
