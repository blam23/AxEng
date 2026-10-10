return function(request, task)
    local sprites = bg.sprite_buffer(1024)
    local values = bg.number_buffer(1024)
    local amplitude_sum = (1.0 - 0.5 ^ 6) / (1.0 - 0.5)
    for y = 0, 31 do
        if task:cancelled() then return {} end
        for x = 0, 31 do
            local nx = (x + request.cx * 32) / 50.0
            local ny = (y + request.cy * 32) / 50.0
            local raw_noise = noise.fbm(nx, ny, 2.0, 0.5, 6)
            local value = math.max(0.0, math.min(1.0, 0.5 + 0.5 * raw_noise / amplitude_sum))
            local tx = value < 0.4 and 1 or 0
            local saturation = tx == 0 and 0.4 or value
            local hue = (tx == 0 and 100 or 250) + value * 720
            local tint = ax.vec4_hsla_to_rgba(vec4:new(hue, saturation, 0.5, 1.0))
            local index = y * 32 + x + 1
            values:set(index, value)
            sprites:set(index, x * 32 + request.cx * 1024, y * 32 + request.cy * 1024,
                request.cy * 1024 - 2000, tx * 16, 0, 16, 16, 2, 2, tint)
        end
    end
    return { sprites = sprites, noise = values }
end
