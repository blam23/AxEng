local comp = ax.import("@compiler_core")
local project_browser = ax.import("project_browser")

local editor = {
    editors = {},
    current_editor = 0,
    is_active = false
}

local old_active = false
editor.display = function()
    ui.begin_window("\xef\x8c\x83  Lua Editor") -- Pencil Icon
        editor.is_active = ui.is_window_focused()
        ui.push_item_width(-1)
        if ui.begin_tab_bar("ScriptTabs") then
            local i = 0
            for script_name, script_file in pairs(editor.project.scripts) do
                i = i + 1
                local flags = 0
                if editor.editors[i].has_changes then
                    flags = flags | 1 -- ImGuiTabItemFlags_UnsavedDocument
                end
                if ui.begin_tab_item(script_name, flags) then
                    editor.current_editor = i
                    ui.push_font("mono", 24.0)
                    ui.render_editor(editor.editors[i].handle, 35)
                    ui.pop_font()
                    ui.end_tab_item()
                end
            end
            ui.end_tab_bar()
        end
        ui.begin_status_bar("StatusBar", 35.0)
            ui.push_font("mono", 16.0)
            -- TODO: Add script compile status
            local line, column = ui.get_editor_cursor(editor.editors[editor.current_editor].handle)
            ui.text("Ln: " .. line+1 .. ", Col: " .. column)
            ui.pop_font()
        ui.end_status_bar()
    ui.end_window()
end

local function load_text(file)
    local file = io.open(file, "r")
    local content = file:read("*a")
    file:close()
    return content
end

local function overwrite_text(file, content)
    local file = io.open(file, "w")
    file:write(content)
    file:close()
end

local function save_editor_file(index)
    log.debug("<Editor> Saving: " .. editor.editors[index].file)
    overwrite_text(
        editor.editors[index].file,
        ui.get_editor_text(editor.editors[index].handle)
    )
    log.info("<Editor> Saved: " .. editor.editors[index].file)
end

editor.init = function(project)
    editor.project = project
    for script_name, script_file in pairs(editor.project.scripts) do
        local edit = ui.create_editor()
        local editor_data = {
            handle = edit,
            has_changes = false,
            first_change = true,
            file = project.directory .. "/" .. script_file
        }
        ui.set_editor_text(edit, load_text(editor_data.file))
        ui.set_editor_change_callback(edit,
            function()
                if editor_data.first_change then
                    editor_data.first_change = false
                else
                    editor_data.has_changes = true
                    set_unsaved_scripts()
                end
            end
        )
        table.insert(editor.editors, editor_data)
    end
end

local function s_key_event(pressed, mods)
    -- Don't check want_capture_keyboard here as that will
    --  be true when the editor is active.

    if not editor.is_active then
        return
    end

    if pressed and mods == keyboard.modifier.ctrl then
        try_save_current()
    elseif pressed and mods == keyboard.modifier.shift | keyboard.modifier.ctrl then
        try_save_all()
    end
end

local function try_save(index)
    if editor.editors[index] ~= nil and editor.editors[index].has_changes then
        save_editor_file(index)
        editor.editors[index].has_changes = false
    end

    local any_unsaved = false
    for i, editor_data in pairs(editor.editors) do
        if editor_data.has_changes then
            any_unsaved = true
        end
    end

    if not any_unsaved then
        set_saved_scripts()
    end
end

function try_save_all()
    for i, editor_data in pairs(editor.editors) do
        try_save(i)
    end
end

function try_save_current()
    try_save(editor.current_editor)
end

keyboard.subscribe_key(ax.key_map.s, s_key_event)

return editor
