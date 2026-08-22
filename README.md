# Heat Signature Mod Loader

A native code mod loader for [Heat Signature](https://www.heatsig.com/) (GameMaker Studio 1.4 / YYC). Mods are C++ DLLs that subscribe to GameMaker script hooks, draw ImGui overlays, and read/write GML variables through a stable C ABI.

## Installation

1. Grab the latest release.
2. Drop `dxgi.dll` and `ModLoader.dll` into your Heat Signature install folder (next to `Heat_Signature.exe`).
3. Create a `mods/` subfolder there and copy any mod DLLs into it.
4. Launch the game.

To uninstall, delete `dxgi.dll` and `ModLoader.dll`.

## Usage

Mods installed into `./mods/` will automatically be loaded, and will automatically generate configuration files if required.

The mod loader will generate a configuration file on first run, which contains the following options
	- `show_console`: Shows a console window with debug messages when the game is running. The log can always be found in `./ModLoader.log`
	- `imgui_toggle_key`: The keyboard key to press to globally show and hide all UI overlays, including the main menu bar. Default is F7
	- `imgui_visible_default`: Should mod drawn UI elements be visible by default (can always be toggled with the above key)

## Writing a mod

Mods are DLLs that export `ModInit(const HS_ModApi* api)` and `ModApiVersion()`. See `ModLoader/ModInterface.h` for the full ABI, and the bundled mods under `./mods/` for working examples.

Available hooks are listed in `docs/ScriptFunctions.txt`; subscribe to any of them by name via `api->SubscribeHook` / `SubscribeHookPost`.
Built mods go in `./mods/` as `./mods/MyMod.dll`. If they use config, it will be stored in `./mods/MyMod.json`

## Building

Releases are built on Windows with MSVC. Linux is supported for local development only.

### Windows (MSVC)

1. Clone the repo from [Github](http://github.com/piepieonline/HeatSignatureModLoader)
2. Copy `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and fill in the variables in angle brackets.
3. `vcpkg install`
4. `cmake --preset x86-debug` (note that Heat Signature is a x86 game)
5. `cmake --build --preset local-debug`
6. Launch the game. By default the mod loader will not open a console, but it will create a configuration file alongside the dll where this can be changed.

### Linux (cross-compile to Windows)

Builds the same 32-bit Windows DLLs with clang-cl and lld against an MSVC CRT / Windows SDK
unpacked by [xwin](https://github.com/Jake-Shadle/xwin). One-time setup:

1. Install clang and lld (`sudo apt install clang-18 lld-18 llvm-18`) plus `ninja`.
2. Fetch the SDK: `xwin --accept-license --arch x86 --variant desktop splat --include-debug-libs --output <sdk dir>`
   (`--include-debug-libs` is required for the Debug configuration to link).
3. Clone and bootstrap [vcpkg](https://github.com/microsoft/vcpkg).
4. Copy `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and fill in the `linux-cross`
   preset: `VCPKG_ROOT`, `XWIN_SDK_DIR`, a `PATH` entry for `ninja`, and `HEATSIG_DIR`.

Then `cmake --preset linux-cross` and `cmake --build --preset linux-release` (or `linux-debug`).

In VS Code, `Ctrl+Shift+B` runs the Debug build for the current platform; `Build (Release)` and
`Configure` are available from the task list. The tasks call the presets above, so
`CMakeUserPresets.json` has to exist first.

Two differences from the MSVC build:

- **The ScreenRecorder mod is not built.** It needs ATL and the Media Foundation stack, which the
  cross SDK does not ship. `HS_BUILD_SCREENRECORDER` gates it; its vcpkg dependencies live behind
  the `screen-recorder` manifest feature, which the Linux preset turns off.
- **`__try` / `__except` does not catch faults raised in the same stack frame.** Under `/EHsc`
  clang only tracks exception state across calls, where MSVC covers the whole `__try` region, so
  guarded reads of game memory (`ModLoader.cpp`, `DebugMod.cpp`) crash instead of being caught.
  Exceptions raised inside a *called* function are caught normally. Verify anything that depends
  on SEH with an MSVC build.
