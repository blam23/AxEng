local comp = ax.import("@compiler_core")
local project_browser = ax.import("project_browser")

local inspector = {
    current_asset = nil,
    on_asset_edited = lua_event.new()
}

local name_input = ""
local path_input = ""

local function asset_changed(asset)
    inspector.current_asset = asset
    name_input = inspector.current_asset[2][1]
    path_input = inspector.current_asset[2][2]
end

lua_event.subscribe(project_browser.on_selected, asset_changed)

local old_script_name = ""
local script_cache = ""
local script_failed = false
local script_valid = false
local script_error_msg = ""
local function preview_script(script_name)
    if (script_cache == nil and not script_failed) or old_script_name ~= selected then
        script_failed = false
        old_script_name = script_name
        local script_file = project.scripts[script_name]

        if script_file == nil then
            log.error("Failed to get script file from key: '" .. script_name .. "'")
            return
        end

        local file = io.open(comp.get_project_directory_from_args() .. "/" .. script_file, "r")
        if file == nil then
            log.error("Failed to open script: '" .. script_file .. "'.")
            script_failed = true
            return
        end
        script_cache = file:read("*all")
        file:close()

        local res, err = load(script_cache, "*")
        if res then
            script_valid = true
        else
            log.error(err)
            script_valid = false
            script_error_msg = err
        end
    end

    -- todo: replace with code editor
    if not script_failed then
        ui.same_line()
        if script_valid then
            ui.text_color(0, 0.8, 0, 1.0, "\xef\x81\x98")
        else
            ui.text_color(0.7, 0, 0, 1.0, "\xef\x81\xaa")
            ui.text_color(0.8, 0.3, 0.2, 1.0, script_error_msg)
        end
        ui.text(script_cache)
    else
        ui.text_color(0.7, 0, 0, 1.0, "\xef\x81\xaa Invalid script!")
    end
end

local old_texture_name = 0
local texture_cache = nil
local texture_failed = false
local function preview_texture(texture_name)
    if (texture_cache == nil and not texture_failed) or old_texture_name ~= texture_name then
        texture_failed = false
        old_texture_name = texture_name
        local texture_file = project.textures[texture_name]

        local file = io.open(comp.get_project_directory_from_args() .. "/" .. texture_file, "rb")
        if file == nil then
            log.error("Failed to open texture: '" .. texture_file .. "'.")
            texture_failed = true
            return
        end
        local texture_data = file:read("*all")
        file:close()

        texture_cache = app.res.create_texture("proj__" .. texture_file .. "__" .. texture_name, texture_data)
    end

    if not texture_failed and texture_cache and texture_cache.valid then
        ui.image(texture_cache)
    else
        ui.text_color(0.7, 0, 0, 1.0, "\xef\x81\xaa Invalid texture!")
        ui.image(error_texture)
    end
end

inspector.display = function()
    ui.begin_window("Inspector")
        if inspector.current_asset then
            local k = inspector.current_asset[2][1]
            local v = inspector.current_asset[2][2]
            
            local changed, changed_2 = false
            name_input, changed = ui.input_text("Name", name_input)
            path_input, changed_2 = ui.input_text("Path", path_input)
            if ui.button("Save") then
                lua_event.fire(inspector.on_asset_edited,{
                    tbl = inspector.current_asset[1],
                    old_key = k,
                    old_value = v,
                    new_key = name_input,
                    new_value = path_input
                })
                inspector.current_asset[2][1] = name_input
                inspector.current_asset[2][2] = path_input
                k = inspector.current_asset[2][1]
                v = inspector.current_asset[2][2]
            end

            if inspector.current_asset[1] == "scripts" then
                preview_script(k)
            elseif inspector.current_asset[1] == "textures" then
                preview_texture(k)
            end
        end
    ui.end_window()
end

inspector.init = function()
end

return inspector
