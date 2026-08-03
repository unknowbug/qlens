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

    // 1) 优先编译进 exe 的资源（qrc：icons/XXX.ico）——任何安装位置可用
    QString resPath = ":/icons/" + key + ".ico";
    if (QFile::exists(resPath)) {
        QIcon ic(resPath);
        if (!ic.isNull()) { g_iconCache[key] = ic; return ic; }
    }
    // 2) 回退：exe 旁 icons/（可选外部覆盖/换肤）
    QString f = QCoreApplication::applicationDirPath() + "/icons/" + key + ".ico";
    if (QFile::exists(f)) {
        QIcon ic(f);
        if (!ic.isNull()) { g_iconCache[key] = ic; return ic; }
    }
    g_iconCache[key] = {};  // 缓存"无"避免重复找
    return {};
}
