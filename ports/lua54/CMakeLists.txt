cmake_minimum_required(VERSION 3.20)
project(lua54 C)

set(LUA_SOURCES src/lapi.c src/lcode.c src/lctype.c src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c src/lgc.c src/llex.c
    src/lmem.c src/lobject.c src/lopcodes.c src/lparser.c src/lstate.c src/lstring.c src/ltable.c src/ltm.c src/lundump.c
    src/lvm.c src/lzio.c src/lauxlib.c src/lbaselib.c src/lcorolib.c src/ldblib.c src/liolib.c src/lmathlib.c src/loadlib.c
    src/loslib.c src/lstrlib.c src/ltablib.c src/lutf8lib.c src/linit.c)

add_library(lua54 STATIC ${LUA_SOURCES})
target_include_directories(lua54 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
# 5.3 compatibility keeps math.pow and friends. No dynamic library loading: the platforms this builds for are static.
target_compile_definitions(lua54 PRIVATE LUA_COMPAT_5_3)
if(NOT WIN32)
    target_compile_definitions(lua54 PRIVATE LUA_USE_POSIX)
endif()

install(TARGETS lua54 ARCHIVE DESTINATION lib)
install(FILES src/lua.h src/luaconf.h src/lualib.h src/lauxlib.h src/lua.hpp DESTINATION include/lua54)
