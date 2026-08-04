#include "thumbnail.h"
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QCryptographicHash>
#include <QThread>
#include <QDateTime>
#include <atomic>

static const char *kDbName = "qlens_thumbnails.db";

// 每线程独立 SQLite 连接：Qt SQL 连接绑定创建线程，跨线程用同一连接
// 会触发 "database does not belong to the calling thread" 且是未定义行为
// （缩略图任务在 QThreadPool 后台线程调用 get/put，必须线程局部）
static QSqlDatabase db()
{
    static thread_local QSqlDatabase d;
    if (!d.isValid()) {
        const QString connName =
            QString("thumb_%1").arg((quintptr)QThread::currentThreadId(), 0, 16);
        d = QSqlDatabase::addDatabase("QSQLITE", connName);
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(path);
        d.setDatabaseName(path + "/" + kDbName);
        d.open();
    }
    return d;
}

bool ThumbnailCache::init()
{
    auto d = db();
    if (!d.isValid()) return false;

    QSqlQuery q(d);
    q.exec("CREATE TABLE IF NOT EXISTS thumbnails ("
           "path_hash TEXT PRIMARY KEY,"
           "path TEXT,"
           "mtime INTEGER,"
           "data BLOB,"
           "last_access INTEGER)");
    // 老库补 last_access 列（LRU 容量管理用）
    QSqlQuery pragma(d);
    bool hasLast = false;
    if (pragma.exec("PRAGMA table_info(thumbnails)")) {
        while (pragma.next())
            if (pragma.value(1).toString() == QLatin1String("last_access")) { hasLast = true; break; }
    }
    if (!hasLast) {
        QSqlQuery alt(d);
        alt.exec("ALTER TABLE thumbnails ADD COLUMN last_access INTEGER");
    }
    return true;
}

// 容量管理（LRU）：总缓存 > 500MB 删最旧（每 50 次写入检查一次，开销可控）
static void trimCache(QSqlDatabase &d)
{
    static std::atomic<int> counter{0};
    if (++counter % 50 != 0) return;
    QSqlQuery q(d);
    if (!q.exec("SELECT SUM(LENGTH(data)) FROM thumbnails") || !q.next()) return;
    const qint64 total = q.value(0).toLongLong();
    const qint64 kMaxBytes = 500LL * 1024 * 1024;   // 500MB 上限
    if (total <= kMaxBytes) return;
    const qint64 excess = total - kMaxBytes;
    QSqlQuery del(d);
    del.prepare("DELETE FROM thumbnails WHERE path_hash IN "
                "(SELECT path_hash FROM thumbnails ORDER BY last_access LIMIT ?)");
    del.addBindValue(excess / 30000 + 50);   // 粗略按 30KB/条 估算删除条数（一次不够下次补）
    del.exec();
}

QByteArray ThumbnailCache::get(const QString &filePath)
{
    auto d = db();
    if (!d.isOpen()) return {};

    QFileInfo fi(filePath);
    QString hash = QString::fromUtf8(QCryptographicHash::hash(
        filePath.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));

    QSqlQuery q(d);
    q.prepare("SELECT data FROM thumbnails WHERE path_hash=? AND mtime=?");
    q.addBindValue(hash);
    q.addBindValue(fi.lastModified().toSecsSinceEpoch());
    if (q.exec() && q.next()) {
        // 命中：刷新 LRU 时间戳
        QSqlQuery up(d);
        up.prepare("UPDATE thumbnails SET last_access=? WHERE path_hash=?");
        up.addBindValue(QDateTime::currentSecsSinceEpoch());
        up.addBindValue(hash);
        up.exec();
        return q.value(0).toByteArray();
    }
    return {};
}

void ThumbnailCache::put(const QString &filePath, const QByteArray &jpegData)
{
    auto d = db();
    if (!d.isOpen()) return;

    QFileInfo fi(filePath);
    QString hash = QString::fromUtf8(QCryptographicHash::hash(
        filePath.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));

    QSqlQuery q(d);
    q.prepare("INSERT OR REPLACE INTO thumbnails (path_hash, path, mtime, data, last_access) "
              "VALUES (?,?,?,?,?)");
    q.addBindValue(hash);
    q.addBindValue(filePath);
    q.addBindValue(fi.lastModified().toSecsSinceEpoch());
    q.addBindValue(jpegData);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
    trimCache(d);
}
