# The original .vcxproj set <ProjectName>AxEngMain</ProjectName>, which
# controls both the solution-explorer display name and (absent an explicit
# <TargetName>) the output binary name - so the built executable was
# "AxEngMain.exe", not "AxEng.exe". The CMake target mirrors that.
add_executable(AxEngMain
    main.cpp
    argparse/argparse.hpp
    ${CMAKE_SOURCE_DIR}/AxEngLib/resource.h
)

if(WIN32)
    target_sources(AxEngMain PRIVATE ${CMAKE_SOURCE_DIR}/AxEngLib/AxEngLib.rc)
endif()

set_target_properties(AxEngMain PROPERTIES OUTPUT_NAME "AxEngMain")

# Non-build asset referenced by the original project for IDE visibility only.
set(AXENG_MAIN_EXTRA_FILES shaders/composite_shader.wgsl)
set_source_files_properties(${AXENG_MAIN_EXTRA_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(AxEngMain PRIVATE ${AXENG_MAIN_EXTRA_FILES})

target_include_directories(AxEngMain
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(AxEngMain PRIVATE AxEngLib)

axeng_configure_target(AxEngMain
    WARNING_LEVEL W3
    CXX_STANDARD_FLAG /std:c++23
    C_STANDARD_FLAG /std:c17
)
