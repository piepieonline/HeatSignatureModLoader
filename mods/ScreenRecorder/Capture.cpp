#include "Globals.h"

#include <mfreadwrite.h>
#include <dxcam/dxcam.h>
#include <dxcam/core/Region.h>
#include <opencv2/core/mat.hpp>

std::wstring MakeOutputPath();
HRESULT InitSinkWriter(const std::wstring&, UINT32, UINT32, CComPtr<IMFSinkWriter>&, DWORD&);
HRESULT WriteFrame(IMFSinkWriter*, DWORD, const cv::Mat&, LONGLONG);

// Wrap a void()-style callable in a SEH __try/__except so structured
// exceptions (access violations, etc.) coming out of D3D11/DXGI/MF don't
// tear the whole game down. Lives in its own non-C++-unwinding function
// because /EHsc forbids __try in functions with object-unwinding.
template <class Fn>
static DWORD SehGuard(Fn&& fn, const char* label)
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

static bool GetGameWindowRegion(DXCam::Region& outRegion, HWND& outHwnd)
{
    HWND hwnd = g_getGameWindow ? g_getGameWindow() : nullptr;
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

static void RecordingThreadInner()
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
    HWND          gameHwnd   = nullptr;
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
            camera->start(VIDEO_FPS, false);
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
            const LONGLONG wallDt100ns = ((qpcNow.QuadPart - qpcLast.QuadPart) * 10'000'000LL) / qpcFreq.QuadPart;
            qpcLast = qpcNow;

            if (g_recording_paused.load(std::memory_order_acquire))
            {
                Sleep(5);
                qpcLast = qpcNow;   // prevent time jump when resuming
                continue;
            }

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

            // Re-check pause: the flag may have been set after we passed the
            // check above but before we fetched the frame (open-side race).
            // Also drain frames during the post-unpause settling window.
            if (g_recording_paused.load(std::memory_order_acquire))
            {
                accumulator = 0;
                Sleep(5);
                continue;
            }
            {
                int skip = g_unpause_skip_frames.load(std::memory_order_acquire);
                if (skip > 0)
                {
                    g_unpause_skip_frames.store(skip - 1, std::memory_order_release);
                    accumulator = 0;
                    continue;
                }
            }

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

static void RecordingThread()
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
