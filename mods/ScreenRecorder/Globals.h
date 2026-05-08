#pragma once

#include <windows.h>
#include <atlbase.h>
#include <atomic>
#include <thread>
#include <string>
#include <memory>
#include <cstdarg>

#include <mfapi.h>
#include <mfidl.h>

#include "ModInterface.h"

inline constexpr UINT32 VIDEO_FPS            = 30;
inline constexpr UINT64 VIDEO_FRAME_DURATION = 10'000'000ULL / VIDEO_FPS; // 100 ns units
inline const     GUID   VIDEO_INPUT_FORMAT    = MFVideoFormat_RGB32; // BGRA in memory

extern UINT32       g_video_bit_rate;
extern GUID         g_video_encoding_format;
extern std::wstring g_video_output_path;

extern SE_LogFn            g_log;
extern SE_GetTimeScaleFn   g_getTimeScale;
extern SE_GetDailyStatusFn g_getDailyStatus;
extern SE_GetGameWindowFn  g_getGameWindow;
extern std::atomic<bool>   g_recording;
extern std::atomic<bool>   g_recording_enabled;
extern std::atomic<bool>   g_recording_paused;
extern std::atomic<int>    g_unpause_skip_frames;
extern std::thread         g_recordThread;

void Log(const char* fmt, ...);
void ToggleRecording();
