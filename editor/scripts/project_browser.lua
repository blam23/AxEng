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

    return selected
end

function project_browser()

    ui.w_begin("Script Browser")
        selected_script = list_box("scripts", scripts, selected_script, "\xef\x87\x89")
    ui.w_end()

    ui.w_begin("Texture Browser")
        selected_texture = list_box("textures", textures, selected_texture, "\xef\x87\x85")
    ui.w_end()

end

reload()

return true