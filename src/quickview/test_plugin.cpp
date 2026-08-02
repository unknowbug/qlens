// 测试插件：.qtest 格式解码器（返回 256x128 红绿渐变）
#include <windows.h>
#include <malloc.h>
#include "plugins.h"

// 与核心一致的 RegApi
struct RegApi {
    void (*regDecoder)(const DecodePlugin *);
    void (*regRender)(const RenderPlugin *);
};

static bool qtest_decode(const wchar_t *path, ImageBuffer *out)
{
    int w = 256, h = 128;
    out->width = w; out->height = h; out->stride = w * 4;
    out->pixels = (unsigned char*)malloc((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char *p = out->pixels + ((size_t)y * w + x) * 4;
            p[0] = (unsigned char)(x * 255 / w);
            p[1] = (unsigned char)(y * 255 / h);
            p[2] = 255 - (unsigned char)(x * 255 / w);
            p[3] = 255;
        }
    return true;
}

static void qtest_release(ImageBuffer *buf)
{
    if (buf && buf->pixels) { free(buf->pixels); buf->pixels = nullptr; }
}

static DecodePlugin g_decoder = { "qtest", qtest_decode, qtest_release };

extern "C" __declspec(dllexport)
void qlens_plugin_entry(RegApi *api)
{
    if (api && api->regDecoder) api->regDecoder(&g_decoder);
}
