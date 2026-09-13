local selected_script = 0
local selected_texture = 0

local scripts = {}
local textures = {}

for k,v in pairs(project.scripts) do
    table.insert(scripts, { k, v })
end

for k,v in pairs(project.textures) do
    table.insert(textures, { k, v })
end

function list_box(name, tbl, selected)
    ui.set_next_item_width(-1)
    local need_end = ui.begin_listbox("##")
    local i = 0
    for k,v in pairs(project[name]) do
        i = i + 1
        local highlighted, changed = ui.selectable(k .. " (" .. v.. ")", selected == i)
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
                    break
                end
            end
        end
    end

    return selected
end

function project_browser()

    ui.w_begin("Script Browser")
        selected_script = list_box("scripts", scripts, selected_script)
    ui.w_end()

    ui.w_begin("Texture Browser")
        selected_texture = list_box("textures", textures, selected_texture)
    ui.w_end()

end


log.debug("Loaded project browser.")
