# Heat Signature Mod Loader

A native code mod loader for [Heat Signature](https://www.heatsig.com/) (GameMaker Studio 1.4 / YYC). Mods are C++ DLLs that subscribe to GameMaker script hooks, draw ImGui overlays, and read/write GML variables through a stable C ABI.

Bundled mods live under `./mods/`:
- **HS_QOL** — quality-of-life tweaks
- **DiscordRichPresence** — Discord rich presence integration
- **ScreenRecorder** — in-game screen recording

## Installation

1. Grab the latest release.
2. Drop `dxgi.dll` and `ModLoader.dll` into your Heat Signature install folder (next to `Heat_Signature.exe`).
3. Create a `mods/` subfolder there and copy any mod DLLs into it.
4. Launch the game.

To uninstall, delete `dxgi.dll` and `ModLoader.dll`.

## Writing a mod

Mods are DLLs that export `ModInit(const HS_ModApi* api)` and `ModApiVersion()`. See `ModLoader/ModInterface.h` for the full ABI, and the bundled mods under `./mods/` for working examples.

Available hooks are listed in `docs/ScriptFunctions.txt`; subscribe to any of them by name via `api->SubscribeHook` / `SubscribeHookPost`.
Built mods go in `./mods/` as `./mods/MyMod.dll`. If they use config, it will be stored in `./mods/MyMod.json`

## Building

1. Clone the repo.
2. Copy `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and fill in the variables in angle brackets.
3. `vcpkg install`
4. `cmake --preset x86-debug` (note that Heat Signature is a x86 game)
5. `cmake --build --preset local-debug`
6. Launch the game. By default the mod loader will not open a console, but it will create a configuration file alongside the dll where this can be changed.

## License

CC BY-NC-SA 4.0
