#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSqlDatabase>

// 标签数据层 —— 每文件夹一个 qltag.db（隐藏文件）
// 与 MCP Server (Python qlens_lib) 共享同一格式，两边互通
// DB 存纯文件名（DB 就在它管的文件夹里）

class TagStore : public QObject {
    Q_OBJECT
public:
    explicit TagStore(QObject *parent = nullptr);
    ~TagStore() override;

    // 绑定文件夹：打开 folder/qltag.db（不存在则创建，Windows 上设隐藏）
    bool open(const QString &folder);
    void close();
    QString folder() const { return m_folder; }
    bool isOpen() const { return m_db.isOpen(); }

    // 标签 CRUD
    int  addTag(const QString &name, const QString &category = QString());
    void removeTag(int tagId);
    QStringList allTagNames() const;

    // 图片标签（filename = 纯文件名，如 "001.jpg"）
    bool addImageTag(const QString &filename, const QString &tagName);
    bool removeImageTag(const QString &filename, const QString &tagName);
    QStringList tagsForImage(const QString &filename) const;

    // 前缀补全
    QStringList searchTags(const QString &prefix) const;

    // 标签名 → id（内存缓存，未找到返回 -1）
    int tagId(const QString &name) const { return m_tagNames.value(name, -1); }

private:
    QSqlDatabase m_db;
    QString      m_folder;
    QHash<QString,int> m_tagNames;  // tag 名 → id
};
