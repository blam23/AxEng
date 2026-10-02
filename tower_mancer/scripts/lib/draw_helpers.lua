local font_texture = app.res.get_texture("font")

local helpers = {}

function helpers.__string(x, y, text, rotation, color, scale, bold, z_index, use_render_ui)
    if z_index == nil then
        z_index = 0
    end
    local y_offset = 0
    local x_offset = 0
    for i = 1, #text do
        local c = string.byte(text, i)
        local stride = 16
        local height = 16
        local lowercase = false
        if c > 64 and c < 91 then
            c = c - 65
        elseif c > 96 and c < 123 then
            c = c - 97
            lowercase = true
        elseif c > 47 and c < 58 then
            c = c - 22 -- (48-26)
        elseif c == 10 then
            c = 999
            y_offset = y_offset + 1
            x_offset = -((stride + 2.0) * scale.x)
        else
            c = 999
        end
        local row = math.floor(c / 16) 
        local col = c % 16
        if lowercase then
            row = 3
            col = c % 32
            stride = 8
            height = 32
        end
        if bold then
            row = row + 5
        end
        if c ~= 999 then
            if use_render_ui then
                app.window.render_ui(
                    font_texture, x + x_offset, y + (18 * y_offset * scale.y),
                    stride * col + 1, 16 * row + 1, stride - 1, height - 1,
                    math.sin(time * 3.0 + i) * 0.1, z_index,
                    color.r, color.g, color.b, color.a,
                    scale.x, scale.y
                )
            else
                app.window.render(
                    font_texture, x + x_offset, y + (18 * y_offset * scale.y),
                    stride * col + 1, 16 * row + 1, stride - 1, height - 1,
                    math.sin(time * 3.0 + i) * 0.1, z_index,
                    color.r, color.g, color.b, color.a,
                    scale.x, scale.y
                )
            end
        end
        x_offset = x_offset + ((stride + 2.0) * scale.x)
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
    local stride = 16
    for i = 1, #text do
        local c = string.byte(text, i)
        local stride = 16
        local height = 16
        local lowercase = false
        if c > 64 and c < 91 then
            c = c - 65
        elseif c > 96 and c < 123 then
            c = c - 97
            lowercase = true
        elseif c > 47 and c < 58 then
            c = c - 22 -- (48-26)
        elseif c == 10 then
            c = 999
            y_offset = y_offset + 1
            x_offset = -((stride + 2.0) * scale.x)
        else
            c = 999
        end
        local row = math.floor(c / 16) 
        local col = c % 16
        if lowercase then
            row = 3
            col = c % 32
            stride = 8
            height = 32
        end
        if c ~= 999 then
            width = width + ((stride + 2.0) * scale)
        end
    end
    return width
end



return helpers
