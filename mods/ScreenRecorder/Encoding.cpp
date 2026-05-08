#include "Globals.h"

#include <mfreadwrite.h>
#include <codecapi.h>
#include <opencv2/core/mat.hpp>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

std::wstring MakeOutputPath()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t filename[MAX_PATH];
    swprintf_s(filename, L"HeatSignature_%04u-%02u-%02u_%02u-%02u-%02u.mp4",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring path = g_video_output_path;
    if (!path.empty() && path.back() != L'/' && path.back() != L'\\')
        path += L'/';
    return path + filename;
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
    outType->SetGUID(MF_MT_SUBTYPE,     g_video_encoding_format);
    outType->SetUINT32(MF_MT_AVG_BITRATE,    g_video_bit_rate);
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
