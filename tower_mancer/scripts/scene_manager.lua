local class = ax.import("class")

local manager = {}
local manager_mt = class(manager)

function manager:new()
    return setmetatable({
        current_scene = nil,
        scenes = {},
        paused = false,
        pause_scene = nil
    }, manager_mt)
end

local paused = false

function manager:init(scenes)
    self.pause_scene = ax.import("pause")
    self.scenes = scenes
end

function manager:unpause()
    self.paused = false
end

function manager:toggle_pause()
    if self.current_scene.pausable then
        self.paused = not self.paused
    end
end

function manager:tick(delta)
    if self.paused then
        self.pause_scene:tick(delta)
    else
        if self.current_scene then
            self.current_scene:tick(delta)
        end
    end
end

function manager:resize(width, height)
    if self.current_scene then
        self.current_scene:resize(width, height)
    end
    if self.pause_scene then
        self.pause_scene:resize(width, height)
    end
end

function manager:render(delta, pass)
    if self.current_scene then
        self.current_scene:render(delta, pass)
    end

    if self.paused then
        self.pause_scene:render(delta, pass)
    end
end

function manager:ui(delta)
    if not self.paused and self.current_scene then
        self.current_scene:ui(delta)
    end
end

function manager:change_scene(scene_name)
    if self.scenes[scene_name] then
        if self.current_scene then
            self.current_scene:stop()
        end
        self.current_scene = self.scenes[scene_name]
        self.current_scene:start()
    end
end


return manager