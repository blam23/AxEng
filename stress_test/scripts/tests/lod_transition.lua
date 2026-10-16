-- Stress generic layered actors switching between a composite and three-layer sprite.
local base_test = ax.import("base_test")
local test = base_test:new()

local ACTOR_COUNT = 8000
local TRANSITION_BATCH = 160
local TRANSITION_INTERVAL = 100
local FRAME_COUNT = 1000
local MIN_DURATION_SECONDS = 5
local FIXED_DELTA = 1.0 / 60.0

local texture = app.res.get_texture("test")
local actors = {}
local sprite_group = nil
local update_callback = nil
local frame = 0
local time = 0
local run_started_at = 0
local target_high_detail = true
local transition_cursor = nil
local cell_width = 0
local cell_height = 0

test.frame_sample_limit = FRAME_COUNT - 1

local function setup_region(sprite, actor, region_x, region_y)
    app.sprites.setup(
        sprite, texture, actor.x, actor.y,
        region_x, region_y, cell_width, cell_height
    )
    sprite.scale.x = 0.35
    sprite.scale.y = 0.35
end

local function set_actor_detail(actor, high_detail)
    if high_detail then
        setup_region(actor.main, actor, 0, 0)
        actor.main.tint.x = 1
        actor.main.tint.y = 1
        actor.main.tint.z = 1
        actor.main.tint.w = 1

        actor.accent = actor.accent or app.sprites.allocate(sprite_group)
        actor.shadow = actor.shadow or app.sprites.allocate(sprite_group)
        setup_region(actor.accent, actor, cell_width, 0)
        setup_region(actor.shadow, actor, cell_width * 2, 0)
        actor.accent.tint = actor.colour
    else
        if actor.accent then
            app.sprites.free(actor.accent)
            actor.accent = nil
        end
        if actor.shadow then
            app.sprites.free(actor.shadow)
            actor.shadow = nil
        end
        setup_region(actor.main, actor, cell_width * 3, cell_height)
        actor.main.tint = actor.colour
    end
    actor.high_detail = high_detail
end

function test:setup()
    math.randomseed(0574355)
    frame = 0
    time = 0
    target_high_detail = true
    transition_cursor = nil
    actors = {}
    sprite_group = app.sprites.group()
    sprite_group:set_visible(true)
    cell_width = math.floor(texture.width / 4)
    cell_height = math.floor(texture.height / 2)

    for i = 1, ACTOR_COUNT do
        local actor = {
            x = math.random(-app.window.width / 2, app.window.width / 2),
            y = math.random(-app.window.height / 2, app.window.height / 2),
            phase = math.random() * math.pi * 2,
            colour = vec4:new(
                math.random() * 0.5 + 0.5,
                math.random() * 0.5 + 0.5,
                math.random() * 0.5 + 0.5,
                1
            ),
        }
        actor.main = app.sprites.allocate(sprite_group)
        set_actor_detail(actor, true)
        actors[i] = actor
    end
end

local function begin_transition()
    target_high_detail = not target_high_detail
    transition_cursor = 1
end

local function advance_transition()
    if not transition_cursor then return end
    local last_actor = math.min(transition_cursor + TRANSITION_BATCH - 1, #actors)
    for i = transition_cursor, last_actor do
        set_actor_detail(actors[i], target_high_detail)
    end
    transition_cursor = last_actor + 1
    if transition_cursor > #actors then
        transition_cursor = nil
    end
end

local function update_actor_positions()
    for i, actor in ipairs(actors) do
        actor.x = math.cos(time + actor.phase) * app.window.width * 0.4
        actor.y = math.sin(time * 0.8 + actor.phase) * app.window.height * 0.4
        local rotation = math.sin(time * 3 + i) * 0.2
        app.sprites.update_position(actor.main, actor.x, actor.y, actor.y, rotation)
        if actor.accent then
            app.sprites.update_position(actor.accent, actor.x, actor.y, actor.y + 1, rotation)
        end
        if actor.shadow then
            app.sprites.update_position(actor.shadow, actor.x + 2, actor.y + 3, actor.y - 1, 0)
        end
    end
end

function test:run()
    run_started_at = app.clock()
    update_callback = app.on_update.subscribe(function()
        frame = frame + 1
        if frame <= FRAME_COUNT then
            time = time + FIXED_DELTA
            if frame % TRANSITION_INTERVAL == 0 and not transition_cursor then
                begin_transition()
            end
            advance_transition()
            update_actor_positions()
        end

        if frame >= FRAME_COUNT and app.clock() - run_started_at >= MIN_DURATION_SECONDS then
            test.on_complete:fire(time)
        end
    end)
end

function test:teardown()
    if not update_callback then
        log.error("Unable to unsubscribe LOD transition stress callback.")
        return
    end

    app.call_deferred(function()
        app.on_update.unsubscribe(update_callback)
        if sprite_group then
            sprite_group:release()
        end
        actors = {}
        sprite_group = nil
        update_callback = nil
    end)
end

return test
