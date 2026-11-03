vcpkg_download_distfile(ARCHIVE
    URLS "https://www.lua.org/ftp/lua-5.4.8.tar.gz" "https://sources.buildroot.net/lua/lua-5.4.8.tar.gz"
    FILENAME "lua-5.4.8.tar.gz"
    SHA512 875ad1f6df3ba63722b5069564c9d3a4057b4c3564c691061bb49cf6cdf5d2e303f05762bd46797b444aaf992c03021f423df142123eebf86751fd77edaf8060
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

# LuaJIT prints a whole number as "3"; Lua 5.4 prints a float 3.0 as "3.0". The scripts were written against LuaJIT, so
# floats print without the added ".0". The one condition that adds it is turned into one that never holds.
file(READ "${SOURCE_PATH}/src/lobject.c" LOBJECT)
string(REPLACE "if (buff[strspn(buff, \"-0123456789\")] == '\\0') {  /* looks like an int? */"
    "if (0) {  /* LuaJIT style: no '.0' is added to a float that looks like an int */"
    LOBJECT_PATCHED "${LOBJECT}")
if(LOBJECT STREQUAL LOBJECT_PATCHED)
    message(FATAL_ERROR "lobject.c did not contain the expected float formatting code")
endif()
file(WRITE "${SOURCE_PATH}/src/lobject.c" "${LOBJECT_PATCHED}")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/doc/readme.html")
