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
        else
            local glyph = font_texture.glyphs[c]
            if glyph then
                local bitmap = bold and glyph.bold or glyph.regular
                if bitmap.width > 0 and bitmap.height > 0 then
                    local glyph_x = x + x_offset + bitmap.x_offset * scale.x
                    local glyph_y = y + (y_offset * font_texture.line_height + font_texture.baseline + bitmap.y_offset) * scale.y
                    if use_render_ui then
                        app.window.render_ui(
                            font_texture, glyph_x, glyph_y,
                            bitmap.x, bitmap.y, bitmap.width, bitmap.height,
                            math.sin(time * 3.0 + i) * 0.1, z_index,
                            color.r, color.g, color.b, color.a,
                            scale.x, scale.y
                        )
                    else
                        app.window.render(
                            font_texture, glyph_x, glyph_y,
                            bitmap.x, bitmap.y, bitmap.width, bitmap.height,
                            math.sin(time * 3.0 + i) * 0.1, z_index,
                            color.r, color.g, color.b, color.a,
                            scale.x, scale.y
                        )
                    end
                end
                x_offset = x_offset + glyph.advance * scale.x
            else
                x_offset = x_offset + font_texture.space_advance * scale.x
            end
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
    scale = type(scale) == "table" and scale.x or scale or 1
    for i = 1, #text do
        local c = string.byte(text, i)
        if c == 10 then
            max_width = math.max(max_width, width)
            width = 0
        else
            local glyph = font_texture.glyphs[c]
            width = width + (glyph and glyph.advance or font_texture.space_advance) * scale
        end
    end
    return math.max(max_width, width)
end



return helpers
