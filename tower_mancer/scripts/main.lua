local draw_helpers = ax.import("draw_helpers")

local scenes = {
    menu = ax.import("menu"),
    game = ax.import("game"),
    options = ax.import("options"),
}

local pause_scene = ax.import("pause")
local paused = false

function unpause()
    paused = false
end

current_scene = scenes.menu

function change_scene(scene_name)
    if scenes[scene_name] then
        print("Changing scene to ", scene_name)
        print("Value: ", scenes[scene_name])
        current_scene.stop()
        current_scene = scenes[scene_name]
        current_scene.start()
    end
end

local function esc_key_event(pressed, mods)
    if pressed then
        paused = not paused
    end
end

keyboard.subscribe_key(ax.key_map.esc, esc_key_event)

time = 0
app.on_update.subscribe(function(delta, pass)
    time = time + delta
    if paused then
        pause_scene.tick(delta)
    else
        current_scene.tick(delta)
    end
end)

app.window.on_render.subscribe(function(delta, pass)
    current_scene.render(delta, pass)

    if paused then
        pause_scene.render(delta, pass)
    end
end)

app.window.set_clear_color(0.05, 0.05, 0.08, 1.0)

return error_code.Success
