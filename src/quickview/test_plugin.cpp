// 测试插件：.qtest 格式解码器（返回 256x128 红绿渐变）
// 新接口：query（能力）+ decode（按需）
#include <windows.h>
#include <malloc.h>
#include <cstring>
#include "plugins.h"

// 与核心一致的 RegApi
struct RegApi {
    void (*regDecoder)(const DecodePlugin *);
};

static bool qtest_query(const wchar_t *path, DecodeInfo *info)
{
    info->format = QLPF_BGRA8;
    info->frames = 1;
    info->suggestW = 256;
    info->suggestH = 128;
    info->rotateW = 256;
    info->rotateH = 128;
    info->hasAlpha = true;
    info->alphaMode = 0;  // straight
    info->error = QLERR_OK;
    return true;
}

static bool qtest_decode(const wchar_t *path, int frame, int targetW, int targetH, ImageBuffer *out)
{
    int w = 256, h = 128;
    // 目标尺寸缩放
    if (targetW > 0 && targetH > 0 && (targetW < w || targetH < h)) {
        float s = (float)targetW / w;
        if ((float)targetH / h < s) s = (float)targetH / h;
        w = (int)(w * s); h = (int)(h * s);
        if (w < 1) w = 1; if (h < 1) h = 1;
    }
    out->width = w; out->height = h; out->stride = w * 4;
    out->format = QLPF_BGRA8;
    out->pixels = (unsigned char*)malloc((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char *p = out->pixels + ((size_t)y * w + x) * 4;
            p[0] = (unsigned char)(x * 255 / w);
            p[1] = (unsigned char)(y * 255 / h);
            p[2] = 255 - (unsigned char)(x * 255 / w);
            p[3] = 255;
        }
    out->freeFn = [](void *p) { free(p); };
    return true;
}

static DecodePlugin g_decoder = { "qtest", nullptr, qtest_query, qtest_decode };

extern "C" __declspec(dllexport)
void qlens_plugin_entry(RegApi *api)
{
    if (api && api->regDecoder) api->regDecoder(&g_decoder);
}
