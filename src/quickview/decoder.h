#pragma once
#include <windows.h>
#include <string>
#include "plugins.h"  // DecodeInfo/ImageBuffer/QLens 枚举

// WIC 图片解码 —— 返回 BGRA8 像素缓冲（兼容旧调用）
struct DecodedImage {
    int width = 0;
    int height = 0;
    int stride = 0;  // 每行字节（可能 > width*4，WIC 对齐）
    unsigned char *pixels = nullptr;
    ~DecodedImage() { delete[] pixels; }
};

// 解码信息查询（新接口：帧数/EXIF/ICC/HDR/矢量）
bool QueryImageInfo(const std::wstring &path, DecodeInfo &info);
// 按需解码（新接口：frame/target + 元数据，out.freeFn 负责释放）
bool DecodeImageAny(const std::wstring &path, int frame, int targetW, int targetH, ImageBuffer &out);

// 兼容旧接口：全尺寸 BGRA8 解码（frame：GIF 等多帧格式指定帧）
bool DecodeImageFile(const std::wstring &path, DecodedImage &out, int frame = 0);
// 缩略解码：WIC 直接解码到目标尺寸（快）
bool DecodeImageThumb(const std::wstring &path, DecodedImage &out, int targetW, int targetH);
// 插件解码回退（WIC 不支持时）
bool PluginDecodeFallback(const std::wstring &path, DecodedImage &out);
