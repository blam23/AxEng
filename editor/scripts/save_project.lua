local comp = ax.import("@compiler_core")
local json = ax.import("@json")

local save = {}

save.build_data = function(build_data)
    if build_data == nil then
        log.error("No build data given")
        return error_code.InvalidConfiguration
    end

    local success, ret = pcall(function() return json.encode(build_data) end)
    if not success then
        log.error("Failed to encode build.json: ", tostring(ret))
        return error_code.JSONFailure
    end

    if ret == nil then
        log.error("Json encode returned nil")
        return error_code.JSONFailure
    end
    

    local ofile = io.open(comp.get_project_directory_from_args() .. "/" .. "build.json", "w")
    if ofile == nil then
        return error_code.IO
    end
    ofile:write(ret)
    ofile:close()

    print("Saved build.json")
    
    return error_code.Success
end

save.project = function(project)
    if project == nil then
        log.error("No project given")
        return error_code.InvalidConfiguration
    end

    local success, ret = pcall(function() return json.encode(project) end)
    if not success then
        log.error("Failed to encode project.json: ", tostring(ret))
        return error_code.JSONFailure
    end

    if ret == nil then
        log.error("Json encode returned nil")
        return error_code.JSONFailure
    end

    local ofile = io.open(comp.get_project_directory_from_args() .. "/" .. "project.json", "w")
    if ofile == nil then
        return error_code.IO
    end
    ofile:write(ret)
    ofile:close()

    print("Saved project.json")
    
    return error_code.Success
end

save.all = function(project, build_data)
    local err = save.project(project)
    if err ~= error_code.Success then
        return err
    end
    err = save.build_data(build_data)
    if err ~= error_code.Success then
        return err
    end
    return error_code.Success
end

return save