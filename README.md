# AxEng

Very WIP test of using webgpu and Dawn.
Currently only Windows is supported.

## Building on Windows

Install xmake, LLVM Clang, Visual Studio C++ build tools, and a Windows SDK.
Set `VCPKG_ROOT` to your vcpkg checkout, then configure and build:

```powershell
xmake f -y -p windows -a x64 -m debug --toolchain=clang
xmake -y
xmake run Tests
```

## Compiling and running applications
The AxEng CLI has a few important switches.

    --in    Path to the project to run or compile

    --out   Path to compile to

    -c      Compile the given project

    -x      Clean the out directory before doing anything (WARNING: WILL CLEAN THE ENTIRE OUT DIRECTORY!)

    -r      Run the project
              Can be used with compilation, in which case it will run the compiled application from the output folder.

    -q      Recompile the compiler before compiling!
              The compiler is just another application, if you've made changes to it you need to recompile it with this option.

    -v      Verbose mode, enables all printing levels

    --allow-io
    --allow-os
    --allow-threads

            Allows the application to use the built-in lua io and os libraries.
            The allow-threads option runs a background thread to delegate tasks.
            The compiler requires --allow-io and --allow-os, this includes when compiling projects that don't need those permissions.
            The editor requires all three.

Editor specific options:

    --project     The project to open
    --compile_to  Where the project will get compiled to and run from

## Generating a VS2026 solution

To generate a sln file to open with VS:

    xmake project -k vsxmake --lsp=clangd

This is useful for if you want to use Visual Studio for debugging.

## Example VSCode Launch JSON

The following will compile and launch the editor, and make it load the tower_mancer demo project:
```json
{
    "name": "AxEdit",
    "type": "cppvsdbg",
    "request": "launch",
    "program": "${workspaceFolder}\\build\\windows\\x64\\release\\AxEng.exe",
    "args": [
    "-cxrvq",
    "--in",
    "${workspaceFolder}\\editor",
    "--out",
    "${workspaceFolder}\\AxEdit",
    "--allow-threads",
    "--allow-io",
    "--allow-os",
    "--project",
    "${workspaceFolder}\\tower_mancer",
    "--compile_to",
    "${workspaceFolder}\\TowerMancer"
    ],
    "cwd": "${workspaceFolder}",
    "console": "integratedTerminal",
    "preLaunchTask": "xmake: build release"
}
```

<img width="1922" height="1112" alt="image" src="https://github.com/user-attachments/assets/232e0a44-ee8a-442f-b99a-a3c72c7c5c1a" />
