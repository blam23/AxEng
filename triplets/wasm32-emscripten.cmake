# The vcpkg community wasm32-emscripten triplet, with the exception and setjmp models the engine needs: C++ exceptions
# (sol2, the job system) and Lua's setjmp/longjmp both use WebAssembly exception handling.
set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/toolchains/emscripten.cmake")
set(VCPKG_C_FLAGS "-fwasm-exceptions -sSUPPORT_LONGJMP=wasm -pthread")
set(VCPKG_CXX_FLAGS "-fwasm-exceptions -sSUPPORT_LONGJMP=wasm -pthread")
