#include <windows.h>
#include <delayimp.h>
#include <atlbase.h>
#include <cstdarg>
#include <atomic>
#include <thread>
#include <string>
#include <memory>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>

#include <dxcam/dxcam.h>
#include <dxcam/core/Region.h>
#include <opencv2/core/mat.hpp>

#include <imgui.h>

#include "ModInterface.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace
{
    constexpr UINT32 VIDEO_FPS              = 30;
    constexpr UINT64 VIDEO_FRAME_DURATION   = 10'000'000ULL / VIDEO_FPS; // 100 ns units
    constexpr UINT32 VIDEO_BIT_RATE         = 12'000'000;
    const     GUID   VIDEO_ENCODING_FORMAT  = MFVideoFormat_H264;
    const     GUID   VIDEO_INPUT_FORMAT     = MFVideoFormat_RGB32; // BGRA in memory

    SE_LogFn               g_log = nullptr;
    SE_GetTimeScaleFn      g_getTimeScale = nullptr;
    std::atomic<bool>      g_recording{false};
    std::atomic<bool>      g_recording_enabled{false};
    std::thread            g_recordThread;

    void Log(const char* fmt, ...)
    {
        if (!g_log) return;
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        g_log("ScreenRecorder", buf);
    }

    struct FindWindowCtx
    {
        DWORD pid       = 0;
        HWND  bestHwnd  = nullptr;
        LONG  bestArea  = 0;
    };

    BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lparam)
    {
        auto* ctx = reinterpret_cast<FindWindowCtx*>(lparam);

        DWORD wndPid = 0;
        GetWindowThreadProcessId(hwnd, &wndPid);
        if (wndPid != ctx->pid)            return TRUE;
        if (!IsWindowVisible(hwnd))        return TRUE;
        if (GetWindow(hwnd, GW_OWNER))     return TRUE; // skip tool/owned windows

        char cls[64] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        // Skip the mod-loader's allocated console window.
        if (lstrcmpiA(cls, "ConsoleWindowClass") == 0) return TRUE;

        RECT r{};
        if (!GetClientRect(hwnd, &r)) return TRUE;
        const LONG area = (r.right - r.left) * (r.bottom - r.top);
        if (area <= 0) return TRUE;

        if (area > ctx->bestArea)
        {
            ctx->bestArea = area;
            ctx->bestHwnd = hwnd;
        }
        return TRUE;
    }

    HWND FindGameWindow()
    {
        FindWindowCtx ctx;
        ctx.pid = GetCurrentProcessId();
        EnumWindows(&FindGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
        return ctx.bestHwnd;
    }

    bool GetGameWindowRegion(DXCam::Region& outRegion, HWND& outHwnd)
    {
        HWND hwnd = FindGameWindow();
        if (!hwnd) return false;

        RECT cr{};
        if (!GetClientRect(hwnd, &cr)) return false;

        POINT tl{cr.left, cr.top};
        POINT br{cr.right, cr.bottom};
        if (!ClientToScreen(hwnd, &tl)) return false;
        if (!ClientToScreen(hwnd, &br)) return false;

        // H264 requires even dimensions; trim 1 px if odd.
        int width  = br.x - tl.x;
        int height = br.y - tl.y;
        if (width  <= 0 || height <= 0) return false;
        width  &= ~1;
        height &= ~1;

        outRegion.left   = tl.x;
        outRegion.top    = tl.y;
        outRegion.right  = tl.x + width;
        outRegion.bottom = tl.y + height;
        outHwnd          = hwnd;
        return true;
    }

    std::wstring MakeOutputPath()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t name[MAX_PATH];
        swprintf_s(name, L".\\HeatSignature_%04u-%02u-%02u_%02u-%02u-%02u.mp4",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return name;
    }

    HRESULT InitSinkWriter(const std::wstring& path,
                           UINT32 width,
                           UINT32 height,
                           CComPtr<IMFSinkWriter>& outWriter,
                           DWORD& outStreamIndex)
    {
        CComPtr<IMFAttributes> attrs;
        HRESULT hr = MFCreateAttributes(&attrs, 1);
        if (FAILED(hr)) return hr;
        attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);

        CComPtr<IMFSinkWriter> writer;
        hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs, &writer);
        if (FAILED(hr)) return hr;

        CComPtr<IMFMediaType> outType;
        if (FAILED(hr = MFCreateMediaType(&outType))) return hr;
        outType->SetGUID(MF_MT_MAJOR_TYPE,  MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE,     VIDEO_ENCODING_FORMAT);
        outType->SetUINT32(MF_MT_AVG_BITRATE,    VIDEO_BIT_RATE);
        outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize (outType, MF_MT_FRAME_SIZE,         width, height);
        MFSetAttributeRatio(outType, MF_MT_FRAME_RATE,         VIDEO_FPS, 1);
        MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

        DWORD streamIndex = 0;
        if (FAILED(hr = writer->AddStream(outType, &streamIndex))) return hr;

        CComPtr<IMFMediaType> inType;
        if (FAILED(hr = MFCreateMediaType(&inType))) return hr;
        inType->SetGUID(MF_MT_MAJOR_TYPE,  MFMediaType_Video);
        inType->SetGUID(MF_MT_SUBTYPE,     VIDEO_INPUT_FORMAT);
        inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        // Positive stride forces top-down orientation; without this MF treats RGB as bottom-up.
        inType->SetUINT32(MF_MT_DEFAULT_STRIDE, width * 4);
        MFSetAttributeSize (inType, MF_MT_FRAME_SIZE,         width, height);
        MFSetAttributeRatio(inType, MF_MT_FRAME_RATE,         VIDEO_FPS, 1);
        MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

        if (FAILED(hr = writer->SetInputMediaType(streamIndex, inType, nullptr))) return hr;
        if (FAILED(hr = writer->BeginWriting())) return hr;

        outWriter      = writer;
        outStreamIndex = streamIndex;
        return S_OK;
    }

    HRESULT WriteFrame(IMFSinkWriter* writer,
                       DWORD streamIndex,
                       const cv::Mat& bgra,
                       LONGLONG timestamp)
    {
        const LONG  width      = bgra.cols;
        const LONG  height     = bgra.rows;
        const DWORD dstStride  = static_cast<DWORD>(width) * 4;
        const DWORD bufferSize = dstStride * static_cast<DWORD>(height);

        CComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = MFCreateMemoryBuffer(bufferSize, &buffer);
        if (FAILED(hr)) return hr;

        BYTE* dst = nullptr;
        if (FAILED(hr = buffer->Lock(&dst, nullptr, nullptr))) return hr;

        if (bgra.isContinuous() && bgra.step == dstStride)
        {
            memcpy(dst, bgra.data, bufferSize);
        }
        else
        {
            for (LONG y = 0; y < height; ++y)
                memcpy(dst + y * dstStride, bgra.ptr<uint8_t>(y), dstStride);
        }

        buffer->Unlock();
        buffer->SetCurrentLength(bufferSize);

        CComPtr<IMFSample> sample;
        if (FAILED(hr = MFCreateSample(&sample))) return hr;
        sample->AddBuffer(buffer);
        sample->SetSampleTime(timestamp);
        sample->SetSampleDuration(VIDEO_FRAME_DURATION);

        return writer->WriteSample(streamIndex, sample);
    }

    // Wrap a void()-style callable in a SEH __try/__except so structured
    // exceptions (access violations, etc.) coming out of D3D11/DXGI/MF don't
    // tear the whole game down. Lives in its own non-C++-unwinding function
    // because /EHsc forbids __try in functions with object-unwinding.
    template <class Fn>
    DWORD SehGuard(Fn&& fn, const char* label)
    {
        __try
        {
            fn();
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            const DWORD code = GetExceptionCode();
            Log("SEH 0x%08lX in %s", static_cast<unsigned long>(code), label);
            return code;
        }
    }

    void RecordingThreadInner()
    {
        Log("step: CoInitializeEx");
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        Log("step: MFStartup");
        HRESULT hrMf = MFStartup(MF_VERSION);
        if (FAILED(hrMf))
        {
            Log("MFStartup failed (hr=0x%08lX)", static_cast<unsigned long>(hrMf));
            if (SUCCEEDED(hrCo)) CoUninitialize();
            g_recording = false;
            return;
        }

        DXCam::Region region{};
        HWND          gameHwnd  = nullptr;
        const bool    haveRegion = GetGameWindowRegion(region, gameHwnd);
        if (haveRegion)
        {
            Log("game window 0x%p region = (%d,%d)-(%d,%d) %dx%d",
                gameHwnd, region.left, region.top, region.right, region.bottom,
                region.right - region.left, region.bottom - region.top);
        }
        else
        {
            Log("game window not found, falling back to full screen");
        }

        Log("step: DXCam::create");
        std::shared_ptr<DXCam::DXCamera> camera;
        DWORD createSeh = SehGuard([&] {
            try
            {
                camera = haveRegion ? DXCam::create(region) : DXCam::create();
            }
            catch (const std::exception& e)
            {
                Log("DXCam::create threw std::exception: %s", e.what());
            }
            catch (...)
            {
                Log("DXCam::create threw unknown C++ exception");
            }
        }, "DXCam::create");

        if (createSeh != 0 || !camera)
        {
            Log("Failed to create DXCamera");
            MFShutdown();
            if (SUCCEEDED(hrCo)) CoUninitialize();
            g_recording = false;
            return;
        }

        UINT32 width  = 0;
        UINT32 height = 0;
        if (haveRegion)
        {
            width  = static_cast<UINT32>(region.right  - region.left);
            height = static_cast<UINT32>(region.bottom - region.top);
        }
        else
        {
            Log("step: get_width/height");
            width  = static_cast<UINT32>(camera->get_width());
            height = static_cast<UINT32>(camera->get_height());
        }
        Log("capture resolution = %ux%u", width, height);

        const std::wstring path = MakeOutputPath();
        Log("step: InitSinkWriter -> %ls", path.c_str());
        CComPtr<IMFSinkWriter> writer;
        DWORD streamIndex = 0;
        HRESULT hr = InitSinkWriter(path, width, height, writer, streamIndex);
        if (FAILED(hr))
        {
            Log("InitSinkWriter failed (hr=0x%08lX)", static_cast<unsigned long>(hr));
            MFShutdown();
            if (SUCCEEDED(hrCo)) CoUninitialize();
            g_recording = false;
            return;
        }

        Log("step: camera->start");
        DWORD startSeh = SehGuard([&] {
            try
            {
                camera->start(VIDEO_FPS, /*video_mode=*/true);
            }
            catch (const std::exception& e)
            {
                Log("camera->start threw: %s", e.what());
            }
            catch (...)
            {
                Log("camera->start threw unknown");
            }
        }, "camera->start");

        if (startSeh != 0)
        {
            writer->Finalize();
            writer.Release();
            MFShutdown();
            if (SUCCEEDED(hrCo)) CoUninitialize();
            g_recording = false;
            return;
        }

        Log("Recording -> %ls (%ux%u @ %u fps)",
            path.c_str(), width, height, VIDEO_FPS);

        LONGLONG timestamp = 0;
        int frameCount     = 0;
        DWORD loopSeh = SehGuard([&] {
            LARGE_INTEGER qpcFreq{}, qpcLast{};
            QueryPerformanceFrequency(&qpcFreq);
            QueryPerformanceCounter(&qpcLast);

            // Game-time accumulator in 100ns units. The capture loop only emits a
            // frame once this has accrued one VIDEO_FRAME_DURATION of *game* time —
            // so pause (speed=0) freezes it and slowmo (speed<1) drains it slower,
            // making the output video play at "real" game speed.
            LONGLONG accumulator = 0;
            const LONGLONG accCap = static_cast<LONGLONG>(VIDEO_FRAME_DURATION) * 2;

            bool loggedSize = false;
            while (g_recording.load(std::memory_order_acquire))
            {
                LARGE_INTEGER qpcNow{};
                QueryPerformanceCounter(&qpcNow);
                const LONGLONG wallDt100ns =
                    ((qpcNow.QuadPart - qpcLast.QuadPart) * 10'000'000LL) / qpcFreq.QuadPart;
                qpcLast = qpcNow;

                const double speed = g_getTimeScale ? g_getTimeScale() : 1.0;
                if (speed > 0.0)
                    accumulator += static_cast<LONGLONG>(static_cast<double>(wallDt100ns) * speed);
                if (accumulator > accCap) accumulator = accCap;

                if (accumulator < static_cast<LONGLONG>(VIDEO_FRAME_DURATION))
                {
                    Sleep(speed > 0.0 ? 1 : 5);
                    continue;
                }

                cv::Mat frame = camera->get_latest_frame();
                if (frame.empty())              { Sleep(1); continue; }
                if (frame.type() != CV_8UC4)    { Sleep(1); continue; }
                if (!loggedSize)
                {
                    Log("first frame = %dx%d (writer expects %ux%u)",
                        frame.cols, frame.rows, width, height);
                    loggedSize = true;
                }
                if (static_cast<UINT32>(frame.cols) != width || static_cast<UINT32>(frame.rows) != height)
                    { Sleep(1); continue; }

                HRESULT whr = WriteFrame(writer, streamIndex, frame, timestamp);
                if (FAILED(whr))
                {
                    Log("WriteFrame failed (hr=0x%08lX)", static_cast<unsigned long>(whr));
                    break;
                }
                timestamp   += VIDEO_FRAME_DURATION;
                accumulator -= static_cast<LONGLONG>(VIDEO_FRAME_DURATION);
                ++frameCount;
            }
        }, "capture loop");
        (void)loopSeh;

        Log("step: camera->stop");
        SehGuard([&] { camera->stop(); }, "camera->stop");
        camera.reset();

        Log("step: writer->Finalize");
        hr = writer->Finalize();
        if (FAILED(hr))
            Log("Finalize failed (hr=0x%08lX)", static_cast<unsigned long>(hr));
        writer.Release();

        MFShutdown();
        if (SUCCEEDED(hrCo)) CoUninitialize();

        Log("Stopped (%d frames, %.2fs)",
            frameCount, frameCount / static_cast<double>(VIDEO_FPS));
    }

    void RecordingThread()
    {
        DWORD seh = SehGuard([] { RecordingThreadInner(); }, "RecordingThread");
        if (seh != 0)
        {
            Log("RecordingThread aborted by SEH 0x%08lX",
                static_cast<unsigned long>(seh));
        }
        g_recording = false;
    }

    void ToggleRecording()
    {
        if (!g_recording.load(std::memory_order_acquire))
        {
            if (g_recordThread.joinable())
                g_recordThread.join();
            g_recording.store(true, std::memory_order_release);
            g_recordThread = std::thread(RecordingThread);
        }
        else
        {
            g_recording.store(false, std::memory_order_release);
        }
    }

    void ToggleRecordingEnabled()
    {
        g_recording_enabled.store(!g_recording_enabled.load(std::memory_order_acquire), std::memory_order_release);
	}

    void InputLoop()
    {
        bool f9Down = false;
        while (true)
        {
            const bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
            if (f9 && !f9Down)
            {
                
            }
            f9Down = f9;

            Sleep(50);
        }
    }
}

