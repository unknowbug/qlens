#include "TagStore.h"
#include <QThread>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QVariant>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

TagStore::TagStore(QObject *parent) : QObject(parent) {}

TagStore::~TagStore() {
    if (m_db.isOpen()) m_db.close();
}

static void markHidden(const QString &path) {
#ifdef Q_OS_WIN
    SetFileAttributesW((LPCWSTR)path.utf16(), FILE_ATTRIBUTE_HIDDEN);
#endif
}

bool TagStore::open(const QString &folder) {
    close();
    m_folder = folder;
    QString dbPath = folder + "/qltag.db";
    if (!QDir(folder).exists()) return false;

    // 每次 open 用全新连接名：避免在同一连接上 setDatabaseName 换库导致旧 QSqlQuery 悬垂
    // （切目录时旧库的查询可能还在使用连接，换库即悬垂 → QString 损坏崩溃）
    static quint64 s_seq = 0;
    const QString connName = QStringLiteral("qlens_tags_%1").arg(++s_seq);
    m_db = QSqlDatabase::addDatabase("QSQLITE", connName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        // 打开失败：可能是崩溃残留的损坏 wal。清理后重试一次。
        QFile::remove(folder + "/qltag.db-wal");
        QFile::remove(folder + "/qltag.db-shm");
        if (!m_db.open()) {
            qWarning() << "TagStore: open failed" << m_db.lastError().text();
            return false;
        }
    }

    QSqlQuery q(m_db);
    q.exec("CREATE TABLE IF NOT EXISTS tags ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "name TEXT UNIQUE NOT NULL,"
           "category TEXT DEFAULT '',"
           "color TEXT DEFAULT '')");
    q.exec("CREATE TABLE IF NOT EXISTS image_tags ("
           "filename TEXT NOT NULL,"
           "tag_id INTEGER NOT NULL,"
           "source TEXT DEFAULT 'manual',"
           "confidence REAL DEFAULT 1.0,"
           "PRIMARY KEY(filename, tag_id))");
    q.exec("CREATE INDEX IF NOT EXISTS idx_image_tags_file ON image_tags(filename)");
    q.exec("PRAGMA journal_mode=WAL");  // 多进程并发（Manager + MCP）

    markHidden(dbPath);

    // 预加载 tag 名 → id
    m_tagNames.clear();
    QSqlQuery all(m_db);
    all.exec("SELECT id, name FROM tags");
    while (all.next())
        m_tagNames.insert(all.value(1).toString(), all.value(0).toInt());
    return true;
}

void TagStore::close() {
    QString connName = m_db.connectionName();
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    if (!connName.isEmpty())
        QSqlDatabase::removeDatabase(connName);  // 移除注册表，防止连接泄漏
    m_folder.clear();
    m_tagNames.clear();
}

int TagStore::addTag(const QString &name, const QString &category) {
    if (name.trimmed().isEmpty()) return -1;
    int existing = m_tagNames.value(name, -1);
    if (existing >= 0) return existing;

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO tags(name, category) VALUES(?, ?)");
    q.addBindValue(name);
    q.addBindValue(category);
    if (!q.exec()) {
        qWarning() << "TagStore: addTag failed" << q.lastError().text();
        return -1;
    }
    int id = q.lastInsertId().toInt();
    m_tagNames.insert(name, id);
    return id;
}

void TagStore::removeTag(int tagId) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM tags WHERE id=?");
    q.addBindValue(tagId);
    q.exec();
    for (auto it = m_tagNames.begin(); it != m_tagNames.end(); ) {
        if (it.value() == tagId) it = m_tagNames.erase(it);
        else ++it;
    }
}

QStringList TagStore::allTagNames() const {
    return m_tagNames.keys();
}

bool TagStore::addImageTag(const QString &filename, const QString &tagName) {
    int id = addTag(tagName);
    if (id < 0) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO image_tags(filename, tag_id) VALUES(?, ?)");
    q.addBindValue(filename);
    q.addBindValue(id);
    return q.exec();
}

bool TagStore::removeImageTag(const QString &filename, const QString &tagName) {
    int id = tagId(tagName);
    if (id < 0) return false;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM image_tags WHERE filename=? AND tag_id=?");
    q.addBindValue(filename);
    q.addBindValue(id);
    return q.exec();
}

