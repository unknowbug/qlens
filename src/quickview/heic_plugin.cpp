// HEIC/HEIF/AVIF 解码插件——动态加载 libheif（不依赖系统 HEIF 扩展）
#include <windows.h>
#include <string>
#include <vector>
#include "plugins.h"

// ── libheif 动态加载 ──
// heif_error 是 16 字节结构体（x64 ABI 用隐藏指针返回——必须精确匹配）
struct heif_error { int code; int subcode; const char *message; };
struct heif_metadata_ids { int *ids; int count; int capacity; };  // heif_list_* 输出（简化——实际是不透明）

typedef void*(*fn_ctx_alloc)(void);
typedef void(*fn_ctx_free)(void*);
typedef heif_error(*fn_ctx_read_file)(void*, const char*, void*);
typedef heif_error(*fn_ctx_primary)(void*, void**);
typedef void(*fn_handle_release)(void*);
typedef int(*fn_handle_w)(void*);
typedef int(*fn_handle_h)(void*);
typedef int(*fn_handle_alpha)(void*);
typedef int(*fn_handle_bits)(void*);
typedef heif_error(*fn_decode)(void*, void**, int, int, void*);
typedef void(*fn_img_release)(void*);
typedef int(*fn_img_w)(void*, int);  // (img, channel)
typedef int(*fn_img_h)(void*, int);
typedef const unsigned char*(*fn_plane)(void*, int, int*);

static HMODULE g_heifLib = nullptr;
static fn_ctx_alloc pfAlloc; static fn_ctx_free pfFree;
static fn_ctx_read_file pfRead; static fn_ctx_primary pfPrimary;
static fn_handle_release pfHRelease; static fn_handle_w pfHW; static fn_handle_h pfHH; static fn_handle_alpha pfHAlpha; static fn_handle_bits pfHBits;
static fn_decode pfDecode; static fn_img_release pfImgRelease;
static fn_img_w pfIW; static fn_img_h pfIH; static fn_plane pfPlane;

// 加载 libheif DLL（搜插件目录 libheif*.dll）
static bool LoadHeif()
{
    if (g_heifLib) return true;
    wchar_t dir[1024];
    GetModuleFileNameW(nullptr, dir, 1024);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
    std::wstring pattern = std::wstring(dir) + L"\\libheif-*.dll";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    std::wstring libPath = std::wstring(dir) + L"\\" + fd.cFileName;
    FindClose(hFind);

    g_heifLib = LoadLibraryW(libPath.c_str());
    if (!g_heifLib) return false;
    pfAlloc = (fn_ctx_alloc)GetProcAddress(g_heifLib, "heif_context_alloc");
    pfFree = (fn_ctx_free)GetProcAddress(g_heifLib, "heif_context_free");
    pfRead = (fn_ctx_read_file)GetProcAddress(g_heifLib, "heif_context_read_from_file");
    pfPrimary = (fn_ctx_primary)GetProcAddress(g_heifLib, "heif_context_get_primary_image_handle");
    pfHRelease = (fn_handle_release)GetProcAddress(g_heifLib, "heif_image_handle_release");
    pfHW = (fn_handle_w)GetProcAddress(g_heifLib, "heif_image_handle_get_width");
    pfHH = (fn_handle_h)GetProcAddress(g_heifLib, "heif_image_handle_get_height");
    pfHAlpha = (fn_handle_alpha)GetProcAddress(g_heifLib, "heif_image_handle_has_alpha_channel");
    pfHBits = (fn_handle_bits)GetProcAddress(g_heifLib, "heif_image_handle_get_luma_bits_per_pixel");
    pfDecode = (fn_decode)GetProcAddress(g_heifLib, "heif_decode_image");
    pfImgRelease = (fn_img_release)GetProcAddress(g_heifLib, "heif_image_release");
    pfIW = (fn_img_w)GetProcAddress(g_heifLib, "heif_image_get_width");
    pfIH = (fn_img_h)GetProcAddress(g_heifLib, "heif_image_get_height");
    pfPlane = (fn_plane)GetProcAddress(g_heifLib, "heif_image_get_plane_readonly");
    if (!pfAlloc || !pfFree || !pfRead || !pfPrimary || !pfHRelease || !pfHW || !pfHH ||
        !pfHAlpha || !pfDecode || !pfImgRelease || !pfIW || !pfIH || !pfPlane) {
        FreeLibrary(g_heifLib); g_heifLib = nullptr; return false;
    }
    return true;
}

// UTF-8 路径转换
static std::string WideToUtf8(const wchar_t *w)
{
    if (!w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n - 1, nullptr, nullptr);
    return s;
}

// float → half（位操作，scRGB 值 0-8 无次正规问题）
static inline unsigned short f2h(float f)
{
    if (f <= 0.0f) return 0;
    unsigned b; memcpy(&b, &f, 4);
    unsigned sign = (b >> 16) & 0x8000u;
    int e = (int)((b >> 23) & 0xFFu) - 127 + 15;
    unsigned m = (b >> 13) & 0x3FFu;
    if (e >= 31) return (unsigned short)(sign | 0x7BFFu);
    if (e <= 0) return (unsigned short)sign;
    return (unsigned short)(sign | ((unsigned)e << 10) | m);
}

