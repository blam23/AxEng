-- AxEng Lua Compiler

-- cmd line to compile this:
-- -cxv --allow-io --allow-os --in "$(SolutionDir)compiler" --out "$(SolutionDir)AxCompiler2"
-- Add --source to the AxEng command to output plain Lua scripts and manifest.lua.

local comp = ax.import("@compiler_core")

local function check_and_copy_script(script_in, script_out, source_scripts)
    local success, res = comp.check_script(script_in)

    if not success then
        log.error(res)
        return false
    end

    local ofile = io.open(script_out, "wb")
    if ofile == nil then
        log.error("Cannot open output script '" .. script_out .. "'.")
        return false
    end

    if source_scripts then
        local ifile = io.open(script_in, "rb")
        if ifile == nil then
            log.error("Cannot open source script '" .. script_in .. "'.")
            ofile:close()
            return false
        end
        ofile:write(ifile:read("*all"))
        ifile:close()
    else
        ofile:write(string.dump(res))
    end
    ofile:close()

    return true
end

local built_scripts = {}
local function check_and_copy_scripts(project, in_directory, out_directory, source_scripts)
    for script_name, script_path in pairs(project.scripts) do
        if script_path:sub(-4) ~= ".lua" then
            log.error("All scripts must have '.lua' extension, '" .. script_path .. "' does not.")
            return false
        end

        local in_path = in_directory .. "/" .. script_path
        local out_script_path = source_scripts and script_path or (script_path .. "c")
        local out_path = out_directory .. "/" .. out_script_path

        if not check_and_copy_script(in_path, out_path, source_scripts) then
            return false
        end

        built_scripts[script_name] = out_script_path
        log.debug("Compiled script: " .. script_name)
    end

    log.info("Compiled scripts.")

    return true
end

local function check_and_copy_texture(texture_in, texture_out)
    -- todo: Make sure texture is valid
    local sfile = io.open(texture_in, "rb")
    local script = sfile:read("*all")
    sfile:close()

    local ofile = io.open(texture_out, "wb")
    ofile:write(script)
    ofile:close()

    return true
end

local function copy_file(file_in, file_out)
    local sfile = io.open(file_in, "rb")
    local script = sfile:read("*all")
    sfile:close()

    local ofile = io.open(file_out, "wb")
    ofile:write(script)
    ofile:close()

    return true
end

local function is_font_texture_name(name)
    return name:lower():find("font", 1, true) ~= nil
end

local function check_and_copy_textures(project, in_directory, out_directory)
    if project.textures == nil then
        return true
    end

    local copy_icon = false

    if project.build_texture_atlas then
        local icon_texture = nil
        local textures = {}
        local atlas_texture_name = "__texture_atlas"
        local lookup = {}
        ax.print_table(project.textures)
        for k, v in pairs(project.textures) do
            if k == project.icon then
                copy_icon = true
                icon_texture = v
            elseif is_font_texture_name(k) then
                log.debug("Skipping project font texture; AxEng generates it: " .. k)
            else
                table.insert(textures, in_directory .. "/" .. v)
                lookup[in_directory .. "/" .. v] = k
            end
        end
        table.insert(textures, "__builtin_font")
        lookup["__builtin_font"] = "__builtin_font"
        ax.print_table(textures)
        app.res.create_mega_texture("compiler_temp_texture_atlas", textures, 2.0)
        app.res.save_texture_io("compiler_temp_texture_atlas", out_directory .. "/texture_atlas.png")

        local res = app.res.get_texture_regions("compiler_temp_texture_atlas")
        project.atlas_regions = {}
        if res.valid and res.regions then
            for k, v in pairs(res.regions) do
                log.debug("Texture atlas region: " .. k)
                project.atlas_regions[lookup[k]] = v
            end
        end

        project.textures = { [atlas_texture_name] = "texture_atlas.png" }

        if icon_texture then
            project.textures[project.icon] = icon_texture
        end

        log.debug("Compiled texture atlas.")
        project.atlas_texture = atlas_texture_name
    else
        local copied_textures = {}
        for texture_name, texture_path in pairs(project.textures) do
            if is_font_texture_name(texture_name) then
                log.debug("Skipping project font texture; AxEng generates it: " .. texture_name)
            elseif texture_path:sub(-4) ~= ".png" then
                log.error("All textures must have '.png' extension, '" .. texture_path .. "' does not.")
                return false
            else

                local in_path = in_directory .. "/" .. texture_path
                local out_path = out_directory .. "/" .. texture_path

                if not check_and_copy_texture(in_path, out_path) then
                    return false
                end

                copied_textures[texture_name] = texture_path
                log.debug("Compiled texture: " .. texture_name)
            end
        end
        project.textures = copied_textures

        project.atlas_mode = false
    end

    if copy_icon then
        local in_path = in_directory .. "/" .. project.textures[project.icon]
        local out_path = out_directory .. "/" .. project.textures[project.icon]
        if not check_and_copy_texture(in_path, out_path) then
            return false
        end

        log.debug("Compiled texture: " .. project.icon)
    end

    log.info("Compiled textures")

    return true
