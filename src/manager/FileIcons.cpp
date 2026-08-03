#include "FileIcons.h"
#include <QFile>
#include <QCoreApplication>
#include <QHash>

static QHash<QString, QIcon> g_iconCache;

QIcon FileIcons::iconForExt(const QString &ext)
{
    QString key = ext.toUpper();
    if (key.isEmpty()) return {};
    if (g_iconCache.contains(key)) return g_iconCache[key];

    // 1) exe 旁 icons/ 优先（便携/绿色——用户可自由替换图标换肤）
    QString f = QCoreApplication::applicationDirPath() + "/icons/" + key + ".ico";
    if (QFile::exists(f)) {
        QIcon ic(f);
        if (!ic.isNull()) { g_iconCache[key] = ic; return ic; }
    }
    // 2) 兜底：编译进 exe 的资源（qrc——万一发布漏带 icons/ 目录也能显示）
    QString resPath = ":/icons/" + key + ".ico";
    if (QFile::exists(resPath)) {
        QIcon ic(resPath);
        if (!ic.isNull()) { g_iconCache[key] = ic; return ic; }
    }
    g_iconCache[key] = {};  // 缓存"无"避免重复找
    return {};
}
