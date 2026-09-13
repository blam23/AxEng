project_directory = ""

local get_project_directory = function()
    local in_pair_state = false
    local ret = ""

    for i in pairs(args) do
        if in_pair_state then
            ret = rstrip(args[i])
            in_pair_state = false
        else
            if args[i] == "--project" then
                in_pair_state = true
            end
        end
    end

    print("Project: '" .. ret .. "'")
    return ret
end

local open_project = function(directory)
    local res, err_msg = loadfile(directory .. "/" .. "project.lua")
    if res == nil then
        print("Error loading project file: " .. err_msg)
        return error.Lua
    end

    res()
    return error.Success
end

function check_project()
    project_directory = get_project_directory()
    assert_success(open_project(project_directory))
end

log.debug("Project loader loaded.")
