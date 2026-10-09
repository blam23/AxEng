-- Random shape descriptions shared by the shape stress tests.
local shape_gen = {}

local function random_colour()
    return {
        math.random() * 0.5 + 0.5,
        math.random() * 0.5 + 0.5,
        math.random() * 0.5 + 0.5,
        math.random() < 0.25 and 0.6 or 1.0,
    }
end

local function random_outline()
    if math.random() < 0.5 then
        return { colour = { 0, 0, 0, 1 }, thickness = math.random(1, 3) }
    end
    return nil
end

local function star_points(points, outer, inner)
    local result = {}
    for i = 0, points * 2 - 1 do
        local radius = (i % 2 == 0) and outer or inner
        local angle = i * math.pi / points
        table.insert(result, { math.cos(angle) * radius, math.sin(angle) * radius })
    end
    return result
end

local caps = { "butt", "square", "round" }

shape_gen.kinds = { "circle", "ellipse", "rectangle", "triangle", "polygon", "line" }

-- Returns a world-space shape description centred roughly on (x, y).
-- Lines use x1/y1/x2/y2 so they can be moved by updating plain numbers.
function shape_gen.random_shape(x, y)
    local kind = shape_gen.kinds[math.random(1, #shape_gen.kinds)]
    local shape = {
        type = kind,
        fill = random_colour(),
        outline = random_outline(),
        z = math.random(1, 10),
    }

    if kind == "circle" then
        shape.x, shape.y = x, y
        shape.radius = math.random(4, 16)
    elseif kind == "ellipse" then
        shape.x, shape.y = x, y
        shape.rx, shape.ry = math.random(4, 20), math.random(4, 20)
    elseif kind == "rectangle" then
        shape.x, shape.y = x, y
        shape.width, shape.height = math.random(8, 32), math.random(8, 32)
        shape.corner_radius = math.random() < 0.5 and math.random(1, 4) or 0
    elseif kind == "triangle" then
        shape.x, shape.y = x, y
        shape.points = { { 0, -10 }, { 10, 8 }, { -10, 8 } }
        shape.scale = math.random() * 1.5 + 0.5
    elseif kind == "polygon" then
        shape.x, shape.y = x, y
        shape.points = star_points(math.random(4, 7), 14, 6)
    else
        local angle = math.random() * math.pi * 2
        local length = math.random(10, 40)
        shape.x1, shape.y1 = x, y
        shape.x2, shape.y2 = x + math.cos(angle) * length, y + math.sin(angle) * length
        shape.thickness = math.random(1, 6)
        shape.cap = caps[math.random(1, #caps)]
    end

    return shape
end

-- Moves a description created by random_shape so its anchor is at (x, y).
function shape_gen.move(shape, x, y)
    if shape.type == "line" then
        shape.x2, shape.y2 = shape.x2 + (x - shape.x1), shape.y2 + (y - shape.y1)
        shape.x1, shape.y1 = x, y
    else
        shape.x, shape.y = x, y
    end
end

-- Simple wandering movement shared by the stress tests, matching the sprite tests.
function shape_gen.new_mover()
    return {
        x = math.random(-app.window.width / 2.0, app.window.width / 2.0),
        y = math.random(-app.window.height / 2.0, app.window.height / 2.0),
        r = 0,
        vx = 0,
        vy = 0,
        rand_timer = math.random(),
    }
end

function shape_gen.tick_mover(mover, i, time, delta)
    mover.r = math.sin((time + i) * 3.0) * 0.2
    mover.x = mover.x + mover.vx * delta
    mover.y = mover.y + mover.vy * delta
    mover.rand_timer = mover.rand_timer - delta
    if mover.rand_timer <= 0 then
        mover.vx = math.random(-100, 100) / 10.0
        mover.vy = math.random(-100, 100) / 10.0
        mover.rand_timer = math.random(10, 30) / 10.0
    end
end

return shape_gen
