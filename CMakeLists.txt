cmake_minimum_required(VERSION 3.23)

# Use the vcpkg toolchain automatically when running on a machine that has
# vcpkg installed (e.g. GitHub-hosted Windows runners) and no toolchain file
# was explicitly requested. This mirrors the "vcpkg integrate install" /
# manifest-mode restore that used to be wired up for the MSBuild build.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_INSTALLATION_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(AxEng LANGUAGES CXX C)

if(WIN32)
    enable_language(RC)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# AxEngLib/AxEng/Tests all previously used the dynamic (non-static) CRT
# (UseDebugLibraries true/false without <RuntimeLibrary>MultiThreaded[Debug]</...>
# maps to MSVC's default dynamic CRT selection: /MDd for Debug, /MD for Release).
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# WholeProgramOptimization=true on every Release configuration in the old
# .vcxproj files (/GL at compile time, /LTCG at link time).
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# --- Third-party dependencies (see vcpkg.json for the pinned package list) ---
find_package(spdlog CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED)
find_package(minizip-ng CONFIG REQUIRED)

option(AXENG_BUILD_TESTS "Build the Google Test suite" ON)
if(AXENG_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
endif()

# Applies the MSVC-specific compiler/linker options that used to live in the
# per-configuration ItemDefinitionGroups of the .vcxproj files. Every AxEng
# target had GenerateDebugInformation=true and SubSystem=Console in every
# configuration, so those two are applied unconditionally; everything else
# that varied between projects is passed in as an argument.
function(axeng_configure_target target)
    set(options TREAT_WARNINGS_AS_ERRORS BIGOBJ_IN_DEBUG)
    set(oneValueArgs WARNING_LEVEL CXX_STANDARD_FLAG C_STANDARD_FLAG)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT MSVC)
        message(WARNING "axeng_configure_target(${target}): no MSVC-specific flags applied for this compiler")
        return()
    endif()

    target_compile_options(${target} PRIVATE
        /${ARG_WARNING_LEVEL}  # WarningLevel
        /permissive-           # ConformanceMode=true
        /sdl                   # SDLCheck=true
        /utf-8                 # AdditionalOptions: /utf-8
        $<$<CONFIG:Release>:/Zi /Gy /Oi> # GenerateDebugInformation / FunctionLevelLinking / IntrinsicFunctions
    )

    if(ARG_BIGOBJ_IN_DEBUG)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/bigobj>)
    endif()

    if(ARG_CXX_STANDARD_FLAG)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ARG_CXX_STANDARD_FLAG}>)
    endif()
    if(ARG_C_STANDARD_FLAG)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:${ARG_C_STANDARD_FLAG}>)
    endif()

    if(ARG_TREAT_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE /WX)
    endif()

    target_compile_definitions(${target} PRIVATE _CONSOLE)

    # GenerateDebugInformation=true was set for every configuration, including
    # Release; CMake/MSVC already emits debug info for Debug by default.
    target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/DEBUG>)
endfunction()

add_subdirectory(AxEngLib)
add_subdirectory(AxEng)

if(AXENG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
