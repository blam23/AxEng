-- -cxr --allow-io --allow-os --in "$(SolutionDir)tower_mancer" --out "E:\TowerMancer"

local scene_manager = ax.import("scene_manager")

scene_manager:init({
    menu = ax.import("menu"),
    game = ax.import("game"),
    options = ax.import("options"),
})
scene_manager:change_scene("menu")

local function esc_key_event(pressed, mods)
    if pressed then
        scene_manager:toggle_pause()
    end
end

keyboard.subscribe_key(ax.key_map.esc, esc_key_event)

time = 0
app.on_update.subscribe(function(delta, pass)
    time = time + delta
    scene_manager:tick(delta)
end)

app.window.on_render.subscribe(function(delta, pass)
    scene_manager:render(delta, pass)
end)

app.window.on_resize.subscribe(function(width, height)
    scene_manager:resize(width, height)
end)

app.window.set_clear_color(0.05, 0.05, 0.08, 1.0)

return error_code.Success
