# AGENTS.md

Notes for AI agents working on AxEng. AxEng is a WIP WebGPU (Dawn) + Lua game engine. It is Windows-only and built with xmake, clang and C++23. See [README.md](README.md) for user-facing CLI docs.

## Documentation

DO NOT Update the README for anything except build changes that are relevant to the README examples, add any relevant data to the "docs" folder under either an existing md file or a new one.

## Environment & building

- xmake and LLVM may not be on `PATH` in a fresh shell. Dependencies come from vcpkg in manifest mode via `VCPKG_ROOT`:
  ```powershell
  $env:Path += ";C:\Program Files\xmake;C:\Program Files\LLVM\bin"
  if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = "e:\vcpkg" }
  xmake f -y -p windows -a x64 -m debug --toolchain=clang   # or -m release
  xmake -y
  ```
  [build.ps1](build.ps1) wraps this. Use `-Mode debug|release|both -Test -NoRun`.
- Warnings are errors (`set_warnings("all", "error")`). Fix warnings instead of suppressing them globally. Add per-file flags with `add_files(..., {force = {cxxflags = ...}})` in [xmake.lua](xmake.lua).
- **`#embed`** is used to embed text and binary assets such as Lua libs and WGSL shaders. In C++ it is a clang extension, so the file must be added in `xmake.lua` with `{force = {cxxflags = "-Wno-c23-extensions"}}`. Embedded data is **not** null-terminated, so always pass an explicit size:
  ```cpp
  constexpr unsigned char s_shader[] = {
  #embed "shapes.wgsl"
  };
  source.code = { reinterpret_cast<const char*>(s_shader), sizeof(s_shader) };
  ```
  xmake tracks `#embed` dependencies, so editing the embedded file triggers a rebuild.
- Debug builds define `AX_DEBUG_BUILD`, and also `_DEBUG` through the MDd runtime. C++ code should use `ax::build::is_debug` / `ax::build::mode` from [build_info.h](src/axeng/core/build_info.h). Lua code reads `app.engine_build.debug` / `app.engine_build.mode`, which describe the engine build, not the project's `build.json`.
- Debug builds are **very** slow at runtime, often 10x or worse. Do performance work and stress testing in release.
- Leave the xmake config in the mode the user had it in (usually debug) when you finish.

## Tests

- GoogleTest. Sources are globbed from `src/tests/**.cpp`, so new test files are picked up automatically.
- Run with `xmake run Tests [--gtest_filter=Suite.*]`. You can also run `build\windows\x64\<mode>\Tests.exe` **from its own directory**, because it expects the `lua_tests` / `AxCompiler` folders copied next to it. Wrap it in `cmd /c "... 2>&1"` to capture stderr.
- [log_capture](src/tests/main/log_capture.h) fails a test if spdlog logs an unexpected error. If an error is expected, call `testlog::LogCapture::instance().expect_total_error_count(n)`.
- sol2 prints `[sol2] An exception occurred` for Lua errors that tests trigger on purpose. This is harmless.
- GPU tests (see [test_shape_renderer.cpp](src/tests/core/test_shape_renderer.cpp)) create a headless D3D12 Dawn device and `GTEST_SKIP` if none is available. They render offscreen to 128x128 RGBA8 with 4x MSAA and read pixels back. Copy that fixture for new rendering tests.
- To check that tests actually catch bugs, temporarily break the code (e.g. flip a sign) and confirm they fail.

## Running Lua projects

- `AxEng.exe -cxr --in <project> --out <dir> --allow-io --allow-os`.
  - `-x` **deletes the whole `--out` directory**. Never point it at a user folder. Use something like `%TEMP%\AxSomething`.
  - Without `-x`, compiling into an existing output folder can fail with an IO error creating `assets`.
  - `-r` alone needs an already compiled `manifest.luac`.
