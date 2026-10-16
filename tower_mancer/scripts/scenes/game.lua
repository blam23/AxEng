local draw_helpers = ax.import("draw_helpers")
local math_helpers = ax.import("math_helpers")
local scene = ax.import("scene")
local game = scene:new(true)
local chunk = ax.import("chunk")

local tower = ax.import("tower")

local texture = app.res.get_texture("tower")

local shadow_sprite_area = { 176, 208, 16, 16 }

local grass_tiles = {}

local chunks = {}
local chunk_lookup = {}
local chunk_size = chunk.size
local max_resident_chunks = 160
local next_order = 0

local function tilemap_lod_for_zoom(zoom)
    if zoom >= 2 then return 1024 end
    if zoom >= 0.75 then return 512 end
    return 256
end

local function enemy_detail_for_zoom(zoom)
    return zoom >= 0.75
end

function game:stop()
    for _, c in ipairs(chunks) do c:release() end
    for _, tile in ipairs(grass_tiles) do app.sprites.free(tile.sprite) end
    chunks = {}
    chunk_lookup = {}
    next_order = 0
    grass_tiles = {}
    tower:release()
end

function game:start()
    self:stop()
    assert(bg and bg.submit, "Tower Mancer requires --allow-threads")
    app.window.set_clear_color(0.36, 0.53, 0.22, 1.0)

    mouse.set_cursor(app.window.handle, mouse.cursors.arrow)
    tower:setup(app.window.width/2, app.window.height/2)
end

function game:get_chunk(x, y)
    return chunk_lookup[x .. ":" .. y]
end

local camera = app.window.camera
function game:tick(delta)
    tower:tick(delta)

    local movement = 200 * delta / camera.zoom
    if keyboard.is_pressed(ax.key_map.w) then
        camera:translate(vec2:new(0, -movement))
    end
    if keyboard.is_pressed(ax.key_map.s) then
        camera:translate(vec2:new(0, movement))
    end
    if keyboard.is_pressed(ax.key_map.a) then
        camera:translate(vec2:new(-movement, 0))
    end
    if keyboard.is_pressed(ax.key_map.d) then
        camera:translate(vec2:new(movement, 0))
    end

    if keyboard.is_pressed(ax.key_map.q) then
        camera.zoom = math.min(16, camera.zoom * 1.01 ^ (60 * delta))
    end
    if keyboard.is_pressed(ax.key_map.e) then
        camera.zoom = camera.zoom / 1.01 ^ (60 * delta)
    end

    self:update_chunks()
    for _, c in ipairs(chunks) do c:tick(delta) end
end

function game:resident_chunks()
    return chunks
end

