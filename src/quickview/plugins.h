// 插件接口 —— 核心定义，插件 DLL 实现
// 设计：解码器插件（格式解码）+ 渲染注入插件（HDR/色彩算法，纯像素变换，不碰 D3D11）
#pragma once
#include <windows.h>
#include <string>
#include <vector>

// 像素缓冲（跨 DLL 安全：纯内存，无 D3D 对象）
struct ImageBuffer {
    int width = 0;
    int height = 0;
    int stride = 0;
    unsigned char *pixels = nullptr;
};

// ── 解码器插件 ──
struct DecodePlugin {
    const char *ext;
    bool (*decode)(const wchar_t *path, ImageBuffer *out);
    void (*release)(ImageBuffer *buf);
};

// ── 渲染注入插件（HDR/色彩算法）──
struct RenderPlugin {
    const char *name;
    bool (*process)(const ImageBuffer *src, ImageBuffer *dst);
};

// ── 插件注册表 ──
namespace QLensPlugins {

void RegisterDecoder(const DecodePlugin &p);
const DecodePlugin *FindDecoder(const std::wstring &extLower);
void RegisterRender(const RenderPlugin &p);
const RenderPlugin *FindRender(const char *name);
bool LoadPluginsFromDir(const std::wstring &dir);
bool ApplyRender(const char *name, const ImageBuffer *src, ImageBuffer *dst);

}  // namespace QLensPlugins
