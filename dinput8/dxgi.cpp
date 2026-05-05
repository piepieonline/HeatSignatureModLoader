#include <windows.h>
#include <dxgi.h>
#include <cstdio>

static HMODULE g_real = nullptr;

// =========================
// Load real system dxgi.dll
// =========================
static HMODULE LoadRealDxgi()
{
    if (g_real) return g_real;

    char sys[MAX_PATH] = { 0 };
    if (!GetSystemDirectoryA(sys, MAX_PATH))
        return nullptr;

    strcat_s(sys, "\\dxgi.dll");
    g_real = LoadLibraryA(sys);
    return g_real;
}

static FARPROC GetRealProc(const char* name)
{
    HMODULE mod = LoadRealDxgi();
    return mod ? GetProcAddress(mod, name) : nullptr;
}

// =========================
// ScriptExtender bootstrap
// =========================
static DWORD WINAPI InitThread(LPVOID)
{
    while (!GetModuleHandleA("steam_api.dll"))
        Sleep(100);

    LoadLibraryA(".\\ModLoader.dll");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        LoadRealDxgi();
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        if (g_real)
        {
            FreeLibrary(g_real);
            g_real = nullptr;
        }
        break;
    }
    return TRUE;
}

// =========================
// DXGI proxy trampolines
// =========================
//
// Earlier this file used a FORWARD(name) macro that exported each function as
// `void* name() { return GetRealProc(#name); }` -- it returned the real
// function pointer instead of *calling* the real function. That broke every
// DXGI export: callers got a function-pointer value reinterpreted as HRESULT,
// and every out-pointer argument (REFIID, void**, ...) was left untouched.
// Hitting CreateDXGIFactory1 from DXCam-CPP then crashed in the next
// dereference of the uninitialised IDXGIFactory1*.
//
// Each trampoline below caches the real function pointer in a function-local
// static and forwards arguments verbatim. Signatures match the SDK headers.
//
// The /EXPORT linker pragmas re-export each function under its undecorated
// name so the DLL's export table matches the system dxgi.dll (on x86 the
// __stdcall calling convention would otherwise produce `_Name@N`).

extern "C"
HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory)
{
    using Fn = HRESULT(WINAPI*)(REFIID, void**);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("CreateDXGIFactory"));
    if (!real) { if (ppFactory) *ppFactory = nullptr; return E_FAIL; }
    return real(riid, ppFactory);
}

extern "C"
HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory)
{
    using Fn = HRESULT(WINAPI*)(REFIID, void**);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("CreateDXGIFactory1"));
    if (!real) { if (ppFactory) *ppFactory = nullptr; return E_FAIL; }
    return real(riid, ppFactory);
}

extern "C"
HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory)
{
    using Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("CreateDXGIFactory2"));
    if (!real) { if (ppFactory) *ppFactory = nullptr; return E_FAIL; }
    return real(Flags, riid, ppFactory);
}

extern "C"
HRESULT WINAPI DXGIGetDebugInterface(REFIID riid, void** pDebug)
{
    using Fn = HRESULT(WINAPI*)(REFIID, void**);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("DXGIGetDebugInterface"));
    if (!real) { if (pDebug) *pDebug = nullptr; return E_FAIL; }
    return real(riid, pDebug);
}

extern "C"
HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug)
{
    using Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("DXGIGetDebugInterface1"));
    if (!real) { if (pDebug) *pDebug = nullptr; return E_FAIL; }
    return real(Flags, riid, pDebug);
}

extern "C"
HRESULT WINAPI DXGIDeclareAdapterRemovalSupport()
{
    using Fn = HRESULT(WINAPI*)();
    static Fn real = reinterpret_cast<Fn>(GetRealProc("DXGIDeclareAdapterRemovalSupport"));
    if (!real) return E_FAIL;
    return real();
}

// Strip x86 __stdcall name decoration so the export table publishes the names
// callers expect (CreateDXGIFactory, not _CreateDXGIFactory@8).
#pragma comment(linker, "/EXPORT:CreateDXGIFactory=_CreateDXGIFactory@8")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory1=_CreateDXGIFactory1@8")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory2=_CreateDXGIFactory2@12")
#pragma comment(linker, "/EXPORT:DXGIGetDebugInterface=_DXGIGetDebugInterface@8")
#pragma comment(linker, "/EXPORT:DXGIGetDebugInterface1=_DXGIGetDebugInterface1@12")
#pragma comment(linker, "/EXPORT:DXGIDeclareAdapterRemovalSupport=_DXGIDeclareAdapterRemovalSupport@0")
