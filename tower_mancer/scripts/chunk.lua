local enemies = ax.import("enemy")

local chunk_size = 1024

chunk = {}
chunk.__index = chunk

function chunk:new(x, y)
    local instance = setmetatable({}, chunk)
    instance.x = x
    instance.y = y
    instance.visible = true
    instance.enemies = enemies.new()
    instance.debug_color = ax.vec4_hsla_to_rgba(vec4:new(math.random() * 360, 0.6, 0.7, 1.0))
    instance.debug_color_fill = instance.debug_color:copy()
    instance.debug_color_fill.a = 0.1
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

local debug_chunk_outline_thickness = 2.0
function chunk:render(delta, pass)
    if (debug.enabled) then
        pass.debug_rect_fill(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size, self.debug_color_fill)
        pass.debug_rect_outline(self.x * chunk_size, self.y * chunk_size, chunk_size, chunk_size, self.debug_color, debug_chunk_outline_thickness)
    end
end

return chunk
