// decode_api.cpp —— 统一解码接口实现
// 用 qlens_decode（decoder.cpp + plugins.cpp）解码任意图片 → QImage
#include "decode_api.h"
#include "decoder.h"
#include "plugins.h"
#include <math.h>

namespace QLensCore {

QImage decodeImage(const QString &filePath, int maxDim)
{
    std::wstring path = filePath.toStdWString();
    DecodeInfo info;
    if (!QueryImageInfo(path, info)) return {};

    // 完整解码：插件 → WIC 兜底（decoder.h 的 DecodeImageAny）
    ImageBuffer ib = {};
    bool ok = DecodeImageAny(path, 0, maxDim, maxDim, ib);
    if (!ok || !ib.pixels) return {};

    QImage img;
    if (ib.format == QLPF_BGRA8) {
        img = QImage((const uchar*)ib.pixels, ib.width, ib.height, ib.stride, QImage::Format_ARGB32);
        img = img.copy();  // 深拷贝（释放插件缓冲后数据仍有效）
    } else if (ib.format == QLPF_RGBA16F) {
        // 高位深：转 8bit（tone map 已在 DecodeImageFile 里，这里简单转）
        img = QImage(ib.width, ib.height, QImage::Format_ARGB32);
        const unsigned short *src = (const unsigned short*)ib.pixels;
        for (int y = 0; y < ib.height; ++y)
            for (int x = 0; x < ib.width; ++x) {
                size_t i = (size_t)y * ib.width + x;
                auto half2f = [](unsigned short h) -> float {
                    unsigned e = (h >> 10) & 0x1F, m = h & 0x3FF;
                    if (e == 0) return (m == 0) ? 0.0f : (float)m / 1024.0f / 16384.0f;
                    if (e == 31) return 1.0f;
                    return ldexpf((float)(m | 0x400), (int)e - 25);
                };
                float r = half2f(src[i*4+0]), g = half2f(src[i*4+1]), b = half2f(src[i*4+2]);
                auto cl = [](float v) -> int { v = v < 0 ? 0 : (v > 1 ? 1 : v); return (int)(v * 255); };
                img.setPixel(x, y, qRgba(cl(r), cl(g), cl(b), 255));
            }
    }
    if (ib.freeFn) ib.freeFn(ib.pixels);
    return img;
}

bool queryImageInfo(const QString &filePath, ImageInfo *info)
{
    if (!info) return false;
    DecodeInfo di;
    if (!QueryImageInfo(filePath.toStdWString(), di)) return false;
    info->isVector = di.vector;
    info->hasAlpha = di.hasAlpha;
    info->frames = di.frames;
    info->suggestW = di.suggestW;
    info->suggestH = di.suggestH;
    info->exifRot = di.exifRot;
    info->isHdr = (di.format == QLPF_RGBA16F || di.format == QLPF_RGBA32F);
    return true;
}

}  // namespace QLensCore
