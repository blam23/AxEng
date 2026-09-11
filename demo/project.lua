project = {
    name = "My First Project",
    init_script = "init",
    entry_point = "test",
    icon = "icon",
}

project.scripts = {
    init = "scripts/init.lua",
    test = "scripts/test.lua",
}

project.textures = {
    icon = "assets/icon.png",
    tower = "assets/tower-sheet.png",
}

project.types = {
    entity = type.define({
        pos = type.vec2,
        scale = type.vec2,
        rot = type.float,
        texture = type.id,
    }),
}
