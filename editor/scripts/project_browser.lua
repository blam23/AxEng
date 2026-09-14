local selected_script = 0
local selected_texture = 0

local scripts = {}
local textures = {}

function reload()
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
    if not need_end then
        return 0, false
    end
    local i = 0
    for k,v in pairs(project[name]) do
        i = i + 1
        local highlighted, changed = ui.selectable(icon .. " " .. k .. " (" .. v.. ")", selected == i)
        if changed and highlighted then
            selected = i
        end
    end
    ui.end_listbox()
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

    return selected, true
end

local old_script_selected = 0
local script_cache = ""
function preview_script(selected)
    if script_cache == "" or old_script_selected ~= selected then
        local script_file = ""
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
            return
        end

        local file = io.open(get_project_directory() .. "/" .. script_file, "r")
        script_cache = file:read("*all")
        file:close()

        old_script_selected = selected
    end

    -- todo: replace with code editor
    ui.text(script_cache)
end

local old_texture_selected = 0
local texture_cache = nil
function preview_texture(selected)
    if texture_cache == nil or old_texture_selected ~= selected then
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
            return
        end

        local file = io.open(get_project_directory() .. "/" .. texture_file, "rb")
        local texture_data = file:read("*all")
        file:close()

        texture_cache = app.res.create_texture("proj__" .. texture_name, texture_data)
        old_texture_selected = selected
    end

    if texture_cache and texture_cache.valid then
        ui.image(texture_cache)
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