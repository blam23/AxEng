set_project("AxEng")
set_version("0.1.0")
set_xmakever("2.9.0")

set_allowedplats("windows", "wasm")
set_allowedarchs("x64", "wasm32")
set_allowedmodes("debug", "release")
-- The browser build uses Emscripten (xmake f -p wasm --emsdk=<emsdk directory>); see docs/wasm.md.
if is_plat("wasm") then
    set_toolchains("emcc")
else
    set_toolchains("clang")
end
if is_plat("wasm") then
    -- Emscripten's EM_ASM (used by miniaudio) is only available in the GNU language modes.
    set_languages("gnu17", "gnuxx23")
else
    set_languages("c17", "c++23")
end
set_encodings("utf-8")
set_warnings("all", "error")
-- LuaJIT's protected calls need sol2 to convert C++ callback exceptions into Lua errors.
add_defines("SOL_EXCEPTIONS_SAFE_PROPAGATION=0")
add_rules("mode.debug", "mode.release")

local engine_root = os.scriptdir()

option("vcpkg_full_check")
    set_default(false)
    set_showmenu(true)
    set_description("Run vcpkg manifest installation and ABI checks")
option_end()

option("tracy")
    set_default(true)
    set_showmenu(true)
    set_description("Enable the Tracy profiler client")
option_end()

local vcpkg_debug = is_mode("debug")
local vcpkg_wasm = is_plat("wasm")
local vcpkg_triplet = vcpkg_wasm and "wasm32-emscripten" or "x64-windows-static-md"
local vcpkg_install_name = vcpkg_wasm and "vcpkg_installed_wasm" or "vcpkg_installed"
local vcpkg_triplet_dir = path.join(engine_root, "triplets")
local vcpkg_config_dir = vcpkg_debug and "debug" or ""
if not vcpkg_wasm then
    set_runtimes(vcpkg_debug and "MDd" or "MD")
    add_defines("_ITERATOR_DEBUG_LEVEL=" .. (vcpkg_debug and "2" or "0"))
end
if is_mode("debug") then
    add_defines("AX_DEBUG_BUILD")
end

if vcpkg_wasm then
    -- WebAssembly threads, exceptions and setjmp/longjmp share one model with the vcpkg triplet's libraries. WebGPU and
    -- GLFW come from Emscripten ports rather than vcpkg.
    local wasm_flags = {"-pthread", "-fwasm-exceptions", "-sSUPPORT_LONGJMP=wasm", "--use-port=emdawnwebgpu", "--use-port=contrib.glfw3"}
    add_cxflags(wasm_flags, {force = true})
    -- The sources carry MSVC warning pragmas, which clang-cl understands and clang does not.
    add_cxflags("-Wno-unknown-pragmas", {force = true})
    -- LuaJIT, which the scripts were written for, converts a number with a fraction to an integer argument (sol2 rounds it);
    -- Lua 5.4 would refuse it.
    add_defines("SOL_NO_CHECK_NUMBER_PRECISION=1", {public = true})
    add_ldflags(wasm_flags, {force = true})
end

-- Dependencies come from vcpkg.json (vcpkg manifest mode). Full checking installs them into
-- build/vcpkg_installed; quick builds use the existing installation.
rule("vcpkg.manifest")
    on_load(function (target)
        local installroot = path.join(engine_root, "build", vcpkg_install_name)
        local prefix = path.join(installroot, vcpkg_triplet)
        local vcpkg = "vcpkg"
        local vcpkg_root = os.getenv("VCPKG_ROOT")
        if vcpkg_root and os.isfile(path.join(vcpkg_root, "vcpkg.exe")) then
            vcpkg = path.join(vcpkg_root, "vcpkg.exe")
        end
        local install_arguments = {"install", "--triplet", vcpkg_triplet,
                                   "--overlay-triplets=" .. vcpkg_triplet_dir,
                                   "--overlay-ports=" .. path.join(engine_root, "ports"),
                                   "--x-manifest-root=" .. engine_root,
                                   "--x-install-root=" .. installroot}
        if vcpkg_wasm then
            if has_config("vcpkg_full_check") then
                local emsdk = get_config("emsdk") or os.getenv("EMSDK")
                assert(emsdk and os.isdir(emsdk), "The WebAssembly build needs the Emscripten SDK: xmake f -p wasm --emsdk=<directory>")
                os.execv(vcpkg, install_arguments, {envs = {
                    EMSDK = emsdk,
                    EMSCRIPTEN_ROOT = path.join(emsdk, "upstream", "emscripten"),
                }})
            end
        elseif has_config("vcpkg_full_check") then
            local toolchain = target:toolchain("clang")
            assert(toolchain and toolchain:check(), "AxEng requires the Windows clang toolchain with an MSVC SDK")
            local vcvars = toolchain:config("vcvars")
            assert(vcvars and vcvars.VSInstallDir and vcvars.VCToolsVersion and vcvars.WindowsSDKVersion,
                   "Unable to determine the MSVC toolset and Windows SDK selected by clang")
            local cc = toolchain:tool("cc")
            assert(cc, "Unable to locate xmake's selected clang compiler")
            local clang_cl = path.join(path.directory(cc), "clang-cl.exe")
            assert(os.isfile(clang_cl), "Missing clang-cl beside xmake's selected compiler: " .. clang_cl)
            os.execv(vcpkg, install_arguments, {envs = {
                VCPKG_VISUAL_STUDIO_PATH = vcvars.VSInstallDir:gsub("[/\\]+$", ""),
                AXENG_CLANG_CL = clang_cl,
                AXENG_VS_TOOLSET_VERSION = vcvars.VCToolsVersion,
                AXENG_WINDOWS_SDK_VERSION = vcvars.WindowsSDKVersion:gsub("[/\\]+$", "")
            }})
        end
        local libdir = path.join(prefix, vcpkg_config_dir, "lib")
        assert(os.isdir(libdir), "Missing vcpkg libraries for the selected build mode: " .. libdir)
        target:add("sysincludedirs", path.join(prefix, "include"), {public = true})
        target:add("linkdirs", libdir, {public = true})
        if vcpkg_wasm then
            -- Profiling is not available in the browser; these headers stand in for Tracy's.
            target:add("sysincludedirs", path.join(engine_root, "web", "tracy_stub"), {public = true})
            target:add("sysincludedirs", path.join(prefix, "include", "lua54"), {public = true})
            for _, lib in ipairs(os.files(path.join(libdir, "lib*.a"))) do
                target:add("links", path.basename(lib):sub(4), {public = true})
            end
        else
            target:add("sysincludedirs", path.join(prefix, "include", "tracy"), {public = true})
            target:add("sysincludedirs", path.join(prefix, "include", "luajit"), {public = true})
            for _, lib in ipairs(os.files(path.join(libdir, "*.lib"))) do
                target:add("links", path.basename(lib), {public = true})
            end
        end
    end)
