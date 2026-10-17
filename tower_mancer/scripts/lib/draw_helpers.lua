local font_texture = app.res.get_texture("__builtin_font")

local helpers = {}

function helpers.__string(x, y, text, rotation, color, scale, bold, z_index, use_render_ui)
    if z_index == nil then
        z_index = 0
    end
    local y_offset = 0
    local x_offset = 0
    for i = 1, #text do
        local c = string.byte(text, i)
        if c == 10 then
            y_offset = y_offset + 1
            x_offset = 0
        elseif c >= 32 and c <= 126 then
            local index = c - 32
            local row = math.floor(index / 16) + (bold and 8 or 0)
            local col = index % 16
            if use_render_ui then
                app.window.render_ui(
                    font_texture, x + x_offset, y + (18 * y_offset * scale.y),
                    16 * col + 1, 16 * row + 1, 15, 15,
                    math.sin(time * 3.0 + i) * 0.1, z_index,
                    color.r, color.g, color.b, color.a,
                    scale.x, scale.y
                )
            else
                app.window.render(
                    font_texture, x + x_offset, y + (18 * y_offset * scale.y),
                    16 * col + 1, 16 * row + 1, 15, 15,
                    math.sin(time * 3.0 + i) * 0.1, z_index,
                    color.r, color.g, color.b, color.a,
                    scale.x, scale.y
                )
            end
            x_offset = x_offset + (10 * scale.x)
        else
            x_offset = x_offset + (10 * scale.x)
        end
    end
end

function helpers.__shadowed_string(x, y, text, rotation, color, scale, bold, z_index, use_render_ui)
    if z_index == nil then
        z_index = 0
    end
    helpers.__string(x + (scale.x), y + (scale.y), text, rotation, {r = 0, g = 0, b = 0, a = 0.7}, scale, bold, z_index - 1, use_render_ui)
    helpers.__string(x, y, text, rotation, color, scale, bold, z_index, use_render_ui)
end

function helpers.string(x, y, text, rotation, color, scale, bold, z_index)
    helpers.__string(x, y, text, rotation, color, scale, bold, z_index, false)
end

function helpers.shadowed_string(x, y, text, rotation, color, scale, bold, z_index)
    helpers.__shadowed_string(x, y, text, rotation, color, scale, bold, z_index, false)
end

function helpers.ui_string(x, y, text, rotation, color, scale, bold, z_index)
    helpers.__string(x, y, text, rotation, color, scale, bold, z_index, true)
end

function helpers.ui_shadowed_string(x, y, text, rotation, color, scale, bold, z_index)
    helpers.__shadowed_string(x, y, text, rotation, color, scale, bold, z_index, true)
end

function helpers.string_width(text, scale)
    local width = 0
    local max_width = 0
    scale = scale or 1
    for i = 1, #text do
        local c = string.byte(text, i)
        if c == 10 then
            max_width = math.max(max_width, width)
            width = 0
        else
            width = width + (10 * scale)
        end
    end
    return math.max(max_width, width)
end



return helpers
