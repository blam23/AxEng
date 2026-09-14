local unsaved = false

function set_unsaved()
    unsaved = true
    window.set_title(app.window.handle, "AxEng - " .. project.name .. " *")
end

function set_saved()
    unsaved = false
    window.set_title(app.window.handle, "AxEng - " .. project.name)
end

project = import(app, "load_project")
local save_project = import(app, "save_project")
import(app, "project_browser")

local last_save = 0
local last_save_err = false

function save_window(delta)
    ui.w_begin("Save Window")
        if ui.button("Save") then
            last_save = 3.0
            local err = save_project()
            last_save_err = err
            if (err ~= error_code.Success) then
                log.error("Failed to save project: " .. tostring(msg))
                ui.insert_toast(ui.toast_type.Error, 3000, "Failed to save project.json!")
            else
                set_saved()
                ui.insert_toast(ui.toast_type.Success, 3000, "Saved project.json")
            end
        end
        if last_save > 0 then
            last_save = last_save - delta
            ui.same_line()
            if last_save_err == error_code.Success  then
                ui.text_colored(0, 0.8, 0, last_save / 3.0, "\xef\x81\x98 Saved") -- circle check
            else
                ui.text_colored(0.7, 0, 0, last_save / 3.0, "\xef\x81\xaa Failed to save") -- circle exclamation
            end
        elseif unsaved then
            ui.same_line()
            ui.text_colored(0.7, 0.7, 0, 1, "* Unsaved") -- circle check
        end
    ui.w_end()
end

function main_ui(delta)
    ui.dock_space_over_viewport()
    save_window(delta)
    project_browser()
end

set_saved()
app.on_ui.subscribe(main_ui)

error_texture = app.res.get_texture("error")

return error_code.Success
