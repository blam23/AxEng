function get_project_directory()
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

return open_project(get_project_directory())