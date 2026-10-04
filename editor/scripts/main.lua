-- AxEditor

--[[

Example how to run:

-cxrqv --in "$(SolutionDir)editor" --out "E:\AxEdit" --allow-threads --allow-io --allow-os --project "$(SolutionDir)tower_mancer" --compile_to "E:\TowerMancer"

]]

local comp = ax.import("@compiler_core")
local save = ax.import("save_project")
local project_browser = ax.import("project_browser")
local asset_inspector = ax.import("asset_inspector")
local build_info = ax.import("build_info")
local editor = ax.import("script_editor")
local try_run = ax.import("try_run")

local unsaved_config = false
local unsaved_scripts = false
local show_close_confirm_modal = false
local should_embed_app = false

function is_unsaved()
    return unsaved_config or unsaved_scripts
end

local function update_title()
    if is_unsaved() then
        window.set_title(app.window.handle, "AxEdit - " .. project.name .. " *")
    else
        window.set_title(app.window.handle, "AxEdit - " .. project.name)
        window.set_title(app.window.handle, "AxEdit - " .. project.name)
    end
end

function set_unsaved_config()
    unsaved_config = true
    update_title()
end

function set_saved_config()
    unsaved_config = false
    update_title()
end

function set_unsaved_scripts()
    unsaved_scripts = true
    update_title()
end

function set_saved_scripts()
    unsaved_scripts = false
    update_title()
end

function asset_edited(data)
    local tbl = data.tbl
    local old_key = data.old_key
    local old_value = data.old_value
    local new_key = data.new_key
    local new_value = data.new_value

    if old_key == new_key and old_value == new_value then
        return
    end

    if old_key ~= nil then
        project[tbl][old_key] = nil -- remove old entry
    end

    project[tbl][new_key] = new_value
    set_unsaved_config()
    project_browser.reload()
end

asset_inspector.on_asset_edited:subscribe(asset_edited)

function background_app_done(data)
    local name = data["name"]
    local success = data["success"]
    local err = data["err"]
    local stdout = data["stdout"]
    print(stdout)

    if name == "compiler" then
        if success then
            ui.insert_toast(ui.toast_type.Success, 3000, "Compilation completed.")
        else
            log.error("Compilation failed: " .. tostring(err))
            if stdout then
                log.error("Compilation output: " .. stdout)
            end
            ui.insert_toast(ui.toast_type.Error, 3000, "Compilation failed!")
        end
    end
end

try_run.done:subscribe(background_app_done)

local launched_app = {
    pid = 0
}
function background_app_launched(data)
    launched_app.pid = data.pid
end

try_run.launched:subscribe(background_app_launched)

local last_save = 0
local last_save_err = false
local embedded = false
function main_menu(delta)
    ui.begin_main_menu_bar("MainMenu")
        ui.text(project.name)
        if ui.button("\xef\x83\x87 Save Config") then -- Floppy Disk Icon
            last_save = 3.0
            local err = save.all(project, build_info.build_data)
            last_save_err = err
            if (err ~= error_code.Success) then
                log.error("Failed to save project: " .. tostring(msg))
                ui.insert_toast(ui.toast_type.Error, 3000, "Failed to save project!")
            else
                set_saved_config()
                ui.insert_toast(ui.toast_type.Success, 3000, "Saved project")
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
        elseif is_unsaved() then
            ui.same_line()
            ui.text_color(0.7, 0.7, 0, 1, "* Unsaved") -- circle check
        end
        local running, running_app_name = try_run.is_running()
        if running and running_app_name == "app" then
            -- #A256FF
            ui.draw_rect(1, 1, app.window.width-2, app.window.height-2, 0, 4.0, 0.63, 0.34, 1.0, 1.0)
            if should_embed_app then
                if not embedded and launched_app.pid > 0 then
                    if app.window.try_embed_child(launched_app.pid) then
                        embedded = true
                    end
                end
            end
        else
            embedded = false
        end
        if ui.button("\xef\x80\x93 Compile") then
            if not running then
                try_run.background("compiler", {
                    "-cxv",
                    "--allow-io", "--allow-os",
                    "--in", comp.get_project_directory_from_args(),
                    "--out", comp.get_compile_directory_from_args(),

                })
            end
        end
        local button_text = (running and running_app_name == "app") and "\xef\x81\x8d Stop" or "\xef\x81\x8b Run"
        if ui.button(button_text) then
            if not running then
                try_run.background("app",{
                    "-rv",
                    "--allow-io", "--allow-os", "--allow-threads",
                    "--in", comp.get_compile_directory_from_args(),
                    "--project", comp.get_project_directory_from_args(),
                    "--compile_to", comp.get_compile_directory_from_args()
                })
            else
                app.window.try_kill_child(launched_app.pid)
            end
        end
        local sea, changed = ui.checkbox("Embed App", should_embed_app)
        if changed then
            should_embed_app = sea
        end
    ui.end_main_menu_bar()
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

local app_focused = false
function update_embedded_window()
    if (ui.begin_window("\xef\x82\x91 Application")) then
        local x, y = ui.get_content_position()
        local w, h = ui.get_region_available()
        app.window.set_embedded_child_position(launched_app.pid, x, y, w, h)
        if ui.is_window_focused() and not app_focused then
            app_focused = true
            app.window.redirect_input_to_child(launched_app.pid)
        elseif not ui.is_window_focused() and app_focused then
            app_focused = false
            app.window.reset_input_redirection()
        end
    else
        if app_focused then
            app_focused = false
            app.window.reset_input_redirection()
        end
        app.window.set_embedded_child_position(launched_app.pid, 0, 0, 0, 0)
    end
    ui.end_window()
end

function main_ui(delta)
    ui.dock_space_over_viewport()
    main_menu(delta)
    project_browser.display()
    asset_inspector.display()
    build_info.display()
    editor.display()
    if (embedded) then
        update_embedded_window()
    end
    close_confirm_modal()
end

function tried_to_close()
    local running, _ = try_run.is_running()
    if running then
        app.window.prevent_close()
        ui.insert_toast(ui.toast_type.Error, 3000, "A background task is still running!")
    elseif is_unsaved() then
        app.window.prevent_close()
        ui.insert_toast(ui.toast_type.Error, 3000, "Make sure to save before exiting!")

        show_close_confirm_modal = true
    end
end

project = comp.open_project(comp.get_project_directory_from_args())
local validated = comp.validate_project(project)
if not validated then
    log.error("Failed to validate project.")
    return error_code.InvalidConfiguration
end

update_title()

project_browser.init(project)
asset_inspector.init(project)
build_info.init(project)
editor.init(project)

app.window.on_ui.subscribe(main_ui)
app.window.on_close.subscribe(tried_to_close)

error_texture = app.res.get_texture("error")

return error_code.Success
