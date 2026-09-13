-- use spdlog lib to print
local __print = print
print = function(...)
    local arg={...}
    local res = ""
    for i,v in ipairs(arg) do
        res = res .. tostring(v)
    end
    log.info(res)
end

function get_table_size(tbl)
    local c = 0
    for _ in pairs(tbl) do
        c = c + 1
    end
    return c
end

function print_table(tbl, lvl)
    if lvl == nil then lvl = 0 end
    local indent_str = string.rep("   ", lvl)

    for k, v in pairs(tbl) do
        if type(v) == "table" then
            print(indent_str, k, " = {")
            print_table(v, lvl + 1)
            print(indent_str, "}")
        else
            print(indent_str, k, " = ", tostring(v))
        end
    end
end

function rstrip(str)
    return (str:gsub("%s+$", ""))
end

local __require = require
require = function()
    print("Do not use this!")
end

function assert_success(err)
    assert(err == error.Success)
end

function import(app, script_name)
    if app.imported == nil then
        app.imported = {}
    end

    if app.imported[script_name] then
        log.info("Already loaded script: '" .. script_name .. "'")
        return error.Success
    end

    local script = app.res.get_script(script_name)

    if (not script.valid) then
        log.error("Unable to load script: '" .. script_name .. "'.")
        return error.AssetNotFound
    end

    app.imported[script_name] = true

    if (app.run_in_this_environment(script)) then
        return error.Success
    else
        return error.Lua
    end
end

-- todo: improve
keys = {
    esc = 256,
    w = 87,
    a = 65,
    s = 83,
    d = 68,
}