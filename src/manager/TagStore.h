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
    int  addTag(const QString &name, const QString &category = QString(), const QString &icon = QString());
    void removeTag(int tagId);
    QStringList allTagNames() const;
    // 标签颜色（tags.color 字段；空 = 未设置）
    QString tagColor(const QString &name) const;
    void setTagColor(const QString &name, const QString &color);
    // 固定标（QC）：category='qc' 的 tag 名列表
    QStringList qcTagNames() const;
    // 固定标（QC）：tag 的 icon（非空 = 固定标，缩略图右上角显示）
    QString tagIcon(const QString &name) const;
    void ensureQcTags();

    // 图片标签（filename = 纯文件名，如 "001.jpg"）
    bool addImageTag(const QString &filename, const QString &tagName);
    bool removeImageTag(const QString &filename, const QString &tagName);
    QStringList tagsForImage(const QString &filename) const;

    // 前缀补全
    QStringList searchTags(const QString &prefix) const;

    // 标签名 → id（内存缓存，未找到返回 -1）
    int tagId(const QString &name) const { return m_tagNames.value(name, -1); }

    // ── 线程安全静态查询（W2 worker 用）──
    // 每线程独立连接（B1），按 folder 打开 qltag.db，不依赖实例连接
    static QStringList queryTagsForImage(const QString &folder, const QString &filename);
    static QStringList queryFolderTags(const QString &folder);
    static QStringList queryFilesWithTag(const QString &folder, const QString &tag);
    // 清空当前线程的连接缓存（worker 线程退出前调用）
    static void closeThreadConnection();

private:
    QSqlDatabase m_db;
    QString      m_folder;
    QHash<QString,int> m_tagNames;  // tag 名 → id
};
