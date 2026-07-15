#pragma once
#include <QImage>
#include <QString>
#include "export.h"

struct Clipboard {
    // 复制图片到系统剪贴板
    // 同时写入两种格式：
    //   image/png       → 支持图片粘贴的应用（聊天框、编辑器）
    //   text/uri-list   → 不支持图片的输入框自动降级为文件路径
    static QLENS_EXPORT void copyImage(const QImage &img);
    static QLENS_EXPORT void copyPath(const QString &filePath);
};
