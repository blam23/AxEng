-- AxEng Std Lua Library
-- Automatically runs before any other script to help setup the environment

ax = {}

ax.get_table_size = function(tbl)
    local c = 0
    for _ in pairs(tbl) do
        c = c + 1
    end
    return c
end

ax.print_table = function(tbl, lvl)
    if lvl == nil then lvl = 0 end
    local indent_str = string.rep("   ", lvl)

    for k, v in pairs(tbl) do
        if type(v) == "table" then
            print(indent_str, k, " = {")
            ax.print_table(v, lvl + 1)
            print(indent_str, "}")
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
            print("inner key value: ", data[inner_key])
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

-- todo: generate this
ax.key_map = {
    esc = 256,
    w = 87,
    a = 65,
    s = 83,
    d = 68,
}

return ax