// ── query ──
static bool heic_query(const wchar_t *path, DecodeInfo *info)
{
    if (!info || !LoadHeif()) return false;
    void *ctx = pfAlloc();
    if (!ctx) return false;
    std::string utf8 = WideToUtf8(path);
    heif_error er = pfRead(ctx, utf8.c_str(), nullptr);
    if (er.code != 0) { pfFree(ctx); return false; }
    void *handle = nullptr;
    er = pfPrimary(ctx, &handle);
    if (er.code != 0 || !handle) { pfFree(ctx); return false; }
    int w = pfHW(handle), h = pfHH(handle);
    int hasAlpha = pfHAlpha(handle);
    int bits = pfHBits ? pfHBits(handle) : 8;
    info->format = (bits > 8) ? QLPF_RGBA16F : QLPF_BGRA8;  // 高位深 → 16F
    info->frames = 1;
    info->suggestW = w; info->suggestH = h;
    info->rotateW = w; info->rotateH = h;
    info->hasAlpha = hasAlpha != 0;
    info->exifRot = 0;
    info->error = QLERR_OK;
    pfHRelease(handle);
    pfFree(ctx);
    return w > 0 && h > 0;
}

// ── decode ──
static bool heic_decode(const wchar_t *path, int frame, int targetW, int targetH, ImageBuffer *out)
{
    if (!out || !LoadHeif()) return false;
    void *ctx = pfAlloc();
    if (!ctx) return false;
    std::string utf8 = WideToUtf8(path);
    heif_error er = pfRead(ctx, utf8.c_str(), nullptr);
    if (er.code != 0) { pfFree(ctx); return false; }
    void *handle = nullptr;
    er = pfPrimary(ctx, &handle);
    if (er.code != 0 || !handle) { pfFree(ctx); return false; }
    int w = pfHW(handle), h = pfHH(handle);
    int bits = pfHBits ? pfHBits(handle) : 8;
    void *img = nullptr;
    er = pfDecode(handle, &img, 1, (bits > 8) ? 15 /*RRGGBBAA_LE 16bit*/ : 11 /*RGBA 8bit*/, nullptr);
    if (er.code != 0 || !img) { pfHRelease(handle); pfFree(ctx); return false; }
    int stride = 0;
    const unsigned char *plane = pfPlane(img, 10 /*heif_channel_interleaved*/, &stride);
    int pxSize = (bits > 8) ? 8 : 4;
    if (!plane || w < 1 || h < 1 || stride < w * pxSize) { pfImgRelease(img); pfHRelease(handle); pfFree(ctx); return false; }

    unsigned char *pix = new (std::nothrow) unsigned char[(size_t)w * h * (bits > 8 ? 8 : 4)];
    if (!pix) { pfImgRelease(img); pfHRelease(handle); pfFree(ctx); return false; }
    if (bits > 8) {
        // 16bit LE RRGGBBAA → half（scRGB 线性假设；HDR transfer 后续补）
        unsigned short *dh = (unsigned short*)pix;
        for (int y = 0; y < h; ++y) {
            const unsigned short *src = (const unsigned short*)(plane + (size_t)y * stride);
            unsigned short *dst = dh + (size_t)y * w * 4;
            for (int x = 0; x < w; ++x) {
                dst[x*4+0] = f2h(src[x*4+0] / 65535.0f);  // R
                dst[x*4+1] = f2h(src[x*4+1] / 65535.0f);  // G
                dst[x*4+2] = f2h(src[x*4+2] / 65535.0f);  // B
                dst[x*4+3] = f2h(src[x*4+3] / 65535.0f);  // A
            }
        }
    } else {
        // RGBA → BGRA8 紧凑
        for (int y = 0; y < h; ++y) {
            const unsigned char *src = plane + (size_t)y * stride;
            unsigned char *dst = pix + (size_t)y * w * 4;
            for (int x = 0; x < w; ++x) {
                dst[x*4+0] = src[x*4+2];  // B
                dst[x*4+1] = src[x*4+1];  // G
                dst[x*4+2] = src[x*4+0];  // R
                dst[x*4+3] = src[x*4+3];  // A
            }
        }
    }
    pfImgRelease(img);
    pfHRelease(handle);
    pfFree(ctx);

    out->width = w; out->height = h;
    out->stride = w * (bits > 8 ? 8 : 4);
    out->format = (bits > 8) ? QLPF_RGBA16F : QLPF_BGRA8;
    out->pixels = pix;
    out->freeFn = [](void *p) { delete[] (unsigned char*)p; };
    return true;
}

// ── 注册 ──
struct RegApi {
    void (*regDecoder)(const DecodePlugin *);
};
static DecodePlugin g_heic = { "heic|heif|avif|heifs", "", heic_query, heic_decode };

extern "C" __declspec(dllexport)
void qlens_plugin_entry(RegApi *api)
{
    if (api && api->regDecoder) api->regDecoder(&g_heic);
}