function game:update_chunks()
    -- Bound the visible world span, not the distance the camera can travel.
    local minimum_zoom = math.max(app.window.width, app.window.height) / (6 * chunk_size)
    camera.zoom = math.max(minimum_zoom, math.min(16, camera.zoom))
    local left, top = app.window.global_to_viewport(0, 0)
    local right, bottom = app.window.global_to_viewport(app.window.width, app.window.height)
    local min_x = math.floor((left - 30) / chunk_size)
    local min_y = math.floor((top - 30) / chunk_size)
    local max_x = math.floor((right + 30) / chunk_size)
    local max_y = math.floor((bottom + 30) / chunk_size)
    local tilemap_lod = tilemap_lod_for_zoom(camera.zoom)
    local enemy_high_detail = enemy_detail_for_zoom(camera.zoom)
    local cx = math.floor(camera.position.x / chunk_size)
    local cy = math.floor(camera.position.y / chunk_size)

    local retiring = {}
    for _, c in ipairs(chunks) do
        c.retiring = c.status == "releasing"
            or c.x < min_x - 2 or c.x > max_x + 2
            or c.y < min_y - 2 or c.y > max_y + 2
        if c.retiring then
            c:hide()
            retiring[#retiring + 1] = c
        end
    end
    -- Advance one retiring chunk by a bounded enemy budget each frame.
    if #retiring > 0 then
        table.sort(retiring, function(a, b)
            local da = (a.x - cx) ^ 2 + (a.y - cy) ^ 2
            local db = (b.x - cx) ^ 2 + (b.y - cy) ^ 2
            if da ~= db then return da > db end
            return a.order < b.order
        end)
        local expired = retiring[1]
        local key = expired.x .. ":" .. expired.y
        if chunk_lookup[key] == expired then
            chunk_lookup[key] = nil
        end
        if expired:release(32) then
            if chunk_lookup[key] == expired then
                chunk_lookup[key] = nil
            end
            for i, c in ipairs(chunks) do
                if c == expired then table.remove(chunks, i); break end
            end
        end
    end

    local missing = {}
    for y = min_y - 1, max_y + 1 do
        for x = min_x - 1, max_x + 1 do
            if not self:get_chunk(x, y) then
                missing[#missing + 1] = {
                    x = x, y = y,
                    priority = x >= min_x and x <= max_x and y >= min_y and y <= max_y and 0 or 1,
                    distance = (x - cx) ^ 2 + (y - cy) ^ 2,
                }
            end
        end
    end
    table.sort(missing, function(a, b)
        if a.priority ~= b.priority then return a.priority < b.priority end
        if a.distance ~= b.distance then return a.distance < b.distance end
        if a.y ~= b.y then return a.y < b.y end
        return a.x < b.x
    end)
    for i = 1, math.min(4, #missing, max_resident_chunks - #chunks) do
        local position = missing[i]
        next_order = next_order + 1
        local c = chunk:new(position.x, position.y, next_order)
        c:setup(self, 25)
        chunks[#chunks + 1] = c
        chunk_lookup[c.x .. ":" .. c.y] = c
    end

    local pending = {}
    local staging = {}
    local tile_lod_candidates = {}
    local lod_candidates = {}
    local outstanding = 0
    for _, c in ipairs(chunks) do
        c.desired_visible = not c.retiring and c:onscreen()
        if not c.retiring then
            c:set_tilemap_lod(tilemap_lod)
            if c.desired_visible then
                c.enemies:set_detail(enemy_high_detail)
            end
            c:poll()
        end
        c.priority = c.desired_visible and 0 or
            (app.window.screen_contains_region(
                c.x * chunk_size - chunk_size, c.y * chunk_size - chunk_size,
                chunk_size * 3, chunk_size * 3
            ) and 1 or 2)
        c.distance = (c.x - cx) ^ 2 + (c.y - cy) ^ 2
        if c.desired_visible and c:needs_tilemap_lod() then
            tile_lod_candidates[#tile_lod_candidates + 1] = c
        end
        if c.desired_visible and c.enemies.lod_cursor then
            lod_candidates[#lod_candidates + 1] = c
        end
        if not c.retiring and c.status == "pending" then
            pending[#pending + 1] = c
        elseif not c.retiring and c.status == "generating" then
            outstanding = outstanding + 1
        elseif not c.retiring and c.status == "staging" then
            staging[#staging + 1] = c
        end
    end
    local function nearer(a, b)
        if a.priority ~= b.priority then return a.priority < b.priority end
        if a.distance ~= b.distance then return a.distance < b.distance end
        return a.order < b.order
    end
    table.sort(pending, nearer)
    table.sort(staging, nearer)
    table.sort(tile_lod_candidates, nearer)
    table.sort(lod_candidates, nearer)

    if #tile_lod_candidates > 0 then
        tile_lod_candidates[1]:process_tilemap_lod()
    end

    local lod_budget = 8
    local lod_deadline = app.clock() + 0.0005
    local lod_cursor = self.lod_chunk_cursor or 1
    local lod_idle = 0
    while #lod_candidates > 0
        and lod_budget > 0
        and lod_idle < #lod_candidates
        and app.clock() < lod_deadline do
        if lod_cursor > #lod_candidates then lod_cursor = 1 end
        local processed = lod_candidates[lod_cursor].enemies:process_lod_changes(1, lod_deadline)
        if processed > 0 then
            lod_budget = lod_budget - processed
            lod_idle = 0
        else
            lod_idle = lod_idle + 1
        end
        lod_cursor = lod_cursor % #lod_candidates + 1
    end
    self.lod_chunk_cursor = lod_cursor

    -- Keep the submission window small so camera changes can reprioritize unsent work.
    local submissions = math.min(2 - outstanding, 2 - bg.outstanding())
    for i = 1, math.min(#pending, submissions) do pending[i]:request() end
    local budget = 16
    local deadline = app.clock() + 0.001
    for _, c in ipairs(staging) do
        budget = budget - c:prepare(budget, deadline, c.desired_visible)
        if budget <= 0 or app.clock() >= deadline then break end
    end
    for _, c in ipairs(chunks) do
        if c.desired_visible and c.status == "ready" then c:show() else c:hide() end
    end
    collectgarbage("step", 64)
end

function game:render(delta, pass)
    for _, c in ipairs(chunks) do
        if c.visible then
            c:render(delta, pass)
        end
    end
    draw_helpers.ui_string(5, 5, "000001a", 0, {r = 0.5, g = 1.0, b = 0.7, a = 0.4}, {x = 2, y = 2}, true, 3000)
end

return game