end

local supported_music_extensions = { [".ogg"] = true, [".mp3"] = true, [".flac"] = true, [".wav"] = true }

local function check_and_copy_music(project, in_directory, out_directory)
    if project.music == nil then
        return true
    end

    for music_name, music_path in pairs(project.music) do
        local ext = (music_path:match("(%.[^%./\\]+)$") or ""):lower()
        if not supported_music_extensions[ext] then
            log.error("Music must be .ogg, .mp3, .flac or .wav, '" .. music_path .. "' is not.")
            return false
        end

        local in_path = in_directory .. "/" .. music_path
        local out_path = out_directory .. "/" .. music_path
        local sfile = io.open(in_path, "rb")
        if sfile == nil then
            log.error("Cannot open music file '" .. in_path .. "'.")
            return false
        end
        sfile:close()

        if not copy_file(in_path, out_path) then
            return false
        end

        log.debug("Compiled music: " .. music_name)
    end

    log.info("Compiled music.")

    return true
end

local function copy_custom_resources(project, in_directory, out_directory)
    if project.custom_resources == nil then
        return true
    end

    for i, res_path in pairs(project.custom_resources) do
        local in_path = in_directory .. "/" .. res_path
        local out_path = out_directory .. "/" .. res_path
        log.debug("Copying Resource: '" .. in_path .. "' -> '" .. out_path .. "'..")

        if not copy_file(in_path, out_path) then
            return false
        end

        log.debug("Copied Resource: " .. res_path)
    end

    log.info("Copied custom resources.")

    return true
end

local function gen_script_text()
    local ret = ""

    for k,v in pairs(built_scripts) do
        ret = ret .. "    " .. k .. " = \"" .. v .. "\",\n"
    end

    return ax.rstrip(ret)
end

local function gen_textures_text(project)
    local ret = ""

    if project.textures == nil then
        return ret
    end

    for k,v in pairs(project.textures) do
        ret = ret .. "    " .. k .. " = \"" .. v .. "\",\n"
    end

    return ax.rstrip(ret)
end

local function gen_music_text(project)
    local ret = ""

    if project.music == nil then
        return ret
    end

    for k,v in pairs(project.music) do
        ret = ret .. "    [\"" .. k .. "\"] = \"" .. v .. "\",\n"
    end

    return ax.rstrip(ret)
end

local function gen_types_text(project)
    local ret = ""

    if project.types == nil then
        return ret
    end
    for k,v in pairs(project.types) do
        ret = ret .. "    \"" .. k.. "\",\n"
    end

    return ax.rstrip(ret)
end

local function gen_permissions_text(project)
    local ret = ""
    if project.required_permissions == nil then
        return ret
    end
    for i, perm in pairs(project.required_permissions) do
        ret = ret .. "    \"" .. perm .. "\",\n"
    end
    return ax.rstrip(ret)
end

local function gen_atlas_regions_text(project)
    local ret = ""

    if project.atlas_regions == nil then
        return ret
    end

    for k,v in pairs(project.atlas_regions) do
        ret = ret .. "    " .. k .. " = { " .. v[1] .. ", " .. v[2] .. ", " .. v[3] .. ", " .. v[4] .. " },\n"
    end

    return ax.rstrip(ret)
end

