# Heat Signature Mod Loader

This project is a mod loader for Heat Signature, injected by a dll proxy (dxgi). Various mods also live in this repo, under /mods/.

## Reference and decompilation

Heat Signature runs on GameMaker Studio 1.4.1804, using YYC.

Some decompiled functions have been dumped in `./docs/decomp/*.txt`, use these as a reference when trying to figure out how the engine does something. If you are missing a function that would be useful to reference, stop and ask.

Script functions have the args `(uintptr_t* self, uintptr_t* other, RValue* result, int argc, RValue** argv)`, and this also applies to decompiled dumps in `./docs/decomp/*.txt`, even if the names haven't been updated in the text files.

## Hooks

Hooks are resolved by name at runtime: `gml_Script_*` from the YYC function table, anything else from the engine built-in registry (see `ModLoader/GameFunctions.cpp`). `./docs/ScriptFunctions.txt` lists every script name and offset for reference only; the build does not read it.