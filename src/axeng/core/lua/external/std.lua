-- AxEng Std Lua Library
-- Automatically runs before any other script to help setup the environment

ax = {
    revision = 1
}

ax.get_table_size = function(tbl)
    local c = 0
    for _ in pairs(tbl) do
        c = c + 1
    end
    return c
end

ax.viewport_mouse_position = function()
    return app.window.global_to_viewport(mouse.get_position())
end

ax.print_table = function(tbl, lvl, printed)
    local printed = printed or {}
    if lvl == nil then lvl = 0 end
    local indent_str = string.rep("   ", lvl)

    for k, v in pairs(tbl) do
        if type(v) == "table" then
            if printed[v] then
                print(indent_str, k, " = { <already printed> }")
            else
                printed[v] = true
                print(indent_str, k, " = {")
                ax.print_table(v, lvl + 1, printed)
                print(indent_str, "}")
            end
        else
            print(indent_str, k, " = ", tostring(v))
        end
    end
end

ax.rstrip = function(str)
    return (str:gsub("%s+$", ""))
end

ax.assert_success = function(err, msg)
    if (err ~= error_code.Success) then
        log.error(msg)
    end
    assert(err == error_code.Success)
end
 
ax.import = function(script_name)
    if script_name == nil or #script_name == 0 then
        log.error("No name provided for import")
        return error_code.InvalidScript
    end
    
    if app == nil then
        log.error("App environment is not initialized.")
        return error_code.InvalidScript
    end

    if app.imported == nil then
        app.imported = {}
    end

    if app.imported[script_name] then
        return app.imported[script_name]
    end

    local script = app.res.get_script(script_name)

    if (not script.valid) then
        log.error("Unable to load script: '" .. script_name .. "'.")
        return error_code.AssetNotFound
    end

    local res = app.run_in_this_environment(script)
    if res == nil then
        log.error("Failed to run script '" .. script_name .. "'.")
        return error_code.Lua
    end

    app.imported[script_name] = res
    return res
end

ax.vec4_hsla_to_rgba = function(hsla)
    local r, g, b = ax.hsl_to_rgb(hsla.x, hsla.y, hsla.z)
    local a = hsla.w or 1
    return vec4:new(r, g, b, a)
end

local function hue2rgb(p, q, t)
    if t < 0   then t = t + 1 end
    if t > 1   then t = t - 1 end
    if t < 1/6 then return p + (q - p) * 6 * t end
    if t < 1/2 then return q end
    if t < 2/3 then return p + (q - p) * (2/3 - t) * 6 end
    return p
end

ax.hsl_to_rgb = function(h, s, l)
    local r, g, b
    local h = h / 360

    if s == 0 then
        r, g, b = l, l, l
    else
        local q = (l < 0.5) and l * (1 + s) or l + s - l * s
        local p = l * 2 - q

        r = hue2rgb(p, q, h + 1/3)
        g = hue2rgb(p, q, h)
        b = hue2rgb(p, q, h - 1/3)
    end

    return r, g, b
end

ax.template_replace = function(str, data)
    local ret = ""
    local pos = 0
    local idx = str:find("%%")
    local expect_end = false

    while idx ~= nil do
        local idx_e = str:find("%%", idx + 2, true)
        local key = str:sub(idx+2, idx_e-1)

        local used_key = false
        if #key > 6 and key:sub(1,1) == "?" then
            local inner_key = key:sub(5)
            local test_check = inner_key:sub(1,1) == "~"
            if test_check then
                inner_key = inner_key:sub(2)
            end
            local idx_end_start = str:find("%END%", idx_e + 2, true)
            if tostring(data[inner_key]) == tostring(test_check) then
                used_key = true
                ret = ret .. str:sub(pos, idx - 1)
                idx_e = idx_end_start + 5
                pos = idx_e
            else
                used_key = true
                ret = ret .. str:sub(pos, idx - 1)
                pos = idx_e + 2
                expect_end = true
            end
        end

        if not used_key then
            if data[key] == nil then
                log.error("Template key '"..key.."' not found in data.")
            else
                ret = ret .. str:sub(pos, idx - 1) .. tostring(data[key])
                pos = idx_e + 2
            end
        end

        idx = str:find("%%", idx_e + 2, true)

        if expect_end then
            idx_end = str:find("%END%", idx_e + 2, true)
            if idx > idx_end then
                ret = ret .. str:sub(pos, idx_end - 1) 
                pos = idx_end + 5
                expect_end = false
            end
        end
    end

    ret = ret .. str:sub(pos, -1)

    return ret
end

ax.key_map = {
    esc = 256,
    space = 32,
    enter = 257,
    right = 262,
    left = 263,
    down = 264,
    up = 265,
}

for i = 48, 57 do
    ax.key_map[string.char(i)] = i
end

for i = 65, 90 do
    ax.key_map[string.char(i):lower()] = i
end

return ax
