local class = ax.import("class")

scene = {}
local scene_mt = class(scene)

function scene:new(can_be_paused)
    return setmetatable({ pausable = can_be_paused }, scene_mt)
end

function scene:start()
end

function scene:stop()
end

function scene:resize(width, height)
end

function scene:tick(delta)
end

function scene:render(delta, pass)
end

return scene
