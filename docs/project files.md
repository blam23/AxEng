# Project files outside the project folder

`project.json` names its scripts, textures, music, audio samples and custom resources by paths relative to the
project folder (the `--in` folder). A path may also leave the project folder, so several projects can share code or
assets kept beside them:

```json
"scripts": {
    "main": "scripts/main.lua",
    "ui": "../shared/scripts/ui.lua"
}
```

The compiler mirrors the project's own folders into the output folder, so files inside the project keep their paths.
A file outside it is written to the top of the output folder under a flattened name: each `..` becomes `_up` and the
folders are joined with `_` (`../shared/scripts/ui.lua` compiles to `_up_shared_scripts_ui.luac`). The manifest lists
the flattened path, so `ax.import("ui")` and texture names work as usual, and nothing is written outside the output
folder.
