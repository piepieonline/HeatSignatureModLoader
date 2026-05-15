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

1. Clone the repo from [Github](http://github.com/piepieonline/HeatSignatureModLoader)
2. Copy `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and fill in the variables in angle brackets.
3. `vcpkg install`
4. `cmake --preset x86-debug` (note that Heat Signature is a x86 game)
5. `cmake --build --preset local-debug`
6. Launch the game. By default the mod loader will not open a console, but it will create a configuration file alongside the dll where this can be changed.
