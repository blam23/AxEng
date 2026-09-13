assert_success(import(app, "load_project"))
check_project()

assert_success(import(app, "project_browser"))

function test_window(delta)
    ui.w_begin("Test Window")
    
    ui.text("Delta: " .. string.format("%.2f", delta * 1000.0) .. "ms")
    ui.text("est. FPS: " .. string.format("%.0f", 1.0/delta))

    show_frog, updated = ui.checkbox("Show frilly?", show_frog)

    if updated then
        print("FRILLY TOGGLED")
    end

    if show_frog then
        ui.image(frog_image)
    end

    ui.w_end()
end

function main_ui(delta)
    local updated = false

    ui.dock_space_over_viewport()
    --test_window(delta)
    project_browser()
end

frog_image = app.res.get_texture("frilly")

if (not frog_image.valid) then
    log.error("Unable to load frilly!!1!1!")
    return error.AssetNotFound
end

app.on_ui.subscribe(main_ui)
show_frog = false

return error.Success
