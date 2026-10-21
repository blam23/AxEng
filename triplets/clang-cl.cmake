if(NOT AXENG_VCPKG_WINDOWS_TOOLCHAIN)
    set(AXENG_VCPKG_WINDOWS_TOOLCHAIN "${_VCPKG_ROOT_DIR}/scripts/toolchains/windows.cmake" CACHE FILEPATH "")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES AXENG_VCPKG_WINDOWS_TOOLCHAIN)
include("${AXENG_VCPKG_WINDOWS_TOOLCHAIN}")
set(CMAKE_C_COMPILER "$ENV{AXENG_CLANG_CL}")
set(CMAKE_CXX_COMPILER "$ENV{AXENG_CLANG_CL}")

# CMake's Clang resource preprocessing forwards vcpkg's codepage flag to clang-cl,
# which treats it as an input file instead of an RC option.
string(REPLACE "/c65001" "" CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS}")
string(STRIP "${CMAKE_RC_FLAGS}" CMAKE_RC_FLAGS)
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS}" CACHE STRING "" FORCE)
