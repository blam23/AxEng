local comp = ax.import("@compiler_core")
local json = ax.import("@json")

local save_project = function()

    json.encode(project)

    local success, ret = pcall(function() return json.encode(project) end)
    if not success then
        log.error("Failed to encode project.json: ", tostring(ret))
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

return save_project