project = {
    name = "My First Project",
    init_script = "init",
    entry_point = "test",
    icon = "icon",
    highlight = "#A256FF"
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
    entity = custom_type.define({
        pos = custom_type.vec2,
        scale = custom_type.vec2,
        rot = custom_type.float,
        texture = custom_type.id,
    }),
}
