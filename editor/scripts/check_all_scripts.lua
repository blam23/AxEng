local comp = ax.import("@compiler_core")

local results = app.get_or_create_shared("check_all_scripts")

local dir = comp.get_project_directory_from_args()
local project = comp.open_project(dir)

local success = true
for k, v in pairs(project.scripts) do
    local result, err = comp.check_script(dir .. "/" .. v)
    if (not result) then
        success = false
        results:set(k, false)
        results:set(k .. "_err", err)
    else
        results:set(k, true)
    end
end

app.release_shared(results)

return success