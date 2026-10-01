add_library(AxEngLib STATIC
    asset.cpp
    asset.h
    asset_manager.h
    axeng.cpp
    axeng.h
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_glfw.h
    backends/imgui_impl_wgpu.cpp
    backends/imgui_impl_wgpu.h
    background_worker.cpp
    background_worker.h
    bind_all.cpp
    bind_all.h
    compiler.cpp
    compiler.h
    custom_type.cpp
    custom_type.h
    debug_view.cpp
    debug_view.h
    error.cpp
    error.h
    error_def.h
    event.cpp
    event.h
    flag_set.hpp
    forward.h
    IconsFontAwesome6.h
    imgui_helper.cpp
    imgui_helper.h
    ImGuiNotify.hpp
    imgui_zoomable_image.h
    keyboard.cpp
    keyboard.h
    lua_bindings.cpp
    lua_bindings.h
    lua_engine.cpp
    lua_engine.h
    lua_error_bindings.cpp
    lua_error_bindings.h
    lua_event_bindings.cpp
    lua_event_bindings.h
    lua_imgui_bindings.cpp
    lua_imgui_bindings.h
    lua_key_bindings.cpp
    lua_key_bindings.h
    lua_libs.cpp
    lua_libs.h
    lua_lib_loader.cpp
    lua_lib_loader.h
    lua_log_bindings.cpp
    lua_log_bindings.h
    application.cpp
    application.h
    lua_mouse_bindings.cpp
    lua_mouse_bindings.h
    lua_texture_bindings.cpp
    lua_texture_bindings.h
    lua_type_bindings.cpp
    lua_type_bindings.h
    glfw3webgpu.c
    glfw3webgpu.h
    lua_window_bindings.cpp
    lua_window_bindings.h
    helpers.h
    imgui_style.h
    mouse.cpp
    mouse.h
    perf_profiler.cpp
    perf_profiler.h
    resource_loader.cpp
    resource_loader.h
    script.cpp
    script.h
    log_timer.cpp
    log_timer.h
    shared.cpp
    shared.h
    stb_image.h
    TextEditor.cpp
    TextEditor.h
    texture.cpp
    texture.h
    window.cpp
    window.h
)

# Resource.h is shared with AxEngLib.rc, but the .rc file itself is compiled
# into the AxEngMain executable (see AxEng/CMakeLists.txt) - lua_libs.cpp
# calls FindResource(nullptr, ...), which resolves resources embedded in
# whichever executable module ends up hosting this static library at runtime.
target_sources(AxEngLib PRIVATE resource.h)

target_include_directories(AxEngLib
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${LUA_INCLUDE_DIR}
)

target_compile_definitions(AxEngLib
    PRIVATE
        ENABLE_PROFILER
        IMGUI_IMPL_WEBGPU_BACKEND_DAWN
)

# vcpkg's classic (non-manifest) MSBuild integration exposed every installed
# package's include/lib paths to every project in the solution, so all of
# AxEngLib's public headers could freely use spdlog, glm, sol2, imgui, etc.
# without per-header link bookkeeping. Linking these PUBLIC here preserves
# that same "everything visible" behavior for AxEng and Tests.
target_link_libraries(AxEngLib
    PUBLIC
        spdlog::spdlog
        glfw
        glm::glm
        sol2::sol2
        imgui::imgui
        dawn::webgpu_dawn
        MINIZIP::minizip
        ${LUA_LIBRARIES}
)

if(WIN32)
    target_link_libraries(AxEngLib PRIVATE bcrypt)
endif()

axeng_configure_target(AxEngLib
    WARNING_LEVEL W4
    CXX_STANDARD_FLAG /std:c++latest
    C_STANDARD_FLAG /std:clatest
    TREAT_WARNINGS_AS_ERRORS
    BIGOBJ_IN_DEBUG
)
