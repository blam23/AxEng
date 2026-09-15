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

local function rstrip(str)
    return (str:gsub("%s+$", ""))
end

local found_error = false
local function key_exists(tbl, key)
    if tbl[key] == nil then
        print("Can't find project data: " .. key)
        found_error = true
    end
end

function open_project(directory)
    local file = io.open(directory .. "/" .. "project.json", "r")
    if file == nil then
        return error_code.IO
    end
    local json_str = file:read("*all")
    file:close()

    local success, ret = pcall(function() return json.decode(json_str) end)

    if not success then
        log.error("Failed to parse project.json: ", ret)
        return nil
    end

    print("Loaded project.json")
    return ret
end

local function validate_project(project)
    found_error = false

    key_exists(project, "name")
    key_exists(project, "init_script")
    key_exists(project, "entry_point")
    key_exists(project, "icon")
    key_exists(project, "scripts")
    key_exists(project, "textures")
    --key_exists(project, "types")

    if found_error then
        print("Invalid project file.")
        return false
    end

    return true
end

local function check_and_copy_script(script_in, script_out)
    local chunk, err_msg = loadfile(script_in)

    -- make sure it loaded
    if chunk == nil then
        log.error(err_msg)
        return false
    end

    local ofile = io.open(script_out, "wb")
    ofile:write(string.dump(chunk))
    ofile:close()

    return true
end

local built_scripts = {}
local function check_and_copy_scripts(project, in_directory, out_directory)
    for script_name, script_path in pairs(project.scripts) do
        if script_path:sub(-4) ~= ".lua" then
            log.error("All scripts must have '.lua' extension, '" .. script_path .. "' does not.")
            return false
        end

        local in_path = in_directory .. "/" .. script_path
        local out_path = out_directory .. "/" .. script_path .. "c"

        if not check_and_copy_script(in_path, out_path) then
            return false
        end

        built_scripts[script_name] = script_path .. "c"
        log.debug("Compiled script: ", script_name)
    end

    log.info("Compiled scripts.")

    return true
end

local function check_and_copy_texture(texture_in, texture_out)
    -- todo: Make sure texture is valid
    local sfile = io.open(texture_in, "rb")
    local script = sfile:read("*all")
    sfile:close()

    local ofile = io.open(texture_out, "wb")
    ofile:write(script)
    ofile:close()

    return true
end

local function check_and_copy_textures(project, in_directory, out_directory)
    for texture_name, texture_path in pairs(project.textures) do
        if texture_path:sub(-4) ~= ".png" then
            log.error("All textures must have '.png' extension, '" .. texture_path .. "' does not.")
            return false
        end

        local in_path = in_directory .. "/" .. texture_path
        local out_path = out_directory .. "/" .. texture_path

        if not check_and_copy_texture(in_path, out_path) then
            return false
        end

        log.debug("Compiled texture: ", texture_name)
    end

    log.info("Compiled textures.")

    return true
end

local function template_replace(str, data)
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

local function gen_script_text()
    local ret = ""

    for k,v in pairs(built_scripts) do
        ret = ret .. "    " .. k .. " = \"" .. v .. "\",\n"
    end

    return rstrip(ret)
end

local function gen_textures_text(project)
    local ret = ""

    for k,v in pairs(project.textures) do
        ret = ret .. "    " .. k .. " = \"" .. v .. "\",\n"
    end

    return rstrip(ret)
end

local function gen_types_text(project)
    local ret = ""

    if project.types == nil then
        return ret
    end
    for k,v in pairs(project.types) do
        ret = ret .. "    \"" .. k.. "\",\n"
    end

    return rstrip(ret)
end

local function create_manifest(project, dir, strip_debug_output)
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
        TEXTURES = gen_textures_text(project),
        TYPES = gen_types_text(project),
    }
    local output = template_replace(template, data)

    print(output)

    local chunk, err_msg = load(output, "!manifest", "t", strip_debug_output)

    -- make sure it loads
    if chunk == nil then
        log.error(err_msg)
        return false
    end

    local ofile = io.open(dir .. "/manifest.luac", "wb")
    ofile:write(string.dump(chunk))
    ofile:close()

    log.info("Created manifest.")

    return true
end

function compile(in_directory, out_directory)
    local project = open_project(in_directory)

    local validated = validate_project(project)
    if not validated then
        log.error("Failed to validate project.")
        return error_code.InvalidConfiguration
    end

    local checked_scripts = check_and_copy_scripts(project, in_directory, out_directory)
    if not checked_scripts then
        log.error("Failed to validate scripts.")
        return error_code.InvalidScript
    end

    local checked_textures = check_and_copy_textures(project, in_directory, out_directory)
    if not checked_textures then
        log.error("Failed to validate textures.")
        return error_code.InvalidTexture
    end

    local created_manifest = create_manifest(project, out_directory, true)
    if not created_manifest then
        log.error("Failed to create manifest.")
        return error_code.InvalidConfiguration
    end

    -- for C++ to access if it wants
    compiled_project = project
    return error_code.Success
end