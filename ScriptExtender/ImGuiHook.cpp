#include "ImGuiHook.h"
#include "ScriptExtender.h"

#include <windows.h>
#include <d3d9.h>
#include <MinHook.h>

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    using Direct3DCreate9_t   = IDirect3D9*       (WINAPI*)(UINT);
    using Direct3DCreate9Ex_t = HRESULT           (WINAPI*)(UINT, IDirect3D9Ex**);
    using CreateDevice_t      = HRESULT           (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    using EndScene_t          = HRESULT           (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
    using Reset_t             = HRESULT           (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

    Direct3DCreate9_t   o_Direct3DCreate9   = nullptr;
    Direct3DCreate9Ex_t o_Direct3DCreate9Ex = nullptr;
    CreateDevice_t      o_CreateDevice      = nullptr;
    EndScene_t          o_EndScene          = nullptr;
    Reset_t             o_Reset             = nullptr;

    std::once_flag g_d3dHooksInstalled;
    std::once_flag g_deviceHooksInstalled;

    std::atomic<bool> g_backendsInitialized{false};
    HWND   g_hwnd          = nullptr;
    WNDPROC g_originalWndProc = nullptr;

    std::mutex                                          g_drawMu;
    std::vector<std::pair<SE_ImGuiDrawFn, void*>>       g_drawCallbacks;

    // /EHsc forbids __try in functions that have C++ object unwinding, so the
    // SEH wrapper around each mod callback lives in its own helper.
    DWORD InvokeDrawSafe(SE_ImGuiDrawFn fn, void* userData)
    {
        __try { fn(userData); return 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
    }

    void Log(const char* fmt, ...)
    {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        ScriptExtender::Log("ImGuiHook", buf);
    }

    LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (g_backendsInitialized.load(std::memory_order_acquire))
        {
            if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
                return 1;
        }
        return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
    }

    void InitBackends(IDirect3DDevice9* device, HWND hwnd)
    {
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX9_Init(device);

        g_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));

        g_hwnd = hwnd;
        g_backendsInitialized.store(true, std::memory_order_release);
        Log("ImGui backends initialised (HWND=0x%p)", hwnd);
    }

    HRESULT STDMETHODCALLTYPE Hooked_EndScene(IDirect3DDevice9* device)
    {
        if (!g_backendsInitialized.load(std::memory_order_acquire))
        {
            D3DDEVICE_CREATION_PARAMETERS cp{};
            if (SUCCEEDED(device->GetCreationParameters(&cp)) && cp.hFocusWindow)
                InitBackends(device, cp.hFocusWindow);
        }

        if (g_backendsInitialized.load(std::memory_order_acquire))
        {
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            std::vector<std::pair<SE_ImGuiDrawFn, void*>> snapshot;
            {
                std::lock_guard<std::mutex> lk(g_drawMu);
                snapshot = g_drawCallbacks;
            }
            for (auto& cb : snapshot)
            {
                DWORD seh = InvokeDrawSafe(cb.first, cb.second);
                if (seh != 0)
                    Log("draw callback raised SEH 0x%08lX",
                        static_cast<unsigned long>(seh));
            }

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }

        return o_EndScene(device);
    }

    HRESULT STDMETHODCALLTYPE Hooked_Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp)
    {
        const bool inited = g_backendsInitialized.load(std::memory_order_acquire);
        if (inited)
            ImGui_ImplDX9_InvalidateDeviceObjects();

        HRESULT hr = o_Reset(device, pp);

        if (inited && SUCCEEDED(hr))
            ImGui_ImplDX9_CreateDeviceObjects();

        return hr;
    }

    void InstallDeviceHooks(IDirect3DDevice9* device)
    {
        std::call_once(g_deviceHooksInstalled, [&]() {
            void** vtable = *reinterpret_cast<void***>(device);

            void* resetTarget    = vtable[16];
            void* endSceneTarget = vtable[42];

            if (MH_CreateHook(resetTarget, &Hooked_Reset,
                              reinterpret_cast<void**>(&o_Reset)) != MH_OK)
            {
                Log("MH_CreateHook(Reset) failed");
                return;
            }
            if (MH_CreateHook(endSceneTarget, &Hooked_EndScene,
                              reinterpret_cast<void**>(&o_EndScene)) != MH_OK)
            {
                Log("MH_CreateHook(EndScene) failed");
                return;
            }
            if (MH_EnableHook(resetTarget) != MH_OK ||
                MH_EnableHook(endSceneTarget) != MH_OK)
            {
                Log("MH_EnableHook(device) failed");
                return;
            }
            Log("device vtable hooks installed (Reset=0x%p, EndScene=0x%p)",
                resetTarget, endSceneTarget);
        });
    }

    HRESULT STDMETHODCALLTYPE Hooked_CreateDevice(IDirect3D9* self,
                                                   UINT adapter,
                                                   D3DDEVTYPE deviceType,
                                                   HWND focusWindow,
                                                   DWORD behaviorFlags,
                                                   D3DPRESENT_PARAMETERS* pp,
                                                   IDirect3DDevice9** outDevice)
    {
        HRESULT hr = o_CreateDevice(self, adapter, deviceType, focusWindow,
                                    behaviorFlags, pp, outDevice);
        if (SUCCEEDED(hr) && outDevice && *outDevice)
            InstallDeviceHooks(*outDevice);
        return hr;
    }

    void InstallD3DHooks(IDirect3D9* d3d)
    {
        std::call_once(g_d3dHooksInstalled, [&]() {
            void** vtable = *reinterpret_cast<void***>(d3d);
            void* createDeviceTarget = vtable[16];

            if (MH_CreateHook(createDeviceTarget, &Hooked_CreateDevice,
                              reinterpret_cast<void**>(&o_CreateDevice)) != MH_OK)
            {
                Log("MH_CreateHook(CreateDevice) failed");
                return;
            }
            if (MH_EnableHook(createDeviceTarget) != MH_OK)
            {
                Log("MH_EnableHook(CreateDevice) failed");
                return;
            }
            Log("IDirect3D9::CreateDevice hooked at 0x%p", createDeviceTarget);
        });
    }

    IDirect3D9* WINAPI Hooked_Direct3DCreate9(UINT sdkVersion)
    {
        IDirect3D9* d3d = o_Direct3DCreate9(sdkVersion);
        if (d3d) InstallD3DHooks(d3d);
        return d3d;
    }

    HRESULT WINAPI Hooked_Direct3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** outD3D)
    {
        HRESULT hr = o_Direct3DCreate9Ex(sdkVersion, outD3D);
        if (SUCCEEDED(hr) && outD3D && *outD3D)
            InstallD3DHooks(*outD3D);
        return hr;
    }

    // The dxgi-proxy bootstrap waits for steam_api.dll before loading
    // ScriptExtender, by which time the game has typically already called
    // Direct3DCreate9 and created its device. Hooking Direct3DCreate9 alone is
    // therefore too late. The fix: create a throwaway IDirect3D9 / device
    // ourselves and hook the *function pointers* in their vtables. Those
    // pointers are class-wide (every IDirect3D9 instance shares the same
    // vtable), and MinHook patches the function bytes in memory — so the
    // already-created game device starts routing through our hooks on its
    // next EndScene.
    void ProbeAndHookExistingDevice()
    {
        if (!o_Direct3DCreate9) return;

        // Hidden helper window; D3D9 requires a real HWND to create a device.
        HWND hwnd = CreateWindowExA(0, "STATIC", "se_imgui_probe",
                                    0, 0, 0, 1, 1, nullptr, nullptr,
                                    GetModuleHandleA(nullptr), nullptr);
        if (!hwnd)
        {
            Log("probe CreateWindow failed (err=%lu)", GetLastError());
            return;
        }

        IDirect3D9* d3d = o_Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d)
        {
            Log("probe Direct3DCreate9 returned null");
            DestroyWindow(hwnd);
            return;
        }

        // Hook IDirect3D9::CreateDevice on this instance — same fn pointer as
        // the game's instance because vtables are shared across the class.
        InstallD3DHooks(d3d);

        D3DPRESENT_PARAMETERS pp = {};
        pp.Windowed         = TRUE;
        pp.SwapEffect       = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.hDeviceWindow    = hwnd;

        IDirect3DDevice9* dev = nullptr;
        HRESULT hr = d3d->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
            &pp, &dev);

        // The CreateDevice call above goes through Hooked_CreateDevice, which
        // also calls InstallDeviceHooks — so just creating the probe device is
        // enough to detour EndScene/Reset. Belt-and-suspenders InstallDeviceHooks
        // call below in case the hook chain is bypassed for any reason; it's
        // guarded by a once_flag.
        if (SUCCEEDED(hr) && dev)
        {
            InstallDeviceHooks(dev);
            dev->Release();
        }
        else
        {
            Log("probe CreateDevice failed (hr=0x%08lX)",
                static_cast<unsigned long>(hr));
        }
        d3d->Release();
        DestroyWindow(hwnd);
    }
}

