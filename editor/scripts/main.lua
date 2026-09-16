-- AxEditor
-- -cxrv --in "$(SolutionDir)editor" --out "E:\AxEdit" --project $(SolutionDir)demo" 

local unsaved = false
local show_close_confirm_modal = false

function set_unsaved()
    unsaved = true
    window.set_title(app.window.handle, "AxEng - " .. project.name .. " *")
end

function set_saved()
    unsaved = false
    window.set_title(app.window.handle, "AxEng - " .. project.name)
end

local comp = ax.import("@compiler_core")
project = comp.open_project(comp.get_project_directory_from_args())
local validated = comp.validate_project(project)
if not validated then
    log.error("Failed to validate project.")
    return error_code.InvalidConfiguration
end

local save_project = ax.import("save_project")
local project_browser = ax.import("project_browser")

local last_save = 0
local last_save_err = false
function save_window(delta)
    ui.begin_window("Save Window")
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
                ui.text_color(0, 0.8, 0, last_save / 3.0, "\xef\x81\x98 Saved") -- circle check
            else
                ui.text_color(0.7, 0, 0, last_save / 3.0, "\xef\x81\xaa Failed to save") -- circle exclamation
            end
        elseif unsaved then
            ui.same_line()
            ui.text_color(0.7, 0.7, 0, 1, "* Unsaved") -- circle check
        end
    ui.end_window()
end

function close_confirm_modal()
    if show_close_confirm_modal then
        ui.open_popup("Confirm Close")
        local opened = ui.begin_popup_modal("Confirm Close")

        ui.text("You have unsaved work, are you sure you want to exit?\n\n")
        if ui.button("Yes, close it!") then
            window.request_close(app.window.handle)
            show_close_confirm_modal = false
            ui.close_current_popup()
        end
        ui.same_line()
        if ui.button("I want to keep working") then
            show_close_confirm_modal = false
            ui.close_current_popup()
        end
        if opened then
            ui.end_popup()
        end
    end
end

function main_ui(delta)
    ui.dock_space_over_viewport()
    save_window(delta)
    project_browser()
    close_confirm_modal()
end

function tried_to_close()
    if unsaved then
        app.window.prevent_close()
        ui.insert_toast(ui.toast_type.Error, 3000, "Make sure to save before exiting!")

        show_close_confirm_modal = true
    end
end

set_saved()
app.window.on_ui.subscribe(main_ui)
app.window.on_close.subscribe(tried_to_close)

error_texture = app.res.get_texture("error")

return error_code.Success
