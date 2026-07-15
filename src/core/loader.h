#pragma once
#include <QImage>
#include <QString>
#include "export.h"

// RAW 解码器：读取 embedded JPEG 预览
// 不解码全尺寸 RAW 像素，只提取相机内嵌的 JPEG 缩略图
struct RawLoader {
    // 从 RAW 文件提取 embedded JPEG 预览
    // 返回空 QImage 表示解码失败
    static QLENS_EXPORT QImage loadThumbnail(const QString &filePath, int maxDim = 2048);

    // 解码全尺寸图像（用于查看单张时）
    static QLENS_EXPORT QImage loadFull(const QString &filePath, int maxDim = 3840);
};
