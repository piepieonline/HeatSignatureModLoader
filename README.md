## Installation:
* Clone the repo
* Rename `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and change variables in angle brackets.
* Run `vcpkg install`
* Run `cmake --preset x86-debug`
* Run `cmake --build --preset local-debug`
* Run the game. A terminal window should open and contain logs regarding hook installation.

# Array research
claude --resume 86fb027e-631e-4f47-a278-73e5938868fb

## TODO

Do loud guns QOL prevent glory leaderboards?

migrate engine functions to a modding API to make them simpler to call

## Claude TODO:

  Going-public must-fix

  - No LICENSE file. Add one before publishing.

  Architectural oddness

  - Hook<TReturn, TArgs...> template parameters are unused. Same with the entire function_traits block and
  MinHookManager (commented-out call site). Dead generic machinery — either use it or delete it; the codebase only ever
  instantiates Hook<GMLScript_t> and the params don't gate anything.
  - Lua + LuaBridge dependencies are pulled in via vcpkg and ModLoader.h includes them, but nothing uses them. Drop from
   vcpkg.json and ModLoader.h until needed — they slow first-time builds noticeably.
  - PatternScan / pattern-mask Hook ctor is implemented but never called. Every hook uses raw offsets, so a single game
  patch breaks the entire loader with no fallback. Either wire pattern scanning in for the ones most likely to drift, or
   remove the dead ctor and document the offset-fragility in the README.
  - AllocConsole() unconditionally in ModLoader::ModLoader(). End-users will get a console window every launch with no
  way to disable it from config. Easy win: gate behind m_loaderConfig->Read("show_console", false).

  Code quality / correctness

  - ModLoader::Log is not thread-safe. It opens/closes ModLoader.log per call from any thread (Discord callback,
  recorder, ImGui hook, capture loop, hook trampolines). Output interleaves and writes contend on the file. Wrap with a
  static std::mutex and keep the file handle open for the process lifetime.
  - Per-key disk write on read. ModConfig::Read(key, default) calls Save_Locked() on a miss (ModConfig.cpp:45). First
  run rewrites the JSON N times. Defer with a dirty flag and save once.
  - Read returns a pointer into a per-instance scratch buffer with the documented "valid until next call" lifetime.
  Across the C plugin ABI this is a footgun: any second Read/GetJson on the same handle invalidates a
  previously-returned pointer mid-use, and concurrent reads from different threads race even though the mutex protects
  the data. At minimum, document on SE_ModConfig.Read in ModInterface.h; better, return a struct that owns its string.
  - InputLoop in ScreenRecorder.cpp:37 is dead code — detached thread that polls F9 every 50 ms and does nothing in the
  handler (if (f9 && !f9Down) { }). Remove.
  - DiscordRichPresence callback thread leak. g_running is never cleared, the thread is detached, and there's no
  Discord_Shutdown on detach. Process teardown saves you, but it's worth a DLL_PROCESS_DETACH cleanup.
  - ScreenRecorder default outputPath = "./" writes mp4s into the Steam install dir, which on a default Windows install
  is in Program Files and not user-writable. Default to %USERPROFILE%\Videos\HeatSignature or similar.
  - PlayAsCharacter heap leak: SetString(&ModLoader::nameVal, "Hello") allocates a YYString each call with no free; also
   unbounded Var[0..1000] log spam per character select. (Same point as the debug-cruft item above, but worth noting
  it's also leaking.)
  - Hook instances in ModLoader::hooks are new'd and never deleted. Process-lifetime so harmless; one-line note in code.
  - HookBase::moduleBase is found by querying the first VirtualQuery region of Heat_Signature.exe. That returns just the
   size of the first contiguous MEM_COMMIT range with the same protections, not the full module size — PatternScan (if
  it were ever used) would scan only the first section. Use GetModuleInformation / MODULEINFO::SizeOfImage instead.
  - Hook install order: ImGuiHook::Install() calls MH_CreateHook but relies on ModLoader::CreateHooks having already
  called MH_Initialize first. Fine today, fragile if anyone reorders. The commented-out MinHookManager::Initialize() was
   the right idea — finish or delete.

  Smaller polish

  - ModLoader.cpp has a stray #include "ModLoader.h"; (trailing semicolon at dllmain.cpp:6). Compiles, but worth
  cleaning.
  - STANDARD_GML_HOOK is fine but the two near-identical hand-written variants (PlayerIsDailyChallenger, draw_text)
  duplicate the macro body for no reason; collapse to the macro.
  - Many of the SEH guards' fallback messages assume specific failure modes ("deref +0x60 null"); fine, but the
  kStepReasons array indexing in TimeScaleWalk_SEH will read stale step if the first deref fails (step==0 is correct, OK
   actually) — re-check anyway.
  - README only mentions Debug build; add a sentence on local-release for end-user installs and a "drop dxgi.dll +
  ModLoader.dll + mods/ next to Heat_Signature.exe" install blurb.