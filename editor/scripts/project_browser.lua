function project_browser()
    ui.w_begin("Script Browser")
        for k,v in pairs(project.scripts) do
            ui.text("Script: " .. k .. " -> " .. v)
        end
    ui.w_end()
    ui.w_begin("Texture Browser")
        for k,v in pairs(project.textures) do
            ui.text("Texture: " .. k .. " -> " .. v)
        end
    ui.w_end()
end


log.debug("Loaded project browser.")
