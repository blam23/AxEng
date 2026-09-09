-- todo: generate this

app = {
    name = "My First App",
    entry_point = "test.lua",
    window = {
        width = 1920,
        height = 1080,
        title = "AxEng",
        vsync = true,
    },
}

app.textures = {
    icon = "assets/icon.png",
    tower = "assets/tower-sheet.png",
}

app.types = {
    entity = type.define({
        pos = type.vec2,
        scale = type.vec2,
        rot = type.float,
        texture = type.id,
    }),
}
