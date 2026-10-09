-- Immediate shapes: plain Lua description tables are re-submitted (and re-parsed) every frame.
local base_test = ax.import("base_test")
local shape_gen = ax.import("shape_gen")
local test = base_test:new()

local SHAPE_COUNT = 5000
local FRAME_COUNT = 1000

local descriptions = {}
local movers = {}
local time = 0
local frame = 0

function test:setup()
    math.randomseed(0574355)
    time = 0
    frame = 0
    descriptions = {}
    movers = {}
    for i = 1, SHAPE_COUNT do
        local mover = shape_gen.new_mover()
        descriptions[i] = shape_gen.random_shape(mover.x, mover.y)
        movers[i] = mover
    end
end

function test:run()
    test.update_callback = app.on_update.subscribe(function(delta)
        time = time + delta
        frame = frame + 1
        for i = 1, SHAPE_COUNT do
            local mover = movers[i]
            local shape = descriptions[i]
            shape_gen.tick_mover(mover, i, time, delta)
            shape_gen.move(shape, mover.x, mover.y)
            shape.rotation = mover.r
        end

        if frame == FRAME_COUNT then
            test.on_complete:fire(time)
        end
    end)

    test.render_callback = app.window.on_render.subscribe(function(delta, pass)
        app.shapes.draw(pass, descriptions)
    end)
end

function test:teardown()
    local stats = app.shapes.stats()
    log.info(string.format("Immediate shapes: %d draw calls, %d vertices, %d indices", stats.draw_calls, stats.vertices, stats.indices))

    if test.update_callback and test.render_callback then
        app.call_deferred(function()
            app.on_update.unsubscribe(test.update_callback)
            app.window.on_render.unsubscribe(test.render_callback)
            descriptions = {}
            movers = {}
        end)
    else
        log.error("Unable to unsubscribe shape callbacks")
    end
end

return test
