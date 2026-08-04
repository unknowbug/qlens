#pragma once
#include <QWidget>
#include <QListView>
#include <QAbstractListModel>
#include <QMimeData>
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

// 目录扫描结果（W2 worker 线程返回值）
struct ScanResult {
    QStringList subDirs;
    QStringList imageFiles;
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
    // 可编辑（内联重命名需要）
    Qt::ItemFlags flags(const QModelIndex &index) const override {
        return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
    }
    // 拖出：返回选中项的文件 URL（拖到资源管理器 = 复制文件）
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    QStringList mimeTypes() const override { return {QStringLiteral("text/uri-list")}; }

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
    // 打开文件夹后选中指定图片（滚动到可见；loadFolder 异步——pending 到扫描完成）
    void selectImage(const QString &path);
    void setHighlightTag(const QString &tag);
    // 过滤模式：只显示命中标签的图片（空 = 不过滤）
    void setFilterTag(const QString &tag);   // 逗号分隔多 tag = AND
    void setFilterQc(const QString &qc);
    // 缩略图大小（32~480；状态栏滑块 / Ctrl+滚轮 调用）
    void setThumbSize(int size);
    int thumbSize() const { return m_thumbSize; }

    // 当前文件夹全部图片路径（QC 批量检测用；文件夹项排除）
    QStringList allImagePaths() const;
    // 重新加载当前文件夹（QC 检测后刷新缩略图/角标）
    void refreshCurrentFolder();

    // 当前文件夹的全部标签（供工具条候选，主线程汇总）
    QStringList folderTags() const;
    QString currentFolder() const { return m_currentFolder; }
    // 图片数量（不含子目录）——状态栏用
    int imageCount() const;

    // 线程池任务回调（主线程执行；QPixmap 只能 GUI 线程创建）
    // token 用于丢弃过期任务：loadFolder 自增，回调不匹配则丢弃（防止切目录竞态）
    void applyImageThumb(int row, const QString &path, const QImage &img, int token);
    void applyFolderThumb(int row, const QImage &img, int token);
    void applyScanResult(const ScanResult &res, int token);

signals:
    void folderDoubleClicked(const QString &path);
    void imageDoubleClicked(const QString &path);
    void imageClicked(const QString &path);
    void tagsChanged();   // 批量打标/删标后（刷新筛选候选 + 缩略图角标）
    void thumbSizeChanged(int size);   // Ctrl+滚轮缩放 → 状态栏滑块同步

protected:
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    QPoint m_pressPos;   // 按下位置（release 判断拖动/框选）
    bool   m_pressOnItem = false;   // press 落在 item 上（手动接管点击，release 不调 super）
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    int thumbCellSize() const { return m_thumbSize + 24; }  // 图 + 文字

    int  findModelRow(const QString &path) const;
    // 右键批量功能（ACDSEE 风格）
    void saveAsDialog(const QString &path);
    void batchConvert();
    void batchResize();
    void batchRename();
    // 批量标签编辑（作用于选中集 + 右键项）
    void batchAddTags(const QString &rightClickPath);
    void batchRemoveTags(const QString &rightClickPath);
    void renameSelected();   // 单选=单文件重命名；多选=批量重命名（选中项）
    QStringList selectedImagePaths() const;
    // 慢双击重命名跟踪
    int    m_lastClickRow  = -1;
    qint64 m_lastClickTime = 0;
    void applyFilter();

    TagStore    *m_store = nullptr;
    ThumbModel  *m_model = nullptr;
    int          m_thumbSize = 160;
    QStringList  m_imageFiles;
    QStringList  m_subDirs;
    QList<ThumbItem> m_allItems;   // 全量（不过滤）
    QString      m_currentFolder;
    QString      m_pendingSelect;  // 待选中图片（loadFolder 异步完成后处理）
    QString      m_highlightTag;
    QString      m_filterTag;
    QStringList  m_filterTags;  // 逗号解析后的多 tag（AND）
    QString      m_filterQc;   // 固定标筛选（与 m_filterTag AND）
    int          m_loadToken = 0;
};
