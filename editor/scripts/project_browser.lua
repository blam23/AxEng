local comp = ax.import("@compiler_core")

local browser = {
    selected_script = 0,
    selected_texture = 0,
    scripts = {},
    textures = {},
    script_active = false,
    texture_active = false,
    on_selected = lua_event.new()
}

local old_script_selected = 0
local old_texture_selected = 0

browser.reload = function()
    old_script_selected = 0
    old_texture_selected = 0
    scripts = {}
    textures = {}
    for k,v in pairs(browser.project.scripts) do
        table.insert(scripts, { k, v })
    end

    for k,v in pairs(browser.project.textures) do
        table.insert(textures, { k, v })
    end
end

local function get_color(name)
    local color = { 0.8, 0.8, 0.8, 1.0 }
    if name == "scripts" then
        color = { 0.8, 0.8, 0, 1.0 }
    elseif name == "textures" then
        color = { 0, 0.8, 0.8, 1.0 }
    end

    return color
end

local function list_box(name, tbl, selected, icon)
    local cr, cg, cb, ca = table.unpack(get_color(name))
    ui.set_next_item_width(-1)
    local need_end = ui.begin_sized_listbox("##", -1, -10)
    ui.text_color(cr, cg, cb, ca, name:sub(1,1):upper() .. name:sub(2))
    local i = 0
    for k,v in pairs(browser.project[name]) do
        i = i + 1
        ui.text_color(cr, cg, cb, ca, icon)
        ui.same_line()
        local highlighted, changed = ui.selectable(k .. " (" .. v.. ")", selected == i)
        if changed and highlighted then
            browser.selected_script = 0
            browser.selected_texture = 0

            selected = i
            lua_event.fire(browser.on_selected, { name, tbl[selected] })
        end
    end
    if need_end then
        ui.end_listbox()
    end

    return selected, need_end
end

browser.display = function()
    ui.begin_window("\xef\xa0\x82 Asset Browser") -- Folder Tree Icon
        browser.selected_script, browser.script_active = list_box("scripts", scripts, browser.selected_script, "\xef\x87\x89")
        browser.selected_texture, browser.texture_active = list_box("textures", textures, browser.selected_texture, "\xef\x87\x85")
    ui.end_window()
end

browser.init = function(project)
    browser.project = project
    browser.reload()
end


return browser