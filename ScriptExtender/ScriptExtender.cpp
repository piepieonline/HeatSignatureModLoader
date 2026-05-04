#include "ScriptExtender.h"
#include "ModInterface.h"

#include <Windows.h>
#include <atomic>
#include <iostream>
#include <fstream>
#include <string>

#include "MinHook.h"

std::map<std::string, HookBase*> ScriptExtender::HookMap{};

double ScriptExtender::GetTimeScale()
{
    // Pointer chain identified via Cheat Engine — resolves to the live game
    // timescale double once the time-manager instance exists. Walking it is
    // safe to call from any thread (read-only, no engine code runs).
    //
    // Heat_Signature.exe is 32-bit, so each indirection is a 4-byte pointer.
    // Chain (bottom-to-top in CE):
    //   p = *(u32*)(module + 0x0453D610)
    //   p = *(u32*)(p + 0x60)
    //   p = *(u32*)(p + 0x10)
    //   p = *(u32*)(p + 0x3C4)
    //   value = *(double*)(p + 0x1B0)

    // Called every recorder loop iteration — log only on state transitions
    // (last failure reason vs. current) to avoid spamming the console.
    static const char* s_lastFailure = "init"; // != nullptr -> next success logs recovery
    auto fail = [](const char* reason) -> double {
        if (s_lastFailure == nullptr || strcmp(s_lastFailure, reason) != 0)
        {
            ScriptExtender::Log("[TimeScale] returning 1.0 (%s)", reason);
            s_lastFailure = reason;
        }
        return 1.0;
    };

    if (HookBase::moduleBase == 0)
        return fail("module base not found");

    constexpr uintptr_t kBaseRVA      = 0x0453D610;
    constexpr uint32_t  kDerefOffsets[] = { 0x60, 0x10, 0x3C4 };
    constexpr uint32_t  kFinalOffset  = 0x1B0;

    double result = 1.0;
    const char* failReason = nullptr;
    __try
    {
        uint32_t ptr = *reinterpret_cast<uint32_t*>(HookBase::moduleBase + kBaseRVA);
        if (!ptr) { failReason = "base pointer null"; }
        else
        {
            int step = 0;
            for (uint32_t off : kDerefOffsets)
            {
                ptr = *reinterpret_cast<uint32_t*>(ptr + off);
                if (!ptr)
                {
                    static const char* kStepReasons[] = {
                        "deref +0x60 null", "deref +0x10 null", "deref +0x3C4 null"
                    };
                    failReason = kStepReasons[step];
                    break;
                }
                ++step;
            }
            if (!failReason)
                result = *reinterpret_cast<double*>(ptr + kFinalOffset);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        failReason = "access violation walking pointer chain";
    }

    if (failReason)
        return fail(failReason);

    if (s_lastFailure != nullptr)
    {
        ScriptExtender::Log("[TimeScale] recovered (was: %s) -> %f", s_lastFailure, result);
        s_lastFailure = nullptr;
    }
    return result;
}

using MissionAccept_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, int a5);
using MissionComplete_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, int a5);
using GenerateMissions_t = uintptr_t* (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);
using SetTimeScale_t = uintptr_t* (__cdecl*)(int self, int other, uintptr_t* result, int argc, uintptr_t** argv);
using PauseMission_t = uintptr_t* (__cdecl*)(uintptr_t* a1, int a2, uintptr_t* a3);
using PauseFor_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, uintptr_t** a5);
using SetSlowMotionEffectStrength_t = uintptr_t* (__cdecl*)(int a1, int a2, uintptr_t* a3, int a4, uintptr_t* a5);

ScriptExtender& ScriptExtender::Instance()
{
    static ScriptExtender instance;
    return instance;
}

ScriptExtender::ScriptExtender()
{
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);

    std::ofstream logFile("ScriptExtender.log", std::ios::trunc);
    logFile.close();

    Log("Up");

    CreateHooks();
    LoadMods();
}

void ScriptExtender::Log(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::cout << buffer << std::endl;

    std::ofstream logFile("ScriptExtender.log", std::ios::app);
    if (logFile.is_open())
        logFile << buffer << std::endl;
}

