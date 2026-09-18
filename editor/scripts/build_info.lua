local comp = ax.import("@compiler_core")

local info = {
    selected_config = 0,
    config_name = "Choose a config"
}

info.display = function()
    ui.begin_window("Build Info")
        if (ui.begin_combo("##configs", info.config_name)) then
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
        if info.selected_config == 0 then
        else
            local compile_out, compile_changed = 
                ui.checkbox("Compile scripts ", info.build_data.configs[info.config_name].compile_scripts)
            
            if compile_changed then
                info.build_data.configs[info.config_name].compile_scripts = compile_out
                set_unsaved()
            end
        end
    ui.end_window()
end

info.init = function(project)
    info.build_data = comp.get_build_data(project)
end

return info