# fuelflux
fuelflux software

## Debugging and Development

Use CMake + Ninja to build and debug in VS Code. Recommended steps (PowerShell):

```powershell
# create build directory and configure
cmake -S . -B build -G "Ninja"

# build the project (Debug config)
cmake --build build --config Debug

# run the executable
.\build\bin\fuelflux.exe
```

In VS Code:
- Open the Run and Debug view and select "Debug fuelflux (CMake Build)".
- Start debugging (F5). The preLaunchTask will build the project first.

If you use MSVC toolchain instead of Ninja/gcc, change the `-G "Ninja"` to your generator and adjust `miDebuggerPath` in `.vscode/launch.json` to the Visual Studio debugger (or use `cppvsdbg` type).