namespace
{
    void DrawImGui(void* /*userData*/)
    {
        if (!ImGui::Begin("ScreenRecorder")) { ImGui::End(); return; }

        const bool recording = g_recording.load(std::memory_order_acquire);
        const bool recordingEnabled = g_recording_enabled.load(std::memory_order_acquire);
        ImGui::Text("recording: %s", recording ? "yes" : "no");
        if (g_getTimeScale)
            ImGui::Text("timescale: %.3f", g_getTimeScale());

        if (ImGui::Button(recordingEnabled ? "Disable Recording" : "Enable Recording"))
            ToggleRecordingEnabled();

        ImGui::End();
    }

    void OnAcceptMission(const char* /*hookName*/, void* /*userData*/)
    {
        if (g_recording.load(std::memory_order_acquire)) return;
        if (!g_recording_enabled.load(std::memory_order_acquire)) return;
        Log("AcceptMission -> start recording");
        ToggleRecording();
    }

    void OnCompleteMission(const char* /*hookName*/, void* /*userData*/)
    {
        if (!g_recording.load(std::memory_order_acquire)) return;
        Log("CompleteMission -> stop recording");
        ToggleRecording();
    }

    void OnCancelMission(const char* /*hookName*/, void* /*userData*/)
    {
        if (!g_recording.load(std::memory_order_acquire)) return;
        Log("CancelMission -> stop recording");
        ToggleRecording();
    }
}

