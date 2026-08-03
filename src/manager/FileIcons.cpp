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

    // 路径：exe 旁 icons/（发布）→ exe 的 ../../icons（开发：build-qv/Release → 项目根/icons）
    QString base = QCoreApplication::applicationDirPath();
    QStringList dirs = {
        base + "/icons",
        base + "/../../icons",
    };
    for (const QString &d : dirs) {
        QString f = d + "/" + key + ".ico";
        if (QFile::exists(f)) {
            QIcon ic(f);
            if (!ic.isNull()) {
                g_iconCache[key] = ic;
                return ic;
            }
        }
    }
    g_iconCache[key] = {};  // 缓存"无"避免重复找
    return {};
}
