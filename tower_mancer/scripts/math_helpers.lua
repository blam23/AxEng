local helpers = {}

function helpers.normalize(x, y)
    local length = math.sqrt(x * x + y * y)
    if length == 0 then
        return 0, 0
    end
    return x / length, y / length
end

function helpers.lerp(a, b, t)
    return a + (b - a) * t
end

function helpers.slerp(a, b, t)
    return a + (b - a) * math.sin(t * math.pi * 0.5)
end

return helpers