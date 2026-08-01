#include "TagStore.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
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

    m_db = QSqlDatabase::addDatabase("QSQLITE", "qlens_tags_" + QString::number((quintptr)this));
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qWarning() << "TagStore: open failed" << m_db.lastError().text();
        return false;
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
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
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
