return function(request, task)
    local tile_count = math.floor(request.tile_count)
    local chunk_size = request.chunk_size
    local tile_total = tile_count * tile_count
    local values = bg.number_buffer(tile_total)
    local colors = bg.number_buffer(tile_total * 4)
    local enemies = bg.number_buffer(request.enemy_count * 8)
    local amplitude_sum = (1.0 - 0.5 ^ 6) / (1.0 - 0.5)
    for y = 0, tile_count - 1 do
        if task:cancelled() then return {} end
        for x = 0, tile_count - 1 do
            local nx = (x + request.cx * tile_count) / 50.0
            local ny = (y + request.cy * tile_count) / 50.0
            local raw_noise = noise.fbm(nx, ny, 2.0, 0.5, 6)
            local value = math.max(0.0, math.min(1.0, 0.5 + 0.5 * raw_noise / amplitude_sum))
            local tx = value < 0.4 and 1 or 0
            local saturation = tx == 0 and 0.4 or value
            local hue = (tx == 0 and 100 - value * 720 or math.random(214,218))
            local tint = ax.vec4_hsla_to_rgba(vec4:new(hue, saturation, 0.5, 1.0))
            local index = y * tile_count + x + 1
            values:set(index, value)
            local color_index = (index - 1) * 4 + 1
            colors:set(color_index, tint.r)
            colors:set(color_index + 1, tint.g)
            colors:set(color_index + 2, tint.b)
            colors:set(color_index + 3, tint.a)
        end
    end
    for i = 1, request.enemy_count do
        if task:cancelled() then return {} end
        local enemy_x = request.cx * chunk_size + math.random(0, chunk_size)
        local enemy_y = request.cy * chunk_size + math.random(0, chunk_size)
        local face = math.random(1, 6)
        local offset = math.random(0, 1000)
        local clothes = ax.vec4_hsla_to_rgba(vec4:new(
            math.random(0, 359),
            math.random(3, 6) * 0.1,
            math.random(1, 3) * 0.1,
            1.0
        ))
        local index = (i - 1) * 8 + 1
        enemies:set(index, enemy_x)
        enemies:set(index + 1, enemy_y)
        enemies:set(index + 2, face)
        enemies:set(index + 3, offset)
        enemies:set(index + 4, clothes.r)
        enemies:set(index + 5, clothes.g)
        enemies:set(index + 6, clothes.b)
        enemies:set(index + 7, clothes.a)
    end
    return { noise = values, colors = colors, enemies = enemies }
end
