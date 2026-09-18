local json = ax.import("@json")

local comp = {
    revision = 1
}

local found_error = false
local function key_exists(tbl, key)
    if tbl[key] == nil then
        log.error("Can't find project data: " .. key)
        found_error = true
    end
end

comp.open_project = function(directory)
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

    ret.directory = directory

    return ret
end

comp.validate_project = function(project)
    found_error = false

    key_exists(project, "name")
    key_exists(project, "entry_point")
    key_exists(project, "scripts")

    if found_error then
        log.error("Invalid project file.")
        return false
    end

    return true
end

comp.check_script = function(script_file)
    local chunk, err_msg = loadfile(script_file)

    -- make sure it loaded
    if chunk == nil then
        return false, err_msg
    end

    return true, chunk
end

comp.get_project_directory_from_args = function()
    local in_pair_state = false
    local ret = ""

    for i in pairs(args) do
        if in_pair_state then
            ret = ax.rstrip(args[i])
            in_pair_state = false
        else
            if args[i] == "--project" then
                in_pair_state = true
            end
        end
    end

    return ret
end

comp.get_build_data = function(project)
    local file = io.open(project.directory .. "/" ..  "build.json", "r")
    if file == nil then
        return error_code.IO
    end
    local json_str = file:read("*all")
    file:close()

    local success, ret = pcall(function() return json.decode(json_str) end)

    if not success then
        log.error("Failed to parse build.json: ", ret)
        return nil
    end
    
    return ret
end

return comp
