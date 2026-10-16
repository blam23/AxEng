-- Exercise generic chunk texture, sprite-group creation, staged population, and retirement.
local base_test = ax.import("base_test")
local test = base_test:new()

local CHUNK_SIZE = 1024
local ENEMIES_PER_CHUNK = 250
local CHUNKS_PER_SHIFT = 4
local PREPARE_BUDGET = 16
local CREATE_BUDGET = 1
local RELEASE_BUDGET = 32
local SHIFT_INTERVAL = 200
local FRAME_COUNT = 1000
local MIN_DURATION_SECONDS = 5
local MAP_TEXTURE_SIZE = 128

local texture = app.res.get_texture("test")
local active_chunks = {}
local staging_chunks = {}
local retiring_chunks = {}
local update_callback = nil
local next_chunk_id = 0
local pending_chunk_creations = 0
local frame = 0
local time = 0
local run_started_at = 0
local completed = false

test.frame_sample_limit = FRAME_COUNT - 1

local function release_chunk(chunk)
    if chunk.released then return end
    chunk.released = true
    for i = #chunk.actors, 1, -1 do
        local actor = chunk.actors[i]
        if actor.body then app.sprites.free(actor.body) end
        if actor.clothes then app.sprites.free(actor.clothes) end
        if actor.shadow then app.sprites.free(actor.shadow) end
    end
    chunk.group:release()
    if chunk.texture and chunk.texture.valid then
        app.res.release_temp_texture(chunk.texture)
    end
    chunk.actors = {}
    chunk.texture = nil
end

local function chunk_position(id)
    local column = (id - 1) % 2
    local row = math.floor((id - 1) / 2) % 2
    return (column - 0.5) * CHUNK_SIZE, (row - 0.5) * CHUNK_SIZE
end

local function create_chunk()
    next_chunk_id = next_chunk_id + 1
    local x, y = chunk_position(next_chunk_id)
    local chunk = {
        id = next_chunk_id,
        x = x,
        y = y,
        actors = {},
        next_actor = 1,
        group = app.sprites.group(),
    }
    chunk.group:set_visible(true)

    chunk.texture = app.res.create_temp_texture(
        "stress_chunk_stream_" .. chunk.id, MAP_TEXTURE_SIZE, MAP_TEXTURE_SIZE
    )
    assert(chunk.texture.valid, "Failed to create chunk streaming stress texture.")

    chunk.map_sprite = app.sprites.allocate(chunk.group)
    app.sprites.setup(
        chunk.map_sprite, texture,
        chunk.x, chunk.y,
        0, 0, texture.width, texture.height
    )
    chunk.map_sprite.scale.x = CHUNK_SIZE / texture.width
    chunk.map_sprite.scale.y = CHUNK_SIZE / texture.height
    chunk.map_sprite.z = chunk.y - 2000

    table.insert(active_chunks, chunk)
    table.insert(staging_chunks, chunk)
end

local function create_pending_chunks()
    for _ = 1, math.min(CREATE_BUDGET, pending_chunk_creations) do
        create_chunk()
        pending_chunk_creations = pending_chunk_creations - 1
    end
end

local function remove_from_staging(chunk)
    for i = #staging_chunks, 1, -1 do
        if staging_chunks[i] == chunk then
            table.remove(staging_chunks, i)
            return
        end
    end
end

local function create_actor(chunk)
    local actor = {
        x = chunk.x + math.random(0, CHUNK_SIZE),
        y = chunk.y + math.random(0, CHUNK_SIZE),
        phase = math.random() * math.pi * 2,
        body = app.sprites.allocate(chunk.group),
        clothes = app.sprites.allocate(chunk.group),
        shadow = app.sprites.allocate(chunk.group),
    }
    app.sprites.setup(actor.body, texture, actor.x, actor.y, 0, 0, 16, 16)
    app.sprites.setup(actor.clothes, texture, actor.x, actor.y, 16, 0, 16, 16)
    app.sprites.setup(actor.shadow, texture, actor.x + 2, actor.y + 3, 32, 0, 16, 16)
    actor.body.scale.x = 2
    actor.body.scale.y = 2
    actor.clothes.scale.x = 2
    actor.clothes.scale.y = 2
    actor.shadow.scale.x = 2
    actor.shadow.scale.y = 1
    actor.clothes.tint = vec4:new(0.65, 0.8, 0.9, 1)
    actor.body.z = actor.y
    actor.clothes.z = actor.y + 1
    actor.shadow.z = actor.y - 1
    table.insert(chunk.actors, actor)
