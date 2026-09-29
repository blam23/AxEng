local comp = ax.import("@compiler_core")

local dir = comp.get_project_directory_from_args()
local project = comp.open_project(dir)

local success = true
for k, v in pairs(project.scripts) do
    local result, err = comp.check_script(dir .. "/" .. v)
    if (not result) then
        success = false
        log.error("Error compiling script '" .. k .. "': " .. err)
        app.set_main_thread("check_all_scripts_result__" .. k, false)
        app.set_main_thread("check_all_scripts_result_err__" .. k, err)
    else
        app.set_main_thread("check_all_scripts_result__" .. k, true)
    end
end


return success