# AxEng

Very WIP test of using webgpu and Dawn.

<img width="1922" height="1112" alt="image" src="https://github.com/user-attachments/assets/232e0a44-ee8a-442f-b99a-a3c72c7c5c1a" />

## Building

The project builds with CMake and [vcpkg](https://github.com/microsoft/vcpkg) manifest mode (dependencies are declared in `vcpkg.json` and restored automatically). Windows x64 is currently the only supported platform/architecture.

Prerequisites:
- Visual Studio 2022 (with the "Desktop development with C++" workload)
- CMake 3.23+
- vcpkg, with the `VCPKG_INSTALLATION_ROOT` environment variable set (already configured on GitHub-hosted Windows runners)

Configure, build and test from the repository root:

```pwsh
cmake --preset windows
cmake --build --preset windows-release
ctest --preset windows-release
```

The `AxEngMain` executable and `Tests` binary are produced under `build/`. Debug builds are available via the `windows-debug` build/test presets.

The original `.vcxproj`/`.slnx` Visual Studio project files are still present alongside the CMake build during the migration, but the CMake build is now the source of truth used by CI.
