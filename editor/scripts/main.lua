function test_ui(delta)
    local updated = false

    ui.w_begin("Test Window")
    
    ui.text("Delta: " .. string.format("%.2f", delta * 1000.0) .. "ms")
    ui.text("est. FPS: " .. string.format("%.0f", 1.0/delta))

    show_frog, updated = ui.checkbox("Enable the frog?", show_frog)

    if updated then
        print("FROG TOGGLED")
    end

    if show_frog then
        ui.image(frog_image)
    end

    ui.w_end()
end

frog_image = app.res.get_texture("frog")

if (not frog_image.valid) then
    log.error("Unable to load frog texture!!!1!1!")
    return
end

for k,v in pairs(frog_image) do
    print(k .. " = " .. tostring(v))
end

app.on_ui.subscribe(test_ui)
show_frog = false