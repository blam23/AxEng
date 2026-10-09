-- -cxr --allow-io --allow-os --in "$(SolutionDir)tower_mancer" --out "E:\TowerMancer"

math.randomseed(0800001066)

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

debug = {
    enabled = false,
    show_noise = false
}
local function debug_key_event(pressed, mods)
    if pressed then
        print("Toggling debug mode. Current state: " .. tostring(debug.enabled))
        debug.enabled = not debug.enabled
    end
end

keyboard.subscribe_key(ax.key_map.esc, esc_key_event)
keyboard.subscribe_key(ax.key_map.f1, debug_key_event)

time = 0
app.on_update.subscribe(function(delta)
    time = time + delta
    scene_manager:tick(delta)
end)

app.window.on_render.subscribe(function(delta, pass)
    scene_manager:render(delta, pass)
end)

app.window.on_resize.subscribe(function(width, height)
    scene_manager:resize(width, height)
end)

app.window.on_ui.subscribe(function(delta)
    scene_manager:ui(delta)
end)


return error_code.Success