local comp = ax.import("@compiler_core")

local info = {
    selected_config = 0,
    config_name = "Choose a config"
}

info.display = function()
    ui.begin_window("\xef\x9f\x99 Build Info") -- Screwdriver Wrench Icon
        if (ui.begin_combo("Compiler##build", "msvc")) then
            ui.selectable("msvc", true)
            ui.end_combo() -- no choice as of yet
        end

        if (ui.begin_combo("Default Config##build", info.build_data.default_config)) then
            for config_name, _ in pairs(info.build_data.configs) do
                if ui.selectable(config_name, config_name == info.build_data.default_config) then
                    info.build_data.default_config = config_name
                    set_unsaved()
                end
            end
            ui.end_combo()
        end
    ui.end_window()
    ui.begin_window("\xef\x82\x85 Build Config Editor") -- Gears Icon
        if (ui.begin_combo("Config##build", info.config_name)) then
            local i = 0
            for config_name, config in pairs(info.build_data.configs) do
                i = i + 1
                if ui.selectable(config_name, info.selected_config == i) then
                    info.selected_config = i
                    info.config_name = config_name
                end
            end
            ui.end_combo()
        end
        if info.selected_config ~= 0 then
            -- Script stuff
            ui.separator_text("Script Config")
            local compile_out, compile_changed = 
                ui.checkbox("Compile scripts ", info.build_data.configs[info.config_name].compile_scripts)
            
            if compile_changed then
                info.build_data.configs[info.config_name].compile_scripts = compile_out
                set_unsaved()
            end

            if info.build_data.configs[info.config_name].strip_compiled_script_symbols == nil then
                info.build_data.configs[info.config_name].strip_compiled_script_symbols = false
            end

            local strip_out, strip_changed = 
                ui.checkbox("Strip Debug Info ", info.build_data.configs[info.config_name].strip_compiled_script_symbols)
            
            if strip_changed then
                info.build_data.configs[info.config_name].strip_compiled_script_symbols = strip_out
                set_unsaved()
            end

            -- Build stuff
            ui.separator_text("Build Config")
            local flags, flags_changed = ui.input_text("Flags", info.build_data.configs[info.config_name].flags)
            if flags_changed then
                info.build_data.configs[info.config_name].flags = flags
                set_unsaved()
            end
        end
    ui.end_window()
end

info.init = function(project)
    info.build_data = comp.get_build_data(project)
end

return info