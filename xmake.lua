set_project("AxEng")
set_version("0.1.0")
set_xmakever("2.9.0")

set_allowedplats("windows")
set_allowedarchs("x64")
set_toolchains("clang")
set_runtimes("MD")
set_languages("c17", "c++23")
set_encodings("utf-8")
set_warnings("all", "error")
add_rules("mode.debug", "mode.release")

local vcpkg_triplet = "x64-windows-static-md-release"
local vcpkg_triplet_dir = path.join(os.projectdir(), "triplets")

-- Dependencies come from vcpkg.json (vcpkg manifest mode). The rule installs them into
-- build/vcpkg_installed using release-only libraries for both app configurations.
-- The vcpkg location is taken from VCPKG_ROOT (or vcpkg on PATH).
rule("vcpkg.manifest")
    on_load(function (target)
        local installroot = path.join(os.projectdir(), "build", "vcpkg_installed")
        local prefix = path.join(installroot, vcpkg_triplet)
        local stamp = path.join(installroot, ".stamp")
        local manifest = path.join(os.projectdir(), "vcpkg.json")
        local triplet_file = path.join(vcpkg_triplet_dir, vcpkg_triplet .. ".cmake")
        if not os.isfile(stamp) or os.mtime(manifest) > os.mtime(stamp) or os.mtime(triplet_file) > os.mtime(stamp) then
            local vcpkg = "vcpkg"
            local vcpkg_root = os.getenv("VCPKG_ROOT")
            if vcpkg_root and os.isfile(path.join(vcpkg_root, "vcpkg.exe")) then
                vcpkg = path.join(vcpkg_root, "vcpkg.exe")
            end
            os.mkdir(installroot)
            os.execv(vcpkg, {"install", "--triplet", vcpkg_triplet,
                             "--overlay-triplets=" .. vcpkg_triplet_dir,
                             "--x-manifest-root=" .. os.projectdir(),
                             "--x-install-root=" .. installroot})
            io.writefile(stamp, "")
        end
        target:add("sysincludedirs", path.join(prefix, "include"), {public = true})
        target:add("linkdirs", path.join(prefix, "lib"), {public = true})
        for _, lib in ipairs(os.files(path.join(prefix, "lib", "*.lib"))) do
            target:add("links", path.basename(lib), {public = true})
        end
    end)
-- Dawn's D3D12 backend needs dxcompiler.dll / dxil.dll next to the executable.
rule("vcpkg.runtime_dlls")
    after_build(function (target)
        local prefix = path.join(os.projectdir(), "build", "vcpkg_installed", vcpkg_triplet)
        local bindir = path.join(prefix, "bin")
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
    -- stb_image.h defines helpers that are only used on some code paths
    -- #embed is a Clang extension in C++ (it is not part of C++26)
    add_files(src .. "/axeng/core/lua/external/lua_libs.cpp", {force = {cxxflags = "-Wno-c23-extensions"}})
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
    add_deps("AxEngLib")
    add_tests("default")
