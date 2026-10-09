-- Retained shapes: a shape_list is built once and updated each frame with the fast setters.
local base_test = ax.import("base_test")
local shape_gen = ax.import("shape_gen")
local test = base_test:new()

local SHAPE_COUNT = 10000
local FRAME_COUNT = 1000

local list = nil
local movers = {}
local time = 0
local frame = 0

function test:setup()
    math.randomseed(0574355)
    time = 0
    frame = 0
    movers = {}
    list = app.shapes.list()
    list:reserve(SHAPE_COUNT)
    for i = 1, SHAPE_COUNT do
        local mover = shape_gen.new_mover()
        list:add(shape_gen.random_shape(mover.x, mover.y))
        movers[i] = mover
    end
end

function test:run()
    test.update_callback = app.on_update.subscribe(function(delta)
        time = time + delta
        frame = frame + 1
        for i = 1, SHAPE_COUNT do
            local mover = movers[i]
            shape_gen.tick_mover(mover, i, time, delta)
            list:set_position(i, mover.x, mover.y)
            list:set_rotation(i, mover.r)
        end

        if frame == FRAME_COUNT then
            test.on_complete:fire(time)
        end
    end)

    test.render_callback = app.window.on_render.subscribe(function(delta, pass)
        app.shapes.draw(pass, list)
    end)
end

function test:teardown()
    local stats = app.shapes.stats()
    log.info(string.format("Retained shapes: %d draw calls, %d vertices, %d indices", stats.draw_calls, stats.vertices, stats.indices))

    if test.update_callback and test.render_callback then
        app.call_deferred(function()
            app.on_update.unsubscribe(test.update_callback)
            app.window.on_render.unsubscribe(test.render_callback)
            list:clear()
            movers = {}
        end)
    else
        log.error("Unable to unsubscribe shape callbacks")
    end
end

return test