end

local function start_stream_shift()
    local retire_count = math.min(CHUNKS_PER_SHIFT, math.max(0, #active_chunks - CHUNKS_PER_SHIFT))
    for _ = 1, retire_count do
        local chunk = table.remove(active_chunks, 1)
        remove_from_staging(chunk)
        chunk.group:set_visible(false)
        table.insert(retiring_chunks, chunk)
    end
    pending_chunk_creations = pending_chunk_creations + CHUNKS_PER_SHIFT
end

local function prepare_staging_chunks()
    local budget = PREPARE_BUDGET
    local index = 1
    while index <= #staging_chunks and budget > 0 do
        local chunk = staging_chunks[index]
        while chunk.next_actor <= ENEMIES_PER_CHUNK and budget > 0 do
            create_actor(chunk)
            chunk.next_actor = chunk.next_actor + 1
            budget = budget - 1
        end
        if chunk.next_actor > ENEMIES_PER_CHUNK then
            table.remove(staging_chunks, index)
        else
            index = index + 1
        end
    end
end

local function retire_chunks()
    local budget = RELEASE_BUDGET
    while #retiring_chunks > 0 and budget > 0 do
        local chunk = retiring_chunks[1]
        while #chunk.actors > 0 and budget > 0 do
            local actor = table.remove(chunk.actors)
            if actor.body then app.sprites.free(actor.body) end
            if actor.clothes then app.sprites.free(actor.clothes) end
            if actor.shadow then app.sprites.free(actor.shadow) end
            budget = budget - 1
        end
        if #chunk.actors == 0 then
            table.remove(retiring_chunks, 1)
            release_chunk(chunk)
        end
    end
end

local function update_actor_positions()
    for _, chunk in ipairs(active_chunks) do
        for i, actor in ipairs(chunk.actors) do
            local x = actor.x + math.cos(time + actor.phase) * 4
            local y = actor.y + math.sin(time + actor.phase) * 4
            local rotation = math.sin(time * 3 + i) * 0.15
            app.sprites.update_position(actor.body, x, y, y, rotation)
            app.sprites.update_position(actor.clothes, x, y, y + 1, rotation)
            app.sprites.update_position(actor.shadow, x + 2, y + 3, y - 1, 0)
        end
    end
end

function test:setup()
    math.randomseed(0574355)
    active_chunks = {}
    staging_chunks = {}
    retiring_chunks = {}
    next_chunk_id = 0
    pending_chunk_creations = 0
    frame = 0
    time = 0
    completed = false
end

function test:run()
    run_started_at = app.clock()
    update_callback = app.on_update.subscribe(function()
        frame = frame + 1
        if frame <= FRAME_COUNT then
            time = time + 1.0 / 60.0
            if frame == 1 or (frame % SHIFT_INTERVAL == 0 and frame < FRAME_COUNT) then
                start_stream_shift()
            end
            create_pending_chunks()
            retire_chunks()
            prepare_staging_chunks()
            update_actor_positions()
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
        log.error("Unable to unsubscribe chunk streaming stress callback.")
        return
    end

    app.call_deferred(function()
        app.on_update.unsubscribe(update_callback)
        for _, chunk in ipairs(active_chunks) do release_chunk(chunk) end
        for _, chunk in ipairs(staging_chunks) do release_chunk(chunk) end
        for _, chunk in ipairs(retiring_chunks) do release_chunk(chunk) end
        active_chunks = {}
        staging_chunks = {}
        retiring_chunks = {}
        update_callback = nil
    end)
end

return test
