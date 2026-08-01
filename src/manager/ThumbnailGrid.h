#pragma once
#include <QWidget>
#include <QListView>
#include <QAbstractListModel>
#include <QStringList>
#include <QPixmap>
#include "TagStore.h"
#include "thumbnail.h"

// 缩略图网格 —— QListView 虚拟化（上千张不卡）
// 性能：系统图标秒开 → SQLite 缓存 → QThreadPool 并发解码 → delegate 只绘制可见项
// 缓存读写只在主线程（ThumbnailCache 单连接，非线程安全）

// ── 网格项 ──
struct ThumbItem {
    QString name;
    QString path;
    bool    isDir = false;
    QPixmap pix;          // 缩略图（未加载时为系统图标）
    bool    highlighted = false;
    bool    hasQcBadge  = false;
};

// ── 网格模型 ──
class ThumbModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole    = Qt::DisplayRole,
        PixRole     = Qt::DecorationRole,
        PathRole    = Qt::UserRole + 1,
        IsDirRole   = Qt::UserRole + 2,
        HighlightRole = Qt::UserRole + 3,
        QcBadgeRole = Qt::UserRole + 4,
    };
    explicit ThumbModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setItems(const QList<ThumbItem> &items);
    const ThumbItem &itemAt(int row) const { return m_items.at(row); }
    void updatePix(int row, const QPixmap &pix);
    void updateHighlight(int row, bool hit);

private:
    QList<ThumbItem> m_items;
};

// ── 网格视图 ──
class ThumbnailGrid : public QListView {
    Q_OBJECT
public:
    explicit ThumbnailGrid(TagStore *store, QWidget *parent = nullptr);
    ~ThumbnailGrid() override;

    void loadFolder(const QString &path);
    void setHighlightTag(const QString &tag);
    // 过滤模式：只显示命中标签的图片（空 = 不过滤）
    void setFilterTag(const QString &tag);

    // 当前文件夹的全部标签（供工具条候选，主线程汇总）
    QStringList folderTags() const;

    // 线程池任务回调（主线程执行）
    void applyImageThumb(int row, const QString &path, const QPixmap &pix);
    void applyFolderThumb(int row, const QPixmap &pix);

signals:
    void folderDoubleClicked(const QString &path);
    void imageDoubleClicked(const QString &path);
    void imageClicked(const QString &path);

protected:
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    int thumbCellSize() const { return m_thumbSize + 24; }  // 图 + 文字

    int  findModelRow(const QString &path) const;
    void applyFilter();

    TagStore    *m_store = nullptr;
    ThumbModel  *m_model = nullptr;
    int          m_thumbSize = 160;
    QStringList  m_imageFiles;
    QStringList  m_subDirs;
    QList<ThumbItem> m_allItems;   // 全量（不过滤）
    QString      m_currentFolder;
    QString      m_highlightTag;
    QString      m_filterTag;
    int          m_loadToken = 0;
};
