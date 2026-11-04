# WebAssembly build

AxEng builds for the browser with Emscripten (`xmake f -p wasm -a wasm32 --emsdk=<emsdk directory>`). A project that
uses it supplies its own page and link options; AxEng stays project agnostic.

- **Dependencies** come from the same `vcpkg.json` with the `wasm32-emscripten` triplet (`triplets/`, packages into
  `build/vcpkg_installed_wasm`). WebGPU and GLFW are Emscripten ports (`--use-port=emdawnwebgpu`,
  `--use-port=contrib.glfw3`), not vcpkg packages. LuaJIT is replaced by the `lua54` overlay port; Tracy by stub headers
  in `web/tracy_stub`.
- **Flags** (`xmake.lua`): `-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm` everywhere (the packages are built with
  the same), GNU language modes (`EM_ASM`), and `SOL_NO_CHECK_NUMBER_PRECISION`.
- **Platform layer**: Windows-only code is behind `_WIN32`; the browser path uses a canvas surface
  (`EmscriptenSurfaceSourceCanvasHTMLSelector`), always `Fifo` presentation, and keeps the `wgpu::Instance` alive for
  the device's lifetime. `ax::poll_device` (`core/gpu_wait.h`) replaces `Device::Tick()` while waiting for a callback.
- **Frame loop**: `Window::run` awaits `requestAnimationFrame` each iteration (`EM_ASYNC_JS`, needs `-sJSPI`).
  Presentation happens when control returns to the browser, so the loop must yield every iteration.
- **Threads**: WebGPU objects belong to the page's thread; do not call Dawn from a worker. `JobSystem` takes at most
  five of the pre-created workers.
- **Files**: user files live where the page mounts persistent storage (`/userfs`); `get_app_data_directory` knows it.
- **Lua**: `lua_engine.h` includes PUC Lua there; the engine registers `unpack`, `loadstring` and the coroutine and utf8
  libraries for scripts written for LuaJIT.
