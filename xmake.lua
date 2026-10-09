set_project("AxEng")
set_version("0.1.0")
set_xmakever("2.9.0")

set_allowedplats("windows")
set_allowedarchs("x64")
set_allowedmodes("debug", "release")
set_toolchains("clang")
set_languages("c17", "c++23")
set_encodings("utf-8")
set_warnings("all", "error")
add_rules("mode.debug", "mode.release")

local vcpkg_debug = is_mode("debug")
local vcpkg_triplet = "x64-windows-static-md"
local vcpkg_triplet_dir = path.join(os.projectdir(), "triplets")
local vcpkg_config_dir = vcpkg_debug and "debug" or ""
set_runtimes(vcpkg_debug and "MDd" or "MD")
add_defines("_ITERATOR_DEBUG_LEVEL=" .. (vcpkg_debug and "2" or "0"))
if is_mode("debug") then
    add_defines("AX_DEBUG_BUILD")
end

-- Dependencies come from vcpkg.json (vcpkg manifest mode). The rule installs them into
-- build/vcpkg_installed with both configurations; each target links its matching CRT/STL ABI.
-- The vcpkg location is taken from VCPKG_ROOT (or vcpkg on PATH).
rule("vcpkg.manifest")
    on_load(function (target)
        local installroot = path.join(os.projectdir(), "build", "vcpkg_installed")
        local prefix = path.join(installroot, vcpkg_triplet)
        local toolchain = target:toolchain("clang")
        assert(toolchain and toolchain:check(), "AxEng requires the Windows clang toolchain with an MSVC SDK")
        local vcvars = toolchain:config("vcvars")
        assert(vcvars and vcvars.VSInstallDir and vcvars.VCToolsVersion and vcvars.WindowsSDKVersion,
               "Unable to determine the MSVC toolset and Windows SDK selected by clang")
        local cc = toolchain:tool("cc")
        assert(cc, "Unable to locate xmake's selected clang compiler")
        local clang_cl = path.join(path.directory(cc), "clang-cl.exe")
        assert(os.isfile(clang_cl), "Missing clang-cl beside xmake's selected compiler: " .. clang_cl)
        local vcpkg = "vcpkg"
        local vcpkg_root = os.getenv("VCPKG_ROOT")
        if vcpkg_root and os.isfile(path.join(vcpkg_root, "vcpkg.exe")) then
            vcpkg = path.join(vcpkg_root, "vcpkg.exe")
        end
        -- Let vcpkg validate its own ABI cache, including changes to the selected toolset.
        os.execv(vcpkg, {"install", "--triplet", vcpkg_triplet,
                         "--overlay-triplets=" .. vcpkg_triplet_dir,
                         "--x-manifest-root=" .. os.projectdir(),
                         "--x-install-root=" .. installroot}, {envs = {
            VCPKG_VISUAL_STUDIO_PATH = vcvars.VSInstallDir:gsub("[/\\]+$", ""),
            AXENG_CLANG_CL = clang_cl,
            AXENG_VS_TOOLSET_VERSION = vcvars.VCToolsVersion,
            AXENG_WINDOWS_SDK_VERSION = vcvars.WindowsSDKVersion:gsub("[/\\]+$", "")
        }})
        local libdir = path.join(prefix, vcpkg_config_dir, "lib")
        assert(os.isdir(libdir), "Missing vcpkg libraries for the selected build mode: " .. libdir)
        target:add("sysincludedirs", path.join(prefix, "include"), {public = true})
        target:add("linkdirs", libdir, {public = true})
        for _, lib in ipairs(os.files(path.join(libdir, "*.lib"))) do
            target:add("links", path.basename(lib), {public = true})
        end
    end)
-- Dawn's D3D12 backend needs dxcompiler.dll / dxil.dll next to the executable.
rule("vcpkg.runtime_dlls")
    after_build(function (target)
        local prefix = path.join(os.projectdir(), "build", "vcpkg_installed", vcpkg_triplet)
        local bindir = path.join(prefix, vcpkg_config_dir, "bin")
        for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
            os.cp(dll, target:targetdir())
        end
    end)

-- Copy Compiler & Lua Tests to the target directory
-- This allows LuaTests.RunLuaTestSuite to run from the target directory directly
rule("copy.lua_tests")
    after_build(function (target)
        os.cp(path.join(os.projectdir(), "lua_tests"), target:targetdir())
        os.cp(path.join(os.projectdir(), "AxCompiler"), target:targetdir())
    end)

local function enable_release_pdbs()
    if is_mode("release") then
        set_symbols("debug")
    end
end

local src = "src"

target("AxEngLib")
    set_kind("static")
    enable_release_pdbs()
    add_rules("vcpkg.manifest")
    add_files(src .. "/axeng/**.cpp", src .. "/axeng/**.c")
    -- #embed is a Clang extension in C++ (it is not part of C++26)
    add_files(src .. "/axeng/core/lua/external/lua_libs.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    add_files(src .. "/axeng/core/shapes/shape_renderer.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
    -- stb_image.h defines helpers that are only used on some code paths
    add_files(src .. "/axeng/core/texture.cpp", {force = {cxxflags = "-Wno-unused-function"}})
    add_includedirs(src, {public = true})
    add_defines("ENABLE_PROFILER", "IMGUI_IMPL_WEBGPU_BACKEND_DAWN", {public = true})
    add_cxflags("-Wa,-mbig-obj", {tools = {"clang"}})
    add_syslinks("bcrypt", "gdi32", "user32", "shell32", "advapi32", "kernel32", "onecore", "ole32", "dxgi", "dxguid", "d3d11", "d3d12", "d3dcompiler", "synchronization", {public = true})

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
    add_deps("AxEngLib")
    add_tests("default")
