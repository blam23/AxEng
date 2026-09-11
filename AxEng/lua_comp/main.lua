-- AxEng Lua Comp

-- use spdlog lib to print
local __print = print
print = function(...)
    local arg = {...}
    local res = ""
    for i,v in ipairs(arg) do
        res = res .. tostring(v)
    end
    log.info(res)
end

function rstrip(str)
    return (str:gsub("%s+$", ""))
end

found_error = false

function key_exists(tbl, key)
    if tbl[key] == nil then
        print("Can't find project data: " .. key)
        found_error = true
    end
end

function validate_project_file()
    found_error = false

    key_exists(project, "name")
    key_exists(project, "init_script")
    key_exists(project, "entry_point")
    key_exists(project, "icon")
    key_exists(project, "scripts")
    key_exists(project, "textures")
    key_exists(project, "types")

    if found_error then
        print("Invalid project file.")
        return false
    end

    return true
end

function check_and_copy_script(script_in, script_out)
    local chunk = loadfile(script_in)

    -- make sure it loaded
    assert(chunk)

    local ofile = io.open(script_out, "wb")
    ofile:write(string.dump(chunk))
    ofile:close()
end

function check_and_copy_texture(texture_in, texture_out)
    -- todo: Make sure texture is valid
    local sfile = io.open(texture_in, "rb")
    local script = sfile:read("*all")
    sfile:close()

    local ofile = io.open(texture_out, "wb")
    ofile:write(script)
    ofile:close()
end

function template_replace(str, data)
    local ret = ""
    local pos = 0
    local idx = str:find("%%")

    while idx ~= nil do
        local idx_e = str:find("%%", idx + 2)
        local key = str:sub(idx+2, idx_e-1)

        if data[key] == nil then
            log.error("Template key '"..key.."' not found in data.")
        else
            ret = ret .. str:sub(pos, idx - 1) .. tostring(data[key])
            pos = idx_e + 2
        end

        idx = str:find("%%", idx_e + 2)
    end

     ret = ret .. str:sub(pos, -1)

    return ret
end

function gen_script_text()
    local ret = ""

    for k,v in pairs(built_scripts) do
        ret = ret .. "    " .. k .. " = \"" .. v .. "\",\n"
    end

    return rstrip(ret)
end

function gen_textures_text()
    local ret = ""

    for k,v in pairs(project.textures) do
        ret = ret .. "    " .. k .. " = \"" .. v .. "\",\n"
    end

    return rstrip(ret)
end

function gen_types_text()
    local ret = ""

    for k,v in pairs(project.types) do
        ret = ret .. "    \"" .. k.. "\",\n"
    end

    return rstrip(ret)
end

function create_manifest(dir, strip_debug_output)
    local tfile = io.open("lua_comp/output_template.luat", "r")
    local template = tfile:read("*all")
    tfile:close()

    local data = {
        NAME = project.name,
        ENTRY_POINT = project.entry_point,
        ICON = project.icon,
        WINDOW_WIDTH = 1920,
        WINDOW_HEIGHT = 1080,
        WINDOW_VSYNC = true,
        SCRIPTS = gen_script_text(),
        TEXTURES = gen_textures_text(),
        TYPES = gen_types_text(),
    }
    local output = template_replace(template, data)

    local chunk = load(output, "!manifest", "t", strip_debug_output)

    -- make sure it loads
    assert(chunk)

    local ofile = io.open(dir .. "/manifest.luac", "wb")
    ofile:write(string.dump(chunk))
    ofile:close()
end