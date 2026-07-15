#include "thumbnail.h"
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QCryptographicHash>

static const char *kDbName = "qlens_thumbnails.db";

static QSqlDatabase db()
{
    static QSqlDatabase d = QSqlDatabase::database("thumb");
    return d;
}

bool ThumbnailCache::init()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);

    auto d = QSqlDatabase::addDatabase("QSQLITE", "thumb");
    d.setDatabaseName(path + "/" + kDbName);
    if (!d.open()) return false;

    QSqlQuery q(d);
    q.exec("CREATE TABLE IF NOT EXISTS thumbnails ("
           "path_hash TEXT PRIMARY KEY,"
           "path TEXT,"
           "mtime INTEGER,"
           "data BLOB)");
    return true;
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
    if (q.exec() && q.next())
        return q.value(0).toByteArray();
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
    q.prepare("INSERT OR REPLACE INTO thumbnails (path_hash, path, mtime, data) "
              "VALUES (?,?,?,?)");
    q.addBindValue(hash);
    q.addBindValue(filePath);
    q.addBindValue(fi.lastModified().toSecsSinceEpoch());
    q.addBindValue(jpegData);
    q.exec();
}
