// decode_api.h —— 统一解码接口（Qt 侧用），封装 qlens_decode + 插件系统
// 提供：解码任意图片（含 SVG/插件格式）→ QImage
#pragma once
#include <QImage>
#include <QString>
#include "export.h"

namespace QLensCore {

// 解码任意图片（WIC + 插件），返回 QImage（失败返回空）
// maxDim > 0 时限制最长边（缩略图场景）
QLENS_EXPORT QImage decodeImage(const QString &filePath, int maxDim = 0);

// 查询图片信息（格式/帧数/EXIF 旋转/是否矢量）
struct ImageInfo {
    bool isVector = false;
    bool hasAlpha = false;
    int  frames = 1;
    int  suggestW = 0, suggestH = 0;
    int  exifRot = 0;
    bool isHdr = false;
};
QLENS_EXPORT bool queryImageInfo(const QString &filePath, ImageInfo *info);

}  // namespace QLensCore
