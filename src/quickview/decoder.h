#pragma once
#include <windows.h>
#include <string>

// WIC 图片解码 —— 返回 BGRA8 像素缓冲
struct DecodedImage {
    int width = 0;
    int height = 0;
    int stride = 0;  // 每行字节（可能 > width*4，WIC 对齐）
    unsigned char *pixels = nullptr;
    ~DecodedImage() { delete[] pixels; }
};

bool DecodeImageFile(const std::wstring &path, DecodedImage &out);
// 缩略解码：WIC 直接解码到目标尺寸（快）
bool DecodeImageThumb(const std::wstring &path, DecodedImage &out, int targetW, int targetH);
// 插件解码回退（WIC 不支持时）
bool PluginDecodeFallback(const std::wstring &path, DecodedImage &out);
