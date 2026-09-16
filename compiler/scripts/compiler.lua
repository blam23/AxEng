-- AxEng Lua Compiler

-- cmd line to compile this:
-- -cxv --in "$(SolutionDir)compiler" --out "$(SolutionDir)AxCompiler2"

local comp = ax.import("@compiler_core")

local function check_and_copy_script(script_in, script_out)
    local success, res = comp.check_script(script_in)

    if not success then
        log.error(res)
        return false
    end

    local ofile = io.open(script_out, "wb")
    ofile:write(string.dump(res))
    ofile:close()

    return true
end

local built_scripts = {}
local function check_and_copy_scripts(project, in_directory, out_directory)
    for script_name, script_path in pairs(project.scripts) do
        if script_path:sub(-4) ~= ".lua" then
            log.error("All scripts must have '.lua' extension, '" .. script_path .. "' does not.")
            return false
        end

        local in_path = in_directory .. "/" .. script_path
        local out_path = out_directory .. "/" .. script_path .. "c"

        if not check_and_copy_script(in_path, out_path) then
            return false
        end

        built_scripts[script_name] = script_path .. "c"
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

local function check_and_copy_textures(project, in_directory, out_directory)
    if project.textures == nil then
        return true
    end

    for texture_name, texture_path in pairs(project.textures) do
        if texture_path:sub(-4) ~= ".png" then
            log.error("All textures must have '.png' extension, '" .. texture_path .. "' does not.")
            return false
        end

        local in_path = in_directory .. "/" .. texture_path
        local out_path = out_directory .. "/" .. texture_path

        if not check_and_copy_texture(in_path, out_path) then
            return false
        end

        log.debug("Compiled texture: " .. texture_name)
    end

    log.info("Compiled textures.")

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

local function create_manifest(project, dir, strip_debug_output)
    local tfile = io.open("../compiler/templates/output_template.luat", "r")
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
        WINDOW_VSYNC = true,
        SCRIPTS = gen_script_text(),
        TEXTURES = gen_textures_text(project),
        TYPES = gen_types_text(project),
    }
    local output = ax.template_replace(template, data)

    log.debug("Manifest:\n" .. output)

    local chunk, err_msg = load(output, "!manifest", "t", strip_debug_output)

    -- make sure it loads
    if chunk == nil then
        log.error(err_msg)
        return false
    end

    local ofile = io.open(dir .. "/manifest.luac", "wb")
    ofile:write(string.dump(chunk))
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

function compile(in_directory, out_directory)
    local project = comp.open_project(in_directory)

    local validated = comp.validate_project(project)
    if not validated then
        log.error("Failed to validate project.")
        return error_code.InvalidConfiguration
    end

    local checked_scripts = check_and_copy_scripts(project, in_directory, out_directory)
    if not checked_scripts then
        log.error("Failed to validate scripts.")
        return error_code.InvalidScript
    end

    local checked_textures = check_and_copy_textures(project, in_directory, out_directory)
    if not checked_textures then
        log.error("Failed to validate textures.")
        return error_code.InvalidTexture
    end

    local copied_resources = copy_custom_resources(project, in_directory, out_directory)
    if not copied_resources then
        log.error("Failed to copy custom resources.")
        return error_code.InvalidResource
    end

    local created_manifest = create_manifest(project, out_directory, true)
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
return compile(in_directory, out_directory)
