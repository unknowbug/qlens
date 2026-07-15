#pragma once
#include <QImage>
#include <QString>
#include "export.h"

// SQLite 缩略图缓存
// 存储路径 → 缩略图 QByteArray 映射
struct ThumbnailCache {
    // 初始化数据库（路径自动从 app data dir 获取）
    static QLENS_EXPORT bool init();

    // 获取缓存（返回空 bytes = 未缓存）
    static QLENS_EXPORT QByteArray get(const QString &filePath);

    // 写入缓存
    static QLENS_EXPORT void put(const QString &filePath, const QByteArray &jpegData);
};
