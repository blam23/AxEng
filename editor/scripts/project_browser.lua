local selected_script = 0
local selected_texture = 0

local scripts = {}
local textures = {}

function reload()
    old_script_selected = 0
    old_texture_selected = 0
    scripts = {}
    textures = {}
    for k,v in pairs(project.scripts) do
        table.insert(scripts, { k, v })
    end

    for k,v in pairs(project.textures) do
        table.insert(textures, { k, v })
    end
end

function list_box(name, tbl, selected, icon)
    ui.set_next_item_width(-1)
    local need_end = ui.begin_listbox("##")
    local i = 0
    for k,v in pairs(project[name]) do
        i = i + 1
        local highlighted, changed = ui.selectable(icon .. " " .. k .. " (" .. v.. ")", selected == i)
        if changed and highlighted then
            selected = i
        end
    end
    if need_end then
        ui.end_listbox()
    end
    if selected > 0 then
        local k = tbl[selected][1]
        local v = tbl[selected][2]
        local changed, changed_2 = false
        ui.text("Edit Selected:")
        k, changed = ui.input_text("Name", k)
        v, changed_2 = ui.input_text("Path", v)
        if changed or changed_2 then
            tbl[selected] = { k, v }
        end
        if ui.button("Save") then
            local i = 0
            for ok,ov in pairs(project[name]) do
                i = i + 1
                if i == selected then
                    project[name][ok] = nil
                    project[name][k] = v
                    print("Updated: '", ok, "' -> '", k, "'.")
                    print("Updated: '", ov, "' -> '", v, "'.")
                    set_unsaved()
                    reload()
                    break
                end
            end
        end
    end

    return selected, need_end
end

local old_script_selected = 0
local script_cache = ""
local script_failed = false
local script_valid = false
local script_error_msg = ""
function preview_script(selected)
    if (script_cache == nil and not script_failed) or old_script_selected ~= selected then
        script_failed = false
        old_script_selected = selected
        local i = 0
        for k,v in pairs(project.scripts) do
            i = i + 1
            if i == selected then
                script_file = v
                break
            end
        end
        if script_file == "" then
            log.error("Unable to get script file.")
            script_failed = true
            return
        end

        local file = io.open(get_project_directory() .. "/" .. script_file, "r")
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
            ui.text_colored(0, 0.8, 0, 1.0, "\xef\x81\x98")
        else
            ui.text_colored(0.7, 0, 0, 1.0, "\xef\x81\xaa")
            ui.text_colored(0.8, 0.3, 0.2, 1.0, script_error_msg)
        end
        ui.text(script_cache)
    else
        ui.text_colored(0.7, 0, 0, 1.0, "\xef\x81\xaa Invalid script!")
    end
end

local old_texture_selected = 0
local texture_cache = nil
local texture_failed = false
function preview_texture(selected)
    if (texture_cache == nil and not texture_failed) or old_texture_selected ~= selected then
        texture_failed = false
        old_texture_selected = selected
        local texture_name = ""
        local texture_file = ""
        local i = 0
        for k,v in pairs(project.textures) do
            i = i + 1
            if i == selected then
                texture_name = k
                texture_file = v
                break
            end
        end

        if texture_file == "" then
            log.error("Unable to get texture file.")
            texture_failed = true
            return
        end

        local file = io.open(get_project_directory() .. "/" .. texture_file, "rb")
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
        ui.text_colored(0.7, 0, 0, 1.0, "\xef\x81\xaa Invalid texture!")
        ui.image(error_texture)
    end
end

function project_browser()
    ui.w_begin("Script Browser")
        selected_script, script_active = list_box("scripts", scripts, selected_script, "\xef\x87\x89")
        if script_active and selected_script > 0 then
            preview_script(selected_script)
        end
    ui.w_end()

    ui.w_begin("Texture Browser")
        selected_texture, texture_active = list_box("textures", textures, selected_texture, "\xef\x87\x85")
        if texture_active and selected_texture > 0 then
            preview_texture(selected_texture)
        end
    ui.w_end()
end

reload()

return true