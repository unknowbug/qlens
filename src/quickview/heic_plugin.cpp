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
typedef int(*fn_list_md)(void*, const char*, int*, int);   // heif_image_handle_get_list_of_metadata_block_IDs
typedef size_t(*fn_md_size)(void*, int);                    // get_metadata_size
typedef heif_error(*fn_get_md)(void*, int, void*);          // get_metadata
typedef heif_error(*fn_decode)(void*, void**, int, int, void*);
typedef void(*fn_img_release)(void*);
typedef int(*fn_img_w)(void*, int);  // (img, channel)
typedef int(*fn_img_h)(void*, int);
typedef const unsigned char*(*fn_plane)(void*, int, int*);

static HMODULE g_heifLib = nullptr;
static fn_ctx_alloc pfAlloc; static fn_ctx_free pfFree;
static fn_ctx_read_file pfRead; static fn_ctx_primary pfPrimary;
static fn_handle_release pfHRelease; static fn_handle_w pfHW; static fn_handle_h pfHH; static fn_handle_alpha pfHAlpha; static fn_handle_bits pfHBits;
static fn_list_md pfListMd; static fn_md_size pfMdSize; static fn_get_md pfGetMd;
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
    pfListMd = (fn_list_md)GetProcAddress(g_heifLib, "heif_image_handle_get_list_of_metadata_block_IDs");
    pfMdSize = (fn_md_size)GetProcAddress(g_heifLib, "heif_image_handle_get_metadata_size");
    pfGetMd = (fn_get_md)GetProcAddress(g_heifLib, "heif_image_handle_get_metadata");
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

// 解析 EXIF TIFF 的 Orientation（Tag 0x0112）——返回 0=无/1-8
static int ParseExifOrientation(const unsigned char *p, size_t size)
{
    if (size < 10 || !p) return 0;
    bool le = (p[0] == 'I' && p[1] == 'I');
    bool be = (p[0] == 'M' && p[1] == 'M');
    if (!le && !be) return 0;
    auto u16 = [&](const unsigned char *q) { return le ? (q[0] | (q[1] << 8)) : ((q[0] << 8) | q[1]); };
    auto u32 = [&](const unsigned char *q) {
        return le ? ((unsigned)q[0] | ((unsigned)q[1] << 8) | ((unsigned)q[2] << 16) | ((unsigned)q[3] << 24))
                  : (((unsigned)q[0] << 24) | ((unsigned)q[1] << 16) | ((unsigned)q[2] << 8) | (unsigned)q[3]);
    };
    if (u16(p + 2) != 42) return 0;  // TIFF magic
    unsigned ifd0 = u32(p + 4);
    if (ifd0 + 2 > size) return 0;
    int count = u16(p + ifd0);
    for (int i = 0; i < count; ++i) {
        const unsigned char *e = p + ifd0 + 2 + (size_t)i * 12;
        if (e + 12 > p + size) break;
        if (u16(e) == 0x0112) return u16(e + 8);  // Orientation（Type 3 SHORT）
    }
    return 0;
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
    // 读 EXIF Orientation（libheif metadata；EXIF 数据前 4 字节是 TIFF 偏移）
    if (pfListMd && pfMdSize && pfGetMd) {
        int mdIds[8] = {};
        int mdCount = pfListMd(handle, "Exif", mdIds, 8);
        for (int mi = 0; mi < mdCount && mi < 8; ++mi) {
            size_t sz = pfMdSize(handle, mdIds[mi]);
            if (sz > 8 && sz < 65536) {
                std::vector<unsigned char> md(sz);
                heif_error me = pfGetMd(handle, mdIds[mi], md.data());
                if (me.code == 0) {
                    int rot = ParseExifOrientation(md.data() + 4, sz - 4);
                    if (rot >= 1 && rot <= 8) { info->exifRot = rot; break; }
                }
            }
        }
    }
    // EXIF 旋转后显示尺寸（Orientation 6/8 = 90° 换宽高）
    if (info->exifRot == 6 || info->exifRot == 8) { info->rotateW = h; info->rotateH = w; }
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