-- dxcompiler.dll / dxil.dll for Dawn devices that keep the default DXC shader compiler, such as the GPU tests.
-- ax::Window compiles with FXC, so applications do not need to ship them.
rule("vcpkg.runtime_dlls")
    after_build(function (target)
        local prefix = path.join(engine_root, "build", vcpkg_install_name, vcpkg_triplet)
        local bindir = path.join(prefix, vcpkg_config_dir, "bin")
        for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
            os.cp(dll, target:targetdir())
        end
    end)

-- Copy Compiler & Lua Tests to the target directory
-- This allows LuaTests.RunLuaTestSuite to run from the target directory directly
rule("copy.lua_tests")
    after_build(function (target)
        os.cp(path.join(engine_root, "lua_tests"), target:targetdir())
        os.cp(path.join(engine_root, "AxCompiler"), target:targetdir())
        local compiler_output = path.join(target:targetdir(), "AxCompiler")
        os.cp(path.join(engine_root, "compiler", "project.json"), compiler_output)
        os.cp(path.join(engine_root, "compiler", "scripts"), compiler_output)
        os.cp(path.join(engine_root, "compiler", "templates"), compiler_output)
    end)

local function enable_release_pdbs()
    if is_mode("release") then
        set_symbols("debug")
    end
end

local src = path.join(engine_root, "src")

target("AxEngLib")
    set_kind("static")
    enable_release_pdbs()
    add_rules("vcpkg.manifest")
    add_files(src .. "/axeng/**.cpp", src .. "/axeng/**.c")
    -- #embed is a Clang extension in C++ (it is not part of C++26)
    add_files(src .. "/axeng/core/lua/external/lua_libs.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/axeng/core/font_data.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/axeng/core/shapes/shape_renderer.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/axeng/core/particles/particle_system.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/axeng/core/lighting/light_renderer.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/axeng/core/lighting/radiance_cascades.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/axeng/core/window.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    -- stb_image.h defines helpers that are only used on some code paths
    add_files(src .. "/axeng/core/texture.cpp", {force = {cxxflags = "-Wno-unused-function"}})
    -- Third-party single file implementations (miniaudio + stb_vorbis)
    add_files(src .. "/axeng/core/audio/miniaudio_impl.c", {force = {cflags = "-w"}})
    add_includedirs(src, {public = true})
    add_defines("IMGUI_IMPL_WEBGPU_BACKEND_DAWN", {public = true})
    if vcpkg_wasm then
        -- The browser supplies the surface and the windowing, not GLFW's native handles.
        remove_files(src .. "/axeng/external/glfw3webgpu.c")
    else
        if has_config("tracy") then
            add_defines("TRACY_ENABLE", "TRACY_ON_DEMAND", {public = true})
        end
        add_cxflags("-Wa,-mbig-obj", {tools = {"clang"}})
        add_syslinks("bcrypt", "gdi32", "user32", "shell32", "advapi32", "kernel32", "onecore", "ole32", "comctl32", "dxgi", "dxguid", "d3d11", "d3d12", "d3dcompiler", "synchronization", {public = true})
    end

target("AxEng")
    set_kind("binary")
    enable_release_pdbs()
    add_rules("vcpkg.runtime_dlls")
    add_files(src .. "/app/**.cpp")
    add_deps("AxEngLib")

target("Tests")
    set_kind("binary")
    enable_release_pdbs()
    add_rules("vcpkg.runtime_dlls", "copy.lua_tests")
    add_files(src .. "/tests/**.cpp")
    -- #embed is a Clang extension in C++ (it is not part of C++26)
    add_files(src .. "/tests/core/test_sprite_shader.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/tests/core/test_native_tasks.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/tests/core/test_shape_renderer.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/tests/core/test_particle_system.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/tests/core/test_light_renderer.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/tests/core/test_lua_resource_bindings.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_deps("AxEngLib")
    add_tests("default")