void ScriptExtender::SubscribeHook(const char* hookName, SE_HookCallback callback, void* userData)
{
    auto it = HookMap.find(hookName);
    if (it == HookMap.end())
    {
        Log("[Hook] Subscribe failed: unknown hook '%s'", hookName);
        return;
    }
    it->second->subscribers.emplace_back(callback, userData);
    Log("[Hook] subscriber registered for %s", hookName);
}

void ScriptExtender::LoadMods()
{
    CreateDirectoryA(".\\mods", nullptr);

    static const SE_ModApi modApi = {
        +[](const char* msg) { ScriptExtender::Log(msg); },
        +[](const char* hookName, SE_HookCallback cb, void* userData) {
            ScriptExtender::SubscribeHook(hookName, cb, userData);
        },
        &ScriptExtender::GetTimeScale
    };

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(".\\mods\\*.dll", &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("[Mods] No mods found in .\\mods\\");
        return;
    }

    do {
        std::string path = std::string(".\\mods\\") + fd.cFileName;
        HMODULE hMod = LoadLibraryA(path.c_str());
        if (!hMod)
        {
            Log("[Mods] Failed to load %s (error %lu)", fd.cFileName, GetLastError());
            continue;
        }

        auto modInit = reinterpret_cast<SE_ModInitFn>(GetProcAddress(hMod, "ModInit"));
        if (!modInit)
        {
            Log("[Mods] %s has no ModInit export, skipping", fd.cFileName);
            continue;
        }

        modInit(&modApi);
        Log("[Mods] Loaded %s", fd.cFileName);

    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

void ScriptExtender::CreateHooks()
{
    if (MH_Initialize() != MH_OK)
    {
        Log("MH_Initialize failed");
        return;
    }

    for (auto hook : hooks)
    {
        hook->CreateHook();
        HookMap[hook->hookName] = hook;
    }

    Log("Hooks installed");
}


/* 
// Extracted setTimeScale from game:
_DWORD *__usercall setTimeScale@<eax>(double a1@<st0>, int a2, int a3, _DWORD *a4, int a5, int a6)
{
  __int64 *v6; // esi
  __int64 *v7; // ebx
  double v8; // xmm0_8
  _DWORD *v9; // ebx
  _DWORD *v10; // eax
  int v11; // eax
  int v12; // eax
  int v13; // eax
  int v14; // ecx
  int v15; // edi
  int v16; // edi
  int v17; // eax
  int v18; // ecx
  int v19; // edi
  int v20; // edi
  int v21; // eax
  _DWORD *v22; // eax
  int v23; // eax
  int v24; // edi
  int v25; // edi
  double v26; // xmm0_8
  double v27; // xmm0_8
  double v29; // [esp+28h] [ebp-10Ch]
  __int64 v30; // [esp+48h] [ebp-ECh] BYREF
  int v31; // [esp+54h] [ebp-E0h]
  double v32; // [esp+58h] [ebp-DCh] BYREF
  int v33; // [esp+64h] [ebp-D0h]
  _DWORD v34[3]; // [esp+6Ch] [ebp-C8h] BYREF
  int v35[4]; // [esp+78h] [ebp-BCh] BYREF
  int v36[4]; // [esp+88h] [ebp-ACh] BYREF
  _BYTE v37[8]; // [esp+98h] [ebp-9Ch] BYREF
  void *Block; // [esp+A0h] [ebp-94h]
  _DWORD v39[2]; // [esp+A8h] [ebp-8Ch] BYREF
  _DWORD v40[4]; // [esp+B0h] [ebp-84h] BYREF
  double v41; // [esp+C0h] [ebp-74h] BYREF
  int v42; // [esp+CCh] [ebp-68h]
  _DWORD v43[2]; // [esp+D0h] [ebp-64h] BYREF
  int v44; // [esp+D8h] [ebp-5Ch]
  int v45; // [esp+DCh] [ebp-58h]
  _DWORD v46[3]; // [esp+E0h] [ebp-54h] BYREF
  int v47; // [esp+ECh] [ebp-48h]
  __int64 v48; // [esp+F0h] [ebp-44h] BYREF
  __int64 v49; // [esp+F8h] [ebp-3Ch]
  __int64 v50; // [esp+100h] [ebp-34h] BYREF
  int v51; // [esp+108h] [ebp-2Ch]
  int v52; // [esp+10Ch] [ebp-28h]
  _DWORD v53[2]; // [esp+110h] [ebp-24h] BYREF
  int v54; // [esp+118h] [ebp-1Ch]
  int v55; // [esp+11Ch] [ebp-18h] BYREF
  int v56[5]; // [esp+120h] [ebp-14h] BYREF

  v56[0] = a3;
  v55 = a2;
  v53[1] = "gml_Script_SetTimeScale";
  v54 = 0;
  v53[0] = dword_45356E8;
  dword_45356E8 = (int)v53;
  v52 = 5;
  LODWORD(v50) = 0;
  HIDWORD(v49) = 5;
  LODWORD(v48) = 0;
  v47 = 5;
  v46[0] = 0;
  v45 = 0;
  v43[1] = 0;
  v43[0] = 0;
  v42 = 0;
  v41 = (double)a5;
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + a4[3])) == 0 )
    a1 = sub_1790(a4);
  a4[3] = 0;
  a4[1] = 0;
  *a4 = 0;
  v54 = 1;
  v6 = *(__int64 **)a6;
  if ( *(__int64 **)a6 != &v50 )
  {
    if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v52)) == 0 )
      a1 = sub_1790(&v50);
    sub_1EE0(&v50, v6);
  }
  v54 = 2;
  v7 = *(__int64 **)(a6 + 4);
  if ( v7 != &v48 )
  {
    if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + HIDWORD(v49))) == 0 )
      a1 = sub_1790(&v48);
    sub_1EE0(&v48, v7);
  }
  v54 = 3;
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v47)) == 0 )
    a1 = sub_1790(v46);
  v47 = 0;
  v46[1] = 1065646817;
  v46[0] = 1202590843;
  v54 = 4;
  if ( ((unsigned int)&unk_FFFFFF & v42) != 0 )
  {
    a1 = sub_CBC130(&v41);
    v8 = a1;
  }
  else
  {
    v8 = v41;
  }
  if ( v8 + -2.0 > *(double *)&qword_44DEFB0 )
  {
    v54 = 5;
    sub_1EE0(v40, *(_DWORD **)(a6 + 8));
    v39[0] = v46;
    v39[1] = v40;
    v9 = (_DWORD *)sub_CC4120(v43, 2, v39);
    if ( v9 != v46 )
    {
      if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v47)) == 0 )
        a1 = sub_1790(v46);
      sub_1EE0(v46, v9);
    }
    if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v45)) == 0 )
      a1 = sub_1790(v43);
    v44 = 0;
    v45 = 5;
    v43[0] = 0;
    if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v40[3])) == 0 )
      a1 = sub_1790(v40);
  }
  v54 = 8;
  if ( (int)sub_CA8590(v37, &v55, v56, 42) > 0 )
  {
    do
    {
      v54 = 9;
      v12 = *(_DWORD *)(v55 + 4);
      if ( v12 )
        v13 = v12 + 41680;
      else
        v13 = (*(int (__thiscall **)(int, int))(*(_DWORD *)v55 + 4))(v55, 2605);
      if ( (int)sub_CC62D0(&v48, v13, qword_44DEFB0) > 0 )
      {
        v54 = 10;
        sub_CAB350(v36, aTimescaleChang);
        sub_CAB350(v35, aTimescaleprior);
        v34[0] = v36;
        v34[1] = &v48;
        v34[2] = v35;
        sub_1DD2E0(v55, v56[0], v43, 3, (int)v34);
        if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v45)) == 0 )
          a1 = sub_1790(v43);
        v44 = 0;
        v45 = 5;
        v43[0] = 0;
        v54 = 11;
        v14 = v55;
        v15 = *(_DWORD *)(v55 + 4);
        if ( v15 )
          v16 = v15 + 41696;
        else
          v16 = (*(int (__thiscall **)(int, int))(*(_DWORD *)v55 + 4))(v55, 2606);
        if ( &v50 != (__int64 *)v16 )
        {
          if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + *(_DWORD *)(v16 + 12))) == 0 )
            a1 = sub_1790(v16);
          *(_DWORD *)(v16 + 12) = v52;
          *(_DWORD *)(v16 + 8) = v51;
          switch ( (unsigned int)&unk_FFFFFF & v52 )
          {
            case 0u:
            case 0xAu:
            case 0xDu:
              *(_QWORD *)v16 = v50;
              break;
            case 1u:
              v17 = v50;
              if ( (_DWORD)v50 )
                ++*(_DWORD *)(v50 + 4);
              goto LABEL_49;
            case 2u:
              v22 = (_DWORD *)v50;
              *(_DWORD *)v16 = v50;
              if ( v22 )
              {
                ++*v22;
                if ( !v22[2] )
                  v22[2] = v16;
              }
              break;
            case 3u:
            case 7u:
            case 0xEu:
              v17 = v50;
LABEL_49:
              *(_DWORD *)v16 = v17;
              break;
            case 6u:
              *(_DWORD *)v16 = v50;
              if ( (_DWORD)v50 )
              {
                v23 = sub_C99FD0(v14);
                nullsub_4(v23, v50);
              }
              break;
            default:
              break;
          }
        }
        v54 = 12;
        v18 = v55;
        v19 = *(_DWORD *)(v55 + 4);
        if ( v19 )
          v20 = v19 + 41680;
        else
          v20 = (*(int (__thiscall **)(int, int))(*(_DWORD *)v55 + 4))(v55, 2605);
        if ( &v48 != (__int64 *)v20 )
        {
          if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + *(_DWORD *)(v20 + 12))) == 0 )
            a1 = sub_1790(v20);
          *(_QWORD *)(v20 + 8) = v49;
          switch ( (unsigned int)&unk_FFFFFF & HIDWORD(v49) )
          {
            case 0u:
            case 0xAu:
            case 0xDu:
              *(_QWORD *)v20 = v48;
              break;
            case 1u:
              v21 = v48;
              if ( (_DWORD)v48 )
                ++*(_DWORD *)(v48 + 4);
              goto LABEL_66;
            case 2u:
              v10 = (_DWORD *)v48;
              *(_DWORD *)v20 = v48;
              if ( v10 )
              {
                ++*v10;
                if ( !v10[2] )
                  v10[2] = v20;
              }
              break;
            case 3u:
            case 7u:
            case 0xEu:
              v21 = v48;
LABEL_66:
              *(_DWORD *)v20 = v21;
              break;
            case 6u:
              *(_DWORD *)v20 = v48;
              if ( (_DWORD)v48 )
              {
                v11 = sub_C99FD0(v18);
                nullsub_4(v11, v48);
              }
              break;
            default:
              break;
          }
        }
        v54 = 13;
        v24 = *(_DWORD *)(v55 + 4);
        if ( v24 )
          v25 = v24 + 41712;
        else
          v25 = (*(int (__thiscall **)(int, int))(*(_DWORD *)v55 + 4))(v55, 2607);
        sub_EE30(&v30, 1, &v50);
        sub_EBD0(&v32, &v30, v46);
        if ( ((unsigned int)&unk_FFFFFF & v33) != 0 )
        {
          a1 = sub_CBC130(&v32);
          v26 = a1;
        }
        else
        {
          v26 = v32;
        }
        sub_CBFDC0(v26);
        v27 = a1;
        if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + *(_DWORD *)(v25 + 12))) == 0 )
        {
          v29 = a1;
          a1 = sub_1790(v25);
          v27 = v29;
        }
        *(_DWORD *)(v25 + 12) = 0;
        *(double *)v25 = v27;
        if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v33)) == 0 )
          a1 = sub_1790(&v32);
        if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v31)) == 0 )
          a1 = sub_1790(&v30);
        if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v35[3])) == 0 )
          a1 = sub_1790(v35);
        if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v36[3])) == 0 )
          a1 = sub_1790(v36);
      }
    }
    while ( (unsigned __int8)sub_C9AC30(v37, &v55) );
  }
  sub_C9AC60(v37);
  if ( Block )
  {
    sub_CB9F20(Block, (int)&v55);
    Block = 0;
  }
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v42)) == 0 )
    sub_1790(&v41);
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v45)) == 0 )
    sub_1790(v43);
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v47)) == 0 )
    sub_1790(v46);
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + HIDWORD(v49))) == 0 )
    sub_1790(&v48);
  if ( ((unsigned int)&unk_FFFFFC & ((unsigned int)&unk_FFFFFF + v52)) == 0 )
    sub_1790(&v50);
  dword_45356E8 = v53[0];
  return a4;
}
*/