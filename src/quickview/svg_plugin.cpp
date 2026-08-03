// SVG 解码插件 —— 用 resvg.exe（进程外栅格化）输出 PNG，WIC 读回 BGRA8
// 实现 Query + Decode 双层接口，作为插件架构的首个真实落地
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>
#include <string>
#include "plugins.h"

using Microsoft::WRL::ComPtr;

// 与核心一致的 RegApi
struct RegApi {
    void (*regDecoder)(const DecodePlugin *);
};

// ── Query：SVG 无固有尺寸，给建议尺寸 ──
static bool svg_query(const wchar_t *path, DecodeInfo *info)
{
    info->format = QLPF_BGRA8;
    info->frames = 1;
    info->vector = true;
    info->suggestW = 1024;
    info->suggestH = 768;
    // 解析 width/height/viewBox
    FILE *f = _wfopen(path, L"rb");
    if (f) {
        char buf[8192];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n > 0) {
            buf[n] = 0;
            int w = 0, h = 0;
            const char *vb = strstr(buf, "viewBox");
            if (vb && sscanf(vb, "viewBox=\"%*f %*f %d %d", &w, &h) == 2 && w > 0 && h > 0) {
                info->suggestW = w; info->suggestH = h;
            } else {
                const char *wd = strstr(buf, "width=");
                const char *ht = strstr(buf, "height=");
                if (wd && sscanf(wd, "width=\"%d", &w) == 1 && w > 0) info->suggestW = w;
                if (ht && sscanf(ht, "height=\"%d", &h) == 1 && h > 0) info->suggestH = h;
            }
        }
    }
    info->suggestW = max(info->suggestW, 16);
    info->suggestH = max(info->suggestH, 16);
    info->rotateW = info->suggestW;
    info->rotateH = info->suggestH;
    info->hasAlpha = true;
    info->alphaMode = 0;
    info->error = QLERR_OK;
    return true;
}

// ── WIC 读 PNG → BGRA8 ──
static bool wicLoadPng(const wchar_t *pngPath, int *outW, int *outH, unsigned char **outPix)
{
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(pngPath, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) return false;

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (w < 1 || h < 1) return false;

    ComPtr<IWICFormatConverter> conv;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr) || !conv) return false;
    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    UINT stride = (w * 4 + 3) & ~3u;
    unsigned char *pix = (unsigned char*)malloc((size_t)stride * h);
    if (!pix) return false;
    hr = conv->CopyPixels(nullptr, stride, stride * h, pix);
    if (FAILED(hr)) { free(pix); return false; }

    *outW = (int)w; *outH = (int)h;
    *outPix = pix;
    return true;
}

// ── Decode：resvg.exe 栅格化 SVG → PNG → WIC 读回 ──
static bool svg_decode(const wchar_t *path, int frame, int targetW, int targetH, ImageBuffer *out)
{
    if (!out) return false;
    out->pixels = nullptr;

    // 找 resvg.exe（exe 旁 resvg/resvg.exe 或同目录）
    wchar_t resvgPath[MAX_PATH] = {};
    {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        wchar_t dir[MAX_PATH];
        wcscpy_s(dir, self);
        PathRemoveFileSpecW(dir);
        wsprintfW(resvgPath, L"%s\\resvg\\resvg.exe", dir);
        if (GetFileAttributesW(resvgPath) == INVALID_FILE_ATTRIBUTES)
            wsprintfW(resvgPath, L"%s\\resvg.exe", dir);
    }

    // 临时输出 PNG
    wchar_t tmpPng[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPng);
    wchar_t tmpFile[MAX_PATH];
    GetTempFileNameW(tmpPng, L"qsvg", 0, tmpFile);
    wcscat_s(tmpFile, L".png");

    // 构造命令行：resvg.exe in.svg out.png [-w W -h H]
    std::wstring cmd = std::wstring(L"\"") + resvgPath + L"\" \"" + path + L"\" \"" + tmpFile + L"\"";
    if (targetW > 0 && targetH > 0) {
        wchar_t dim[64];
        wsprintfW(dim, L" -w %d -h %d", targetW, targetH);
        cmd += dim;
    }

    // 启动 resvg.exe 等待完成
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = cmd;
    // 隐藏窗口（resvg 是控制台程序）
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (ok) {
        WaitForSingleObject(pi.hProcess, 10000);  // 10s 超时
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    if (!ok) return false;

    // WIC 读回 PNG
    int w = 0, h = 0;
    unsigned char *pix = nullptr;
    bool loaded = wicLoadPng(tmpFile, &w, &h, &pix);
    DeleteFileW(tmpFile);
    if (!loaded || !pix) return false;

    out->width = w; out->height = h;
    out->stride = (w * 4 + 3) & ~3u;
    out->format = QLPF_BGRA8;
    out->pixels = pix;
    out->freeFn = [](void *p) { free(p); };
    return true;
}

// ── 注册 ──
static DecodePlugin g_svg = { "svg", "3C3F786D6C", svg_query, svg_decode };  // 签名 <?xm

extern "C" __declspec(dllexport)
void qlens_plugin_entry(RegApi *api)
{
    if (api && api->regDecoder) api->regDecoder(&g_svg);
}
