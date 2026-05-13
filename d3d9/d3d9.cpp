#include <windows.h>
#include <d3d9.h>
#include <cstdio>

static HMODULE g_real = nullptr;

// =========================
// Load real system d3d9.dll
// =========================
static HMODULE LoadRealD3D9()
{
    if (g_real) return g_real;

    char sys[MAX_PATH] = { 0 };
    if (!GetSystemDirectoryA(sys, MAX_PATH))
        return nullptr;

    strcat_s(sys, "\\d3d9.dll");
    g_real = LoadLibraryA(sys);
    return g_real;
}

static FARPROC GetRealProc(const char* name)
{
    HMODULE mod = LoadRealD3D9();
    return mod ? GetProcAddress(mod, name) : nullptr;
}

// =========================
// ModLoader bootstrap
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
        LoadRealD3D9();
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
// D3D9 proxy trampolines
// =========================
//
// Each trampoline caches the real function pointer in a function-local static
// and forwards arguments verbatim. Signatures match the SDK headers.
//
// The /EXPORT linker pragmas re-export each function under its undecorated
// name so the DLL's export table matches the system d3d9.dll (on x86 the
// __stdcall calling convention would otherwise produce `_Name@N`).

extern "C"
IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion)
{
    using Fn = IDirect3D9*(WINAPI*)(UINT);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("Direct3DCreate9"));
    if (!real) return nullptr;
    return real(SDKVersion);
}

extern "C"
HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D)
{
    using Fn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("Direct3DCreate9Ex"));
    if (!real) { if (ppD3D) *ppD3D = nullptr; return E_FAIL; }
    return real(SDKVersion, ppD3D);
}

extern "C"
int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName)
{
    using Fn = int(WINAPI*)(D3DCOLOR, LPCWSTR);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("D3DPERF_BeginEvent"));
    if (!real) return -1;
    return real(col, wszName);
}

extern "C"
int WINAPI D3DPERF_EndEvent(void)
{
    using Fn = int(WINAPI*)(void);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("D3DPERF_EndEvent"));
    if (!real) return -1;
    return real();
}

extern "C"
void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName)
{
    using Fn = void(WINAPI*)(D3DCOLOR, LPCWSTR);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("D3DPERF_SetMarker"));
    if (!real) return;
    real(col, wszName);
}

extern "C"
void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName)
{
    using Fn = void(WINAPI*)(D3DCOLOR, LPCWSTR);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("D3DPERF_SetRegion"));
    if (!real) return;
    real(col, wszName);
}

extern "C"
BOOL WINAPI D3DPERF_QueryRepeatFrame(void)
{
    using Fn = BOOL(WINAPI*)(void);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("D3DPERF_QueryRepeatFrame"));
    if (!real) return FALSE;
    return real();
}

extern "C"
void WINAPI D3DPERF_SetOptions(DWORD dwOptions)
{
    using Fn = void(WINAPI*)(DWORD);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("D3DPERF_SetOptions"));
    if (!real) return;
    real(dwOptions);
}

extern "C"
DWORD WINAPI D3DPERF_GetStatus(void)
{
    using Fn = DWORD(WINAPI*)(void);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("D3DPERF_GetStatus"));
    if (!real) return 0;
    return real();
}

extern "C"
void WINAPI DebugSetMute(void)
{
    using Fn = void(WINAPI*)(void);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("DebugSetMute"));
    if (!real) return;
    real();
}

extern "C"
int WINAPI DebugSetLevel(int Level)
{
    using Fn = int(WINAPI*)(int);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("DebugSetLevel"));
    if (!real) return 0;
    return real(Level);
}

// Strip x86 __stdcall name decoration so the export table publishes the names
// callers expect (Direct3DCreate9, not _Direct3DCreate9@4).
#pragma comment(linker, "/EXPORT:Direct3DCreate9=_Direct3DCreate9@4")
#pragma comment(linker, "/EXPORT:Direct3DCreate9Ex=_Direct3DCreate9Ex@8")
#pragma comment(linker, "/EXPORT:D3DPERF_BeginEvent=_D3DPERF_BeginEvent@8")
#pragma comment(linker, "/EXPORT:D3DPERF_EndEvent=_D3DPERF_EndEvent@0")
#pragma comment(linker, "/EXPORT:D3DPERF_SetMarker=_D3DPERF_SetMarker@8")
#pragma comment(linker, "/EXPORT:D3DPERF_SetRegion=_D3DPERF_SetRegion@8")
#pragma comment(linker, "/EXPORT:D3DPERF_QueryRepeatFrame=_D3DPERF_QueryRepeatFrame@0")
#pragma comment(linker, "/EXPORT:D3DPERF_SetOptions=_D3DPERF_SetOptions@4")
#pragma comment(linker, "/EXPORT:D3DPERF_GetStatus=_D3DPERF_GetStatus@0")
#pragma comment(linker, "/EXPORT:DebugSetMute=_DebugSetMute@0")
#pragma comment(linker, "/EXPORT:DebugSetLevel=_DebugSetLevel@4")