- Exit code `1000` means the entry point failed.
- To read the exit code from PowerShell, use `$p = Start-Process ... -PassThru` and access `$p.Handle` before `WaitForExit()`. Otherwise `ExitCode` comes back empty.
- When stopping processes, use `Stop-Process -Id <pid>`, never by name.
- Projects: `editor/` (AxEdit), `tower_mancer/` (demo game), `stress_test/` (perf tests; it refuses to run on a debug engine build), `lua_tests/` (run by the `LuaTests` gtest), `AxCompiler/` (Lua project compiler).
- Every script must be registered in the project's `project.json` under `scripts`. Scripts `require` each other by those names.
- The stress test runner ([runner.lua](stress_test/scripts/runner.lua)) iterates its tests in effectively random order, because it uses `table.sort` on a map. This is pre-existing behaviour.

## Code layout

- `src/axeng/core/`: engine library (`AxEngLib`, static). `src/app/`: the `AxEng` executable. `src/tests/`: the `Tests` executable.
- `src/axeng/core/window.cpp`: Dawn device/surface setup, the sprite pipeline, the render loop, ImGui, and the engine device limits (`maxVertexBuffers=1`, `maxVertexAttributes=4`, `maxBindGroups=2`).
- `src/axeng/core/shapes/`: immediate-mode shape renderer.
  - `shape.h` defines a `std::variant` of Circle, Ellipse, Rectangle, Triangle, Line and Polygon.
  - `shape_renderer.h/.cpp` holds the renderer; the shader is `shapes.wgsl`, embedded via `#embed`.
  - Each `ShapeRenderer::draw(pass, shapes)` call is batched into at most one opaque and one translucent indexed draw, unless the buffer has to be chunked.
  - Shapes must be drawn in the scene/render-event pass, because the UI pass's depth is read-only.
  - The scene pass uses 4x MSAA and `Depth24Plus` with reverse-Z (`GreaterEqual`, cleared to 0). Higher z is closer; at equal z, the later draw wins.
- `src/axeng/core/lua/bindings/`: sol2 bindings, one `lua_*_bindings.cpp/.h` pair per area.
  - Window-dependent bindings (sprites, shapes) are set up in `lua_application_bindings.cpp` only when the app has a window.
  - Shapes are exposed as `app.shapes` (`list()`, `draw(pass, ...)`, `cap`, `stats()`).
- `src/axeng/core/lua/external/`: Lua libraries embedded with `#embed` (`lua_libs.cpp`).
- `src/axeng/external/`: third-party code. Don't restyle it.

## Code style

- Tabs for indentation and braces on their own line.
- Initialise variables with braces, e.g. `const auto& x{ init };` rather than `const auto& x = init;`. The same goes for `auto`, `const auto` and explicitly typed variables.
- Naming:
  - Types: `PascalCase`.
  - Functions and variables: `snake_case`.
  - Aggregate/struct data fields: `camelCase`, e.g. `cornerRadius`, `screenSpace`.
  - Members: `m_name`.
  - Private, static or file-local variables **and constants**: `s_name`, e.g. `s_max_chunk_size`.
  - Public constants: plain `lower_case`, e.g. `ax::build::is_debug`.
  - **Do not use `kName` constants.** Third-party names like `wgpu::kDepthSliceUndefined` are fine.
- Everything lives in namespace `ax`. Helpers go in an anonymous namespace in the `.cpp`, and inside it you must qualify `ax::` types.
- Only comment code that needs clarification.

## Gotchas learned the hard way

- sol2: never store a `const sol::table&` that might refer to a temporary. Store `sol::table` by value, or it crashes.
- Dawn API naming in this version: `TexelCopyTextureInfo`/`TexelCopyBufferInfo`. `MapAsync`/`PopErrorScope` take lambdas with `CallbackMode::WaitAnyOnly`. The instance needs the `TimedWaitAny` feature.
- Teardown order: release the app and window before `glfwTerminate`. `ax::run` in [axeng.cpp](src/axeng/core/axeng.cpp) now calls `app.cleanup()` on the load-failure path. Otherwise `~Window` runs after GLFW is gone and logs `GLFW library is not initialized`.
- Stress test timings (release, 1000 frames): immediate-mode shapes take about 25s and retained shapes about 13s. Debug builds are dramatically slower, which is why the runner refuses to start on them.
