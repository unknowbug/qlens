#include "thumbnail.h"
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QCryptographicHash>
#include <QThread>

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
           "size INTEGER)");
    // 老库补 size 列（缓存键含尺寸——缩略图尺寸策略变化后旧缓存自动失效）
    QSqlQuery pragma(d);
    bool hasSize = false;
    if (pragma.exec("PRAGMA table_info(thumbnails)")) {
        while (pragma.next())
            if (pragma.value(1).toString() == QLatin1String("size")) { hasSize = true; break; }
    }
    if (!hasSize) {
        QSqlQuery alt(d);
        alt.exec("ALTER TABLE thumbnails ADD COLUMN size INTEGER");
    }
    return true;
}

QByteArray ThumbnailCache::get(const QString &filePath, int size)
{
    auto d = db();
    if (!d.isOpen()) return {};

    QFileInfo fi(filePath);
    QString hash = QString::fromUtf8(QCryptographicHash::hash(
        filePath.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));

    QSqlQuery q(d);
    q.prepare("SELECT data FROM thumbnails WHERE path_hash=? AND mtime=? AND size=?");
    q.addBindValue(hash);
    q.addBindValue(fi.lastModified().toSecsSinceEpoch());
    q.addBindValue(size);
    if (q.exec() && q.next())
        return q.value(0).toByteArray();
    return {};
}

void ThumbnailCache::put(const QString &filePath, int size, const QByteArray &jpegData)
{
    auto d = db();
    if (!d.isOpen()) return;

    QFileInfo fi(filePath);
    QString hash = QString::fromUtf8(QCryptographicHash::hash(
        filePath.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));

    QSqlQuery q(d);
    q.prepare("INSERT OR REPLACE INTO thumbnails (path_hash, path, mtime, data, size) "
              "VALUES (?,?,?,?,?)");
    q.addBindValue(hash);
    q.addBindValue(filePath);
    q.addBindValue(fi.lastModified().toSecsSinceEpoch());
    q.addBindValue(jpegData);
    q.addBindValue(size);
    q.exec();
}