extern "C" __declspec(dllexport)
void ModInit(const SE_ModApi* api)
{
    g_log          = api->Log;
    g_getTimeScale = api->GetTimeScale;
    Log("Initialized");

    api->SubscribeHook("gml_Script_AcceptMission",   &OnAcceptMission,   nullptr);
    api->SubscribeHook("gml_Script_CompleteMission", &OnCompleteMission, nullptr);
    api->SubscribeHook("gml_Script_CancelMission", &OnCancelMission, nullptr);

    if (api->GetImGuiAllocators && api->GetImGuiContext && api->RegisterImGuiDraw)
    {
        // Mirror the host's allocators *first* — required because
        // ImGui::SetCurrentContext is no-op-safe with a null context but any
        // subsequent allocation (e.g. during draw) must come from the host heap.
        void* allocFn = nullptr;
        void* freeFn  = nullptr;
        void* userData = nullptr;
        api->GetImGuiAllocators(&allocFn, &freeFn, &userData);
        if (allocFn && freeFn)
        {
            ImGui::SetAllocatorFunctions(
                reinterpret_cast<ImGuiMemAllocFunc>(allocFn),
                reinterpret_cast<ImGuiMemFreeFunc>(freeFn),
                userData);
        }
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->GetImGuiContext()));
        api->RegisterImGuiDraw(&DrawImGui, nullptr);
    }

    std::thread(InputLoop).detach();
}
