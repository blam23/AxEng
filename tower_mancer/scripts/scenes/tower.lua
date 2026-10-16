local texture = app.res.get_texture("tower")
local tower_sprite_area = { 0, 0, 48, 48 }

local tower = {}

function tower:release()
    if self.sprite then app.sprites.free(self.sprite) end
    self.sprite = nil
end

function tower:setup(pos_x, pos_y)
    tower.sprite = app.sprites.allocate()
    tower.sprite.scale.x = 2
    tower.sprite.scale.y = 2
    tower.sprite.z = pos_y + (tower_sprite_area[4] * tower.sprite.scale.y)
    app.sprites.setup(tower.sprite, texture, pos_x - 48, pos_y - 48, tower_sprite_area[1], tower_sprite_area[2], tower_sprite_area[3], tower_sprite_area[4])
end

function tower:tick(delta)
end

return tower
