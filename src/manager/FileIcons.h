#pragma once
#include <QIcon>
#include <QString>

// 文件类型图标缓存（icons/ 目录按扩展名加载）
// 缩略图生成前使用：按扩展名显示对应格式图标（JPG.ico / PNG.ico ...）
class FileIcons {
public:
    // 按扩展名（如 "jpg"）返回图标；无匹配返回空 QIcon
    static QIcon iconForExt(const QString &ext);
};