namespace ImGuiHook
{
    bool Install()
    {
        // Create the ImGui context up front so mods can SetCurrentContext from
        // their ModInit. Backends (DX9 + Win32) are initialised lazily once we
        // see the first EndScene and have a real device + HWND.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        HMODULE d3d9 = LoadLibraryA("d3d9.dll");
        if (!d3d9) { Log("LoadLibrary(d3d9.dll) failed"); return false; }

        auto rawCreate   = GetProcAddress(d3d9, "Direct3DCreate9");
        auto rawCreateEx = GetProcAddress(d3d9, "Direct3DCreate9Ex");

        if (!rawCreate)
        {
            Log("Direct3DCreate9 not exported by d3d9.dll");
            return false;
        }

        if (MH_CreateHook(reinterpret_cast<void*>(rawCreate),
                          &Hooked_Direct3DCreate9,
                          reinterpret_cast<void**>(&o_Direct3DCreate9)) != MH_OK ||
            MH_EnableHook(reinterpret_cast<void*>(rawCreate)) != MH_OK)
        {
            Log("Failed to install Direct3DCreate9 hook");
            return false;
        }
        Log("Direct3DCreate9 hooked at 0x%p", rawCreate);

        if (rawCreateEx)
        {
            if (MH_CreateHook(reinterpret_cast<void*>(rawCreateEx),
                              &Hooked_Direct3DCreate9Ex,
                              reinterpret_cast<void**>(&o_Direct3DCreate9Ex)) != MH_OK ||
                MH_EnableHook(reinterpret_cast<void*>(rawCreateEx)) != MH_OK)
            {
                Log("Failed to install Direct3DCreate9Ex hook (continuing)");
            }
            else
            {
                Log("Direct3DCreate9Ex hooked at 0x%p", rawCreateEx);
            }
        }

        // Catch the case where the game has already created its device before
        // our DLL loaded. Hook the shared vtable function pointers via a
        // throwaway probe device.
        ProbeAndHookExistingDevice();
        return true;
    }

    void RegisterDraw(SE_ImGuiDrawFn callback, void* userData)
    {
        if (!callback) return;
        std::lock_guard<std::mutex> lk(g_drawMu);
        g_drawCallbacks.emplace_back(callback, userData);
    }

    void* GetContext()
    {
        return ImGui::GetCurrentContext();
    }

    void GetAllocators(void** allocFn, void** freeFn, void** userData)
    {
        ImGuiMemAllocFunc a = nullptr;
        ImGuiMemFreeFunc  f = nullptr;
        void* ud            = nullptr;
        ImGui::GetAllocatorFunctions(&a, &f, &ud);
        if (allocFn)  *allocFn  = reinterpret_cast<void*>(a);
        if (freeFn)   *freeFn   = reinterpret_cast<void*>(f);
        if (userData) *userData = ud;
    }
}
