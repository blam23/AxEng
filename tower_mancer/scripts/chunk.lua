local enemies = ax.import("enemy")

local chunk_size = 256

chunk = {}
chunk.__index = chunk

function chunk:new(x, y)
    local instance = setmetatable({}, chunk)
    instance.x = x
    instance.y = y
    instance.visible = true
    instance.enemies = enemies.new()
    return instance
end

function chunk:onscreen()
    return app.window.screen_contains_region(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size)
end

function chunk:contains_point(px, py)
    local left = self.x * chunk_size
    local right = left + chunk_size
    local top = self.y * chunk_size
    local bottom = top + chunk_size
    return px >= left and px <= right and py >= top and py <= bottom
end

function chunk:setup(enemy_count)
    self.enemies:setup(enemy_count, self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size)
end

function chunk:show()
    self.visible = true
    -- re-register sprites
    self.enemies:show()
    self.enemies:enable()
end

function chunk:hide()
    self.visible = false
    -- unregister sprites
    self.enemies:hide()
    self.enemies:disable()
end

function chunk:tick(delta)
    self.enemies:tick(delta)
end

function chunk:render(delta, pass)
end

return chunk