local function create_manifest(project, dir, source_scripts)
    -- TODO: I need to make this use the project directory directly
    local tfile = io.open("AxCompiler/templates/output_template.luat", "r")
    if tfile == nil then
        log.error("Cannot open manifest template file!")
        return false
    end
    local template = tfile:read("*all")
    tfile:close()

    local data = {
        NAME = project.name,
        ENTRY_POINT = project.entry_point,
        ICON = project.icon or "null",
        HEADLESS = project.headless and tostring(project.headless) or false,
        WINDOW_WIDTH = 1920,
        WINDOW_HEIGHT = 1080,
        WINDOW_VSYNC = project.vsync ~= nil and tostring(project.vsync) or true,
        WINDOW_RESIZABLE = project.resizable ~= nil and tostring(project.resizable) or "true",
        SCRIPTS = gen_script_text(),
        TEXTURES = gen_textures_text(project),
        MUSIC = gen_music_text(project),
        TYPES = gen_types_text(project),
        PERMISSIONS = gen_permissions_text(project),
        ATLAS_TEXTURE = project.atlas_texture or "null",
        ATLAS_REGIONS = gen_atlas_regions_text(project),
    }
    local output = ax.template_replace(template, data)

    log.debug("Manifest:\n" .. output)

    local chunk, err_msg = load(output, "!manifest", "t")

    -- make sure it loads
    if chunk == nil then
        log.error(err_msg)
        return false
    end

    local manifest_path = dir .. (source_scripts and "/manifest.lua" or "/manifest.luac")
    local ofile = io.open(manifest_path, "wb")
    if ofile == nil then
        log.error("Cannot open manifest output '" .. manifest_path .. "'.")
        return false
    end
    if source_scripts then
        ofile:write(output)
    else
        ofile:write(string.dump(chunk))
    end
    ofile:close()

    log.info("Created manifest.")

    return true
end


local function get_in_out_from_args()
    local in_indir_state = false
    local in_outdir_state = false
    local ret_indir = ""
    local ret_outdir = ""

    for i in pairs(args) do
        if in_indir_state then
            ret_indir = ax.rstrip(args[i])
            in_indir_state = false
        elseif in_outdir_state then
            ret_outdir = ax.rstrip(args[i])
            in_outdir_state = false
        else
            if args[i] == "--in" then
                in_indir_state = true
                in_outdir_state = false
            end
            if args[i] == "--out" then
                in_outdir_state = true
            end
        end
    end

    return ret_indir, ret_outdir
end

local function is_source_mode()
    for _, arg in pairs(args) do
        if arg == "--source" then
            return true
        end
    end
    return false
end

function compile(in_directory, out_directory, source_scripts)
    local project = comp.open_project(in_directory)

    local validated = comp.validate_project(project)
    if not validated then
        log.error("Failed to validate project.")
        return error_code.InvalidConfiguration
    end

    local checked_scripts = check_and_copy_scripts(project, in_directory, out_directory, source_scripts)
    if not checked_scripts then
        log.error("Failed to validate scripts.")
        return error_code.InvalidScript
    end

    local checked_textures = check_and_copy_textures(project, in_directory, out_directory)
    if not checked_textures then
        log.error("Failed to validate textures.")
        return error_code.InvalidTexture
    end

    local checked_music = check_and_copy_music(project, in_directory, out_directory)
    if not checked_music then
        log.error("Failed to validate music.")
        return error_code.InvalidResource
    end

    local copied_resources = copy_custom_resources(project, in_directory, out_directory)
    if not copied_resources then
        log.error("Failed to copy custom resources.")
        return error_code.InvalidResource
    end

    local created_manifest = create_manifest(project, out_directory, source_scripts)
    if not created_manifest then
        log.error("Failed to create manifest.")
        return error_code.InvalidConfiguration
    end

    -- for C++ to access if it wants
    compiled_project = project

    log.info("<Build> Successfully compiled to '" .. out_directory .. "'.")
    return error_code.Success
end

local in_directory, out_directory = get_in_out_from_args()
log.info("<Build> Compiling from '" .. in_directory .. " to '" .. out_directory .. "'..")
return compile(in_directory, out_directory, is_source_mode())