QStringList TagStore::tagsForImage(const QString &filename) const {
    QStringList result;
    if (!m_db.isOpen()) return result;
    QSqlQuery q(m_db);
    q.prepare("SELECT t.name FROM tags t JOIN image_tags it ON t.id=it.tag_id "
              "WHERE it.filename=? ORDER BY t.name");
    q.addBindValue(filename);
    q.exec();
    while (q.next())
        result << q.value(0).toString();
    return result;
}

QStringList TagStore::searchTags(const QString &prefix) const {
    if (prefix.trimmed().isEmpty()) return QStringList();
    QStringList result;
    for (const QString &name : m_tagNames.keys()) {
        if (name.startsWith(prefix, Qt::CaseInsensitive))
            result << name;
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}


// ── 线程安全静态查询（B1：每线程独立连接）──

// 线程局部：folder → QSqlDatabase（每线程每文件夹一个连接）
static QHash<QString, QSqlDatabase> &threadConnections()
{
    static thread_local QHash<QString, QSqlDatabase> conns;
    return conns;
}

// 获取当前线程到 folder 的连接（不存在则创建）
static QSqlDatabase threadConnection(const QString &folder)
{
    auto &conns = threadConnections();
    auto it = conns.find(folder);
    if (it != conns.end())
        return it.value();

    // 连接名含线程 id + folder，保证线程/文件夹隔离
    QString connName = QStringLiteral("qlens_tag_w_%1_%2")
        .arg((quintptr)QThread::currentThreadId())
        .arg((quintptr)folder.constData(), 16);
    // 若已存在（同线程同 folder 重建），先移除
    if (QSqlDatabase::contains(connName))
        QSqlDatabase::removeDatabase(connName);
    auto db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(folder + "/qltag.db");
    if (!db.open()) {
        // 可能是崩溃残留，清 wal 重试
        QFile::remove(folder + "/qltag.db-wal");
        QFile::remove(folder + "/qltag.db-shm");
        db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(folder + "/qltag.db");
        if (!db.open())
            return {};
    }
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "name TEXT UNIQUE NOT NULL, category TEXT DEFAULT '', color TEXT DEFAULT '')");
    q.exec("CREATE TABLE IF NOT EXISTS image_tags (filename TEXT NOT NULL, tag_id INTEGER NOT NULL,"
           "source TEXT DEFAULT 'manual', confidence REAL DEFAULT 1.0, PRIMARY KEY(filename, tag_id))");
    conns.insert(folder, db);
    return db;
}

QStringList TagStore::queryTagsForImage(const QString &folder, const QString &filename)
{
    QSqlDatabase db = threadConnection(folder);
    if (!db.isOpen()) return {};
    QStringList result;
    QSqlQuery q(db);
    q.prepare("SELECT t.name FROM tags t JOIN image_tags it ON t.id=it.tag_id "
              "WHERE it.filename=? ORDER BY t.name");
    q.addBindValue(filename);
    q.exec();
    while (q.next())
        result << q.value(0).toString();
    return result;
}

QStringList TagStore::queryFilesWithTag(const QString &folder, const QString &tag)
{
    QSqlDatabase db = threadConnection(folder);
    if (!db.isOpen()) return {};
    QStringList result;
    QSqlQuery q(db);
    q.prepare("SELECT it.filename FROM image_tags it JOIN tags t ON t.id=it.tag_id WHERE t.name=?");
    q.addBindValue(tag);
    q.exec();
    while (q.next())
        result << q.value(0).toString();
    return result;
}

QStringList TagStore::queryFolderTags(const QString &folder)
{
    QSqlDatabase db = threadConnection(folder);
    if (!db.isOpen()) return {};
    QStringList result;
    QSqlQuery q(db);
    q.exec("SELECT DISTINCT t.name FROM tags t JOIN image_tags it ON t.id=it.tag_id ORDER BY t.name");
    while (q.next())
        result << q.value(0).toString();
    return result;
}

void TagStore::closeThreadConnection()
{
    auto &conns = threadConnections();
    for (auto it = conns.begin(); it != conns.end(); ++it) {
        QString name = it.value().connectionName();
        it.value().close();
        QSqlDatabase::removeDatabase(name);
    }
    conns.clear();
}
