#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ThumbnailGrid.h"
#include "i18n.h"
#include "FileIcons.h"
#include <QTimer>

#include <QDir>
#include <QImageReader>
#include "decode_api.h"
#include <QFile>
#include <QtEndian>
#include <QThreadPool>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFileIconProvider>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QUrl>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QBuffer>
#include <QMouseEvent>
#include <QDateTime>
#include <QHash>

// 固定标（QC）标签 → emoji 映射——CV 可检测的固定标签，缩略图右上角显示
static const QHash<QString, QString> &qcEmojiMap()
{
    static const QHash<QString, QString> m = {
        {QStringLiteral("红眼"),     QStringLiteral("👁")},
        {QStringLiteral("闭眼"),     QStringLiteral("😑")},
        {QStringLiteral("模糊"),     QStringLiteral("🌫")},
        {QStringLiteral("曝光过度"), QStringLiteral("☀")},
        {QStringLiteral("色偏"),     QStringLiteral("🎨")},
    };
    return m;
}

// 翻译辅助：msgid=中文，默认中文；.po 覆盖为目标语言
static QString T(const wchar_t *id) { return QString::fromWCharArray(I18n::Get(id)); }

// 跨盘文件夹递归复制（拖入文件夹用）
static bool copyDirRecursive(const QString &srcDir, const QString &dstDir);
#include <QBuffer>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSet>

// ── 模型实现 ──

ThumbModel::ThumbModel(QObject *parent) : QAbstractListModel(parent) {}

int ThumbModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : (int)m_items.size();
}

QVariant ThumbModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= (int)m_items.size())
        return {};
    const ThumbItem &it = m_items.at(index.row());
    switch (role) {
    case NameRole:    return it.name;
    case PixRole:     return it.pix;
    case PathRole:    return it.path;
    case IsDirRole:   return it.isDir;
    case HighlightRole: return it.highlighted;
    case QcBadgeRole: return it.hasQcBadge;
    default: return {};
    }
}

void ThumbModel::setItems(const QList<ThumbItem> &items) {
    beginResetModel();
    m_items = items;
    endResetModel();
}

void ThumbModel::updatePix(int row, const QPixmap &pix) {
    if (row < 0 || row >= (int)m_items.size()) return;
    m_items[row].pix = pix;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {PixRole});
    // IconMode + setUniformItemSizes 下 dataChanged 单行可能不触发重绘，强制刷新
    if (auto *v = qobject_cast<QListView*>(QObject::parent()))
        v->viewport()->update();
}

void ThumbModel::updateHighlight(int row, bool hit) {
    if (row < 0 || row >= (int)m_items.size()) return;
    m_items[row].highlighted = hit;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {HighlightRole});
}

// ── 绘制委托 ──

class ThumbDelegate : public QStyledItemDelegate {
public:
    explicit ThumbDelegate(ThumbModel *model, int *thumbSize, ThumbnailGrid *grid,
                           QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_model(model), m_thumbSize(thumbSize), m_grid(grid) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(*m_thumbSize + 4, *m_thumbSize + 26);
    }

    // ── 内联编辑（资源管理器风格：文件名位置直接编辑）──
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *ed = new QLineEdit(parent);
        ed->setFrame(false);
        ed->setStyleSheet("background:#2a2a2a; color:#fff; border:1px solid #3a5a9a;");
        return ed;
    }
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override {
        auto *ed = qobject_cast<QLineEdit *>(editor);
        if (ed) { ed->setText(idx.data(ThumbModel::NameRole).toString()); ed->selectAll(); }
    }
    void setModelData(QWidget *editor, QAbstractItemModel *, const QModelIndex &idx) const override {
        auto *ed = qobject_cast<QLineEdit *>(editor);
        if (!ed || !m_grid) return;
        QString newName = ed->text().trimmed();
        if (newName.isEmpty()) return;
        QString oldPath = idx.data(ThumbModel::PathRole).toString();
        QFileInfo fi(oldPath);
        if (!newName.contains('.')) newName += "." + fi.suffix();  // 补扩展名
        if (newName == fi.fileName()) return;
        QString dst = QDir(m_grid->currentFolder()).filePath(newName);
        if (QFileInfo(dst).exists()) return;
        if (QFile::rename(oldPath, dst))
            m_grid->loadFolder(m_grid->currentFolder());
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &opt, const QModelIndex &) const override {
        // 编辑器定位在文件名区域（同 paint 的 textRect）
        QRect cell = opt.rect;
        QRect textRect = cell.adjusted(4, *m_thumbSize + 6, -4, -2);
        editor->setGeometry(textRect);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override {
        // 模型 reset 竞态防护：索引无效或越界时直接返回（绘制由 reset 后重新触发）
        if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_model->rowCount())
            return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        bool isDir   = idx.data(ThumbModel::IsDirRole).toBool();
        bool hl      = idx.data(ThumbModel::HighlightRole).toBool();
        bool qcBadge = idx.data(ThumbModel::QcBadgeRole).toBool();
        QPixmap pix  = idx.data(ThumbModel::PixRole).value<QPixmap>();
        QString name = idx.data(ThumbModel::NameRole).toString();

        QRect cell = opt.rect;
        QRect imgRect = cell.adjusted(2, 2, -2, -2);
        imgRect.setHeight(*m_thumbSize);

        // 背景 + 选中框（单选/多选都显示——isSelected 比 opt.state 可靠）
        const bool selected = m_grid && m_grid->selectionModel()->isSelected(idx);
        if (selected) {
            p->fillRect(cell, QColor(40, 60, 90));
            p->setPen(QPen(QColor(90, 160, 230), 2));   // 选中框
            p->drawRect(imgRect.adjusted(1, 1, -1, -1));
        } else if (opt.state & QStyle::State_MouseOver) {
            p->fillRect(cell, QColor(35, 35, 35));
        } else {
            p->fillRect(cell, QColor(17, 17, 17));
        }

        // 缩略图（保持比例居中）
        if (!pix.isNull()) {
            QSize fit = pix.size();
            fit.scale(imgRect.size(), Qt::KeepAspectRatio);
            QPoint pos = imgRect.center() - QPoint(fit.width() / 2, fit.height() / 2);
            p->drawPixmap(QRect(pos, fit), pix);
        }

        // 高亮边框（标签命中）
        if (hl) {
            p->setPen(QPen(QColor(216, 161, 75), 2));
            p->drawRect(imgRect.adjusted(1, 1, -1, -1));
        }

        // QC 固定标已在缩略图内嵌 emoji（applyImageThumb 绘制）——delegate 不再画 "!"

        // 文件名
        QRect textRect = cell.adjusted(4, *m_thumbSize + 6, -4, -2);
        p->setPen(QColor(170, 170, 170));
        p->setFont(QFont(p->font().family(), 9));
        QFontMetrics fm(p->font());
        p->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
                    fm.elidedText(name, Qt::ElideMiddle, textRect.width()));

        p->restore();
    }

private:
    ThumbModel *m_model;
    int *m_thumbSize;
    ThumbnailGrid *m_grid;
};

// ── 解码任务（线程池）──

// 剥离 PNG 的 iCCP chunk：Qt 6.11 无法禁用 ICC 解析，损坏的 ICC profile
// 会触发 qicc.cpp 内部断言（debug 构建 QString 崩溃，qlens_crash.log 已证实）。
// 剥离后 PNG 依然合法，按原始 RGB 解码，无警告、无解析风险。
static QImage loadImageNoIcc(const QString &path, int maxPx)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray data = f.readAll();
    // 有 iCCP 被剥离 → 从内存深拷贝解码（QImage::fromData 会拷贝数据，无悬垂）
    if (data.startsWith("\x89PNG\r\n\x1a\n")) {
        bool stripped = false;
        QByteArray cleaned;
        qsizetype pos = 8;
        while (pos + 8 <= data.size()) {
            const quint32 len = qFromBigEndian<quint32>(data.constData() + pos);
            const qsizetype total = 12 + len;
            if (pos + total > data.size()) break;
            if (data.mid(pos + 4, 4) == "iCCP") {
                stripped = true;
                cleaned = data.left(8);
                break;
            }
            pos += total;
        }
        if (stripped) {
            pos = 8;
            while (pos + 8 <= data.size()) {
                const quint32 len = qFromBigEndian<quint32>(data.constData() + pos);
                const QByteArray type = data.mid(pos + 4, 4);
                const qsizetype total = 12 + len;
                if (pos + total > data.size()) break;
                if (type != "iCCP")
                    cleaned += data.mid(pos, total);
                pos += total;
            }
            QImage img = QImage::fromData(cleaned);
            if (!img.isNull() && maxPx > 0)
                img = img.scaled(maxPx, maxPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            return img;
        }
    }
    // 无 iCCP：直接读文件路径（QImageReader 自管文件生命周期，无悬垂）
    QImageReader rd(path);
    rd.setAutoTransform(true);
    QSize orig = rd.size();
    if (orig.isValid() && orig.width() > 0 && maxPx > 0)
        rd.setScaledSize(orig.scaled(maxPx, maxPx, Qt::KeepAspectRatio));
    QImage img = rd.read();
    if (!img.isNull()) return img;

    // QImageReader 失败（SVG/插件格式等）→ 回退到 qlens_decode 统一解码
    // 需要 plugins 已加载（SVG 等），Manager 启动时加载
    return QLensCore::decodeImage(path, maxPx);
}

class ThumbDecodeTask : public QRunnable {
public:
    ThumbDecodeTask(QPointer<ThumbnailGrid> grid, QString path, int row, int ts, int token)
        : m_grid(grid), m_path(std::move(path)), m_row(row), m_ts(ts), m_token(token) {}

    void run() override {
        // 先查缓存（worker 线程，ThumbnailCache 已加锁线程安全）——命中则跳过解码
        QByteArray cached = ThumbnailCache::get(m_path, m_ts);
        if (!cached.isEmpty()) {
            QImage cimg;
            if (cimg.loadFromData(cached)) {
                QMetaObject::invokeMethod(m_grid, [g = m_grid, r = m_row, p = m_path, im = cimg, t = m_token]() {
                    if (g) g->applyImageThumb(r, p, im, t);
                }, Qt::QueuedConnection);
                return;
            }
        }
        // 剥离损坏 ICC 后解码（ComfyUI PNG 触发 qicc 断言）
        QImage img = loadImageNoIcc(m_path, m_ts);
        if (img.isNull()) return;
        // 传 QImage 回主线程（QPixmap 只能 GUI 线程创建）；token 防切目录竞态
        QMetaObject::invokeMethod(m_grid, [g = m_grid, r = m_row, p = m_path, im = img, t = m_token]() {
            if (g) g->applyImageThumb(r, p, im, t);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<ThumbnailGrid> m_grid;
    QString m_path;
    int m_row, m_ts, m_token;
};

// ── 文件夹拼图任务 ──

class FolderPreviewTask : public QRunnable {
public:
    FolderPreviewTask(QPointer<ThumbnailGrid> grid, QString folder, int row, int ts, int token)
        : m_grid(grid), m_folder(std::move(folder)), m_row(row), m_ts(ts), m_token(token) {}

    void run() override {
        // 文件夹拼图缓存（key=文件夹路径 + 文件夹 mtime）——命中则跳过扫描/生成
        QByteArray cached = ThumbnailCache::get(m_folder, m_ts);
        if (!cached.isEmpty()) {
            QImage cimg;
            if (cimg.loadFromData(cached)) {
                QMetaObject::invokeMethod(m_grid, [g = m_grid, r = m_row, im = cimg, t = m_token]() {
                    if (g) g->applyFolderThumb(r, im, t);
                }, Qt::QueuedConnection);
                return;
            }
        }
        // 只取前 4 个图片文件名（QDirIterator 前 4 个匹配即停，避免枚举整个目录）
        QStringList previews;
        QDirIterator it(m_folder,
                        {"*.jpg","*.jpeg","*.png","*.webp","*.bmp","*.gif"},
                        QDir::Files | QDir::Readable);
        while (it.hasNext() && previews.size() < 4) {
            it.next();
            previews << it.fileName();
        }

        // 全程用 QImage（线程安全）；QPixmap 只在主线程回调里创建
        QImage collage(m_ts, m_ts, QImage::Format_ARGB32_Premultiplied);
        collage.fill(Qt::transparent);
        QPainter p(&collage);
        p.setRenderHint(QPainter::Antialiasing);

        // 自绘文件夹轮廓
        qreal w = m_ts, h = m_ts;
        QColor base(232, 179, 75), dark(190, 140, 40);
        QPainterPath back;
        back.addRoundedRect(QRectF(w*0.02, h*0.18, w*0.96, h*0.78), w*0.05, w*0.05);
        QPainterPath tab;
        tab.moveTo(w*0.02, h*0.18);
        tab.lineTo(w*0.02, h*0.10);
        tab.lineTo(w*0.42, h*0.10);
        tab.lineTo(w*0.50, h*0.18);
        tab.closeSubpath();
        p.fillPath(tab, base);
        p.setPen(QPen(dark, w*0.01));
        p.drawPath(tab);
        p.fillPath(back, base);
        p.drawPath(back);

        // 内页预览
        QRectF inner(w*0.12, h*0.26, w*0.76, h*0.62);
        int sample = std::min(4, (int)previews.size());
        if (sample > 0) {
            int cellCnt = (sample == 1) ? 1 : 2;
            qreal cellW = inner.width() / cellCnt;
            qreal cellH = inner.height() / (cellCnt == 1 ? 1 : 2);
            // 预览目标尺寸（cell 大小，最多 ~half ts）—— 用 setScaledSize 限流，
            // 避免解码 36MB 巨型 PNG 撑爆内存（之前崩溃根因）
            int targetPx = qMax(1, (int)(qMax(cellW, cellH) * 1.5));
            int decoded = 0;
            for (int pi = 0; pi < sample; ++pi) {
                // 剥离损坏 ICC 后解码（同 ThumbDecodeTask）
                QImage img = loadImageNoIcc(m_folder + "/" + previews[pi], targetPx);
                if (img.isNull()) continue;
                ++decoded;
                int col = pi % cellCnt, row = pi / cellCnt;
                QRectF cell(inner.x() + col*cellW, inner.y() + row*cellH, cellW, cellH);
                QSize fit = img.size();
                fit.scale(cell.size().toSize(), Qt::KeepAspectRatio);
                QPointF pos = cell.center() - QPointF(fit.width()/2.0, fit.height()/2.0);
                p.drawImage(QRectF(pos, fit), img);
            }
        }
        p.end();

        // 缓存文件夹拼图（JPEG）——下次启动直接读，不再扫描/生成
        {
            QByteArray fbytes;
            QBuffer buf(&fbytes);
            buf.open(QIODevice::WriteOnly);
            collage.save(&buf, "JPEG", 85);
            if (!fbytes.isEmpty()) ThumbnailCache::put(m_folder, m_ts, fbytes);
        }

        // 回线程池执行：QImage 线程安全，投递主线程收结果（QPixmap 只在主线程创建）
        QMetaObject::invokeMethod(m_grid, [g = m_grid, r = m_row, im = collage, t = m_token]() {
            if (g) g->applyFolderThumb(r, im, t);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<ThumbnailGrid> m_grid;
    QString m_folder;
    int m_row, m_ts, m_token;
};


// ── 目录扫描任务（W2 IO 线程池）──
// 返回当前文件夹的子目录 + 图片文件（QStringList 值类型，线程安全）

class ScanTask : public QRunnable {
public:
    // Result 定义在头文件（ScanResult）
    ScanTask(QPointer<ThumbnailGrid> grid, QString path, int token)
        : m_grid(grid), m_path(std::move(path)), m_token(token) {}

    void run() override {
        QDir dir(m_path);
        dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
        ScanResult r;
        r.subDirs = dir.entryList();

        QStringList filters;
        for (auto &fmt : QImageReader::supportedImageFormats())
            filters << ("*." + QString::fromLatin1(fmt));
        filters << "*.cr2" << "*.cr3" << "*.nef" << "*.arw" << "*.dng" << "*.rw2";
        // qlens 解码/插件支持的格式（QImageReader 不覆盖）
        filters << "*.heic" << "*.heif" << "*.avif" << "*.svg" << "*.svgz" << "*.jxr";
        dir.setNameFilters(filters);
        dir.setFilter(QDir::Files | QDir::Readable);
        r.imageFiles = dir.entryList();

        QMetaObject::invokeMethod(m_grid, [g = m_grid, res = r, t = m_token]() {
            if (g) g->applyScanResult(res, t);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<ThumbnailGrid> m_grid;
    QString m_path;
    int m_token;
};

// ── 网格视图实现 ──

ThumbnailGrid::ThumbnailGrid(TagStore *store, QWidget *parent)
    : QListView(parent), m_store(store) {
    m_model = new ThumbModel(this);
    setModel(m_model);

    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setSelectionMode(QAbstractItemView::ExtendedSelection);  // Ctrl 单选 / Shift 连选 / 拉框多选
    // 不设 uniformItemSizes：Qt 6.11.1 下它与自定义 delegate sizeHint 组合疑似导致 item 点击
    // 交互异常（pressedIndex 持久化失效 → 单击不选中/clicked 不发/release 清空）
    setSpacing(2);
    setGridSize(QSize(thumbCellSize(), thumbCellSize()));
    setItemDelegate(new ThumbDelegate(m_model, &m_thumbSize, this));
    setStyleSheet("QListView{background:#111; border:none; color:#aaa;}");
    setEditTriggers(QAbstractItemView::EditKeyPressed);  // F2 触发内联编辑

    // 拖放：只接受拖入（复制到当前文件夹）；拖出（dragEnabled）疑似导致 Qt item 交互失效（press 进拖拽预检→pressedIndex 不建立）——禁用拖出
    setDragEnabled(false);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DropOnly);

    connect(this, &QListView::doubleClicked, [this](const QModelIndex &idx) {
        if (!idx.isValid()) return;
        const ThumbItem &it = m_model->itemAt(idx.row());
        if (it.isDir) emit folderDoubleClicked(it.path);
        else          emit imageDoubleClicked(it.path);
    });
    connect(this, &QListView::clicked, [this](const QModelIndex &idx) {
        if (!idx.isValid()) return;
        const ThumbItem &it = m_model->itemAt(idx.row());
        if (!it.isDir) emit imageClicked(it.path);
    });
}

// 右键菜单（打开/复制/删除——不再是左键行为）
void ThumbnailGrid::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid()) return;
    const ThumbItem &it = m_model->itemAt(idx.row());

    QMenu menu(this);
    QAction *open = menu.addAction(T(L"在查看器中打开"));
    QAction *saveAs = menu.addAction(T(L"另存为..."));
    menu.addSeparator();
    QAction *cp   = menu.addAction(T(L"复制"));
    QAction *ren  = menu.addAction(T(L"重命名 (F2)"));
    menu.addSeparator();
    QAction *del  = menu.addAction(T(L"删除（回收站）"));
    menu.addSeparator();
    QAction *batchConvAct   = menu.addAction(T(L"批量转换格式..."));
    QAction *batchResizeAct = menu.addAction(T(L"批量调整大小..."));
    QAction *batchRenameAct = menu.addAction(T(L"批量重命名..."));
    QAction *batchAddTagAct = menu.addAction(T(L"批量添加标签..."));
    QAction *batchRmTagAct  = menu.addAction(T(L"批量移除标签..."));
    QAction *sel  = menu.exec(event->globalPos());

    if (sel == open) {
        if (!it.isDir) emit imageDoubleClicked(it.path);
    } else if (sel == saveAs) {
        saveAsDialog(it.path);
    } else if (sel == ren) {
        renameSelected();
    } else if (sel == cp) {
        // 三合一复制（与 QuickView 一致）：文件 + 路径 + 图片
        QImage img = QLensCore::decodeImage(it.path);
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(it.path)});   // 文件（拖放/粘贴）
        mime->setText(it.path);                           // 路径文本
        if (!img.isNull()) mime->setImageData(img);       // 图片位图
        QApplication::clipboard()->setMimeData(mime);
    } else if (sel == del) {
        QFile::moveToTrash(it.path);
        loadFolder(m_currentFolder);
    } else if (sel == batchConvAct) {
        batchConvert();
    } else if (sel == batchResizeAct) {
        batchResize();
    } else if (sel == batchRenameAct) {
        batchRename();
    } else if (sel == batchAddTagAct) {
        batchAddTags(it.path);
    } else if (sel == batchRmTagAct) {
        batchRemoveTags(it.path);
    }
}

// ── 右键批量功能（ACDSEE 风格）──

// 批量添加标签（选中集 + 右键项；逗号分隔）
void ThumbnailGrid::batchAddTags(const QString &rightClickPath)
{
    QStringList paths = selectedImagePaths();
    if (!paths.contains(rightClickPath)) paths << rightClickPath;
    if (paths.isEmpty()) return;
    bool ok = false;
    QString text = QInputDialog::getText(this, T(L"批量添加标签"),
        T(L"输入标签（逗号分隔，添加到 %1 张图）：").arg(paths.size()),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty()) return;
    int n = 0;
    for (const QString &p : paths) {
        for (const QString &t : text.split(',', Qt::SkipEmptyParts)) {
            QString s = t.trimmed();
            if (s.isEmpty()) continue;
            m_store->addImageTag(QFileInfo(p).fileName(), s);
            ++n;
        }
    }
    emit tagsChanged();
}

// 批量移除标签（选中集 + 右键项；逗号分隔）
void ThumbnailGrid::batchRemoveTags(const QString &rightClickPath)
{
    QStringList paths = selectedImagePaths();
    if (!paths.contains(rightClickPath)) paths << rightClickPath;
    if (paths.isEmpty()) return;
    bool ok = false;
    QString text = QInputDialog::getText(this, T(L"批量移除标签"),
        T(L"输入要移除的标签（逗号分隔，作用于 %1 张图）：").arg(paths.size()),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty()) return;
    int n = 0;
    for (const QString &p : paths) {
        for (const QString &t : text.split(',', Qt::SkipEmptyParts)) {
            QString s = t.trimmed();
            if (s.isEmpty()) continue;
            m_store->removeImageTag(QFileInfo(p).fileName(), s);
            ++n;
        }
    }
    emit tagsChanged();
}
// 当前文件夹全部图片路径（按网格显示顺序）
QStringList ThumbnailGrid::allImagePaths() const
{
    QStringList paths;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const ThumbItem &it = m_model->itemAt(i);
        if (!it.isDir) paths << it.path;
    }
    return paths;
}

// 图片数量（不含子目录）——状态栏用
int ThumbnailGrid::imageCount() const
{
    int c = 0;
    for (int i = 0; i < m_model->rowCount(); ++i)
        if (!m_model->itemAt(i).isDir) ++c;
    return c;
}

// 另存为：QImage 解码 → 保存为选择的格式（扩展名决定）
void ThumbnailGrid::saveAsDialog(const QString &path)
{
    QFileInfo fi(path);
    QString def = fi.path() + "/" + fi.completeBaseName() + "_copy.png";
    QString dst = QFileDialog::getSaveFileName(this, T(L"另存为"), def,
        T(L"PNG (*.png);;JPEG (*.jpg *.jpeg);;WebP (*.webp);;BMP (*.bmp)"));
    if (dst.isEmpty()) return;
    QImage img = QLensCore::decodeImage(path);
    if (img.isNull()) { QMessageBox::warning(this, T(L"另存为"), T(L"无法解码图片")); return; }
    if (!img.save(dst))
        QMessageBox::warning(this, T(L"另存为"), T(L"保存失败"));
}

// 批量转换格式：选目标格式 + 输出目录 → 全部转换
void ThumbnailGrid::batchConvert()
{
    bool ok = false;
    QString fmt = QInputDialog::getItem(this, T(L"批量转换格式"), T(L"目标格式："),
        {QStringLiteral("PNG"), QStringLiteral("JPEG"), QStringLiteral("WebP"), QStringLiteral("BMP")},
        0, false, &ok);
    if (!ok) return;
    QString outDir = QFileDialog::getExistingDirectory(this, T(L"选择输出目录"), m_currentFolder);
    if (outDir.isEmpty()) return;

    const QStringList paths = allImagePaths();
    QString ext = fmt.toLower() == "jpeg" ? "jpg" : fmt.toLower();
    int done = 0;
    for (const QString &src : paths) {
        QImage img = QLensCore::decodeImage(src);
        if (img.isNull()) continue;
        QString out = QDir(outDir).filePath(QFileInfo(src).completeBaseName() + "." + ext);
        if (img.save(out)) done++;
    }
    QMessageBox::information(this, T(L"批量转换格式"),
        QString("%1/%2").arg(done).arg(paths.size()));
}

// 批量调整大小：目标最长边像素 → 输出目录（不覆盖原图）
void ThumbnailGrid::batchResize()
{
    bool ok = false;
    int maxSide = QInputDialog::getInt(this, T(L"批量调整大小"), T(L"目标最长边（像素）："),
        1920, 16, 32768, 1, &ok);
    if (!ok) return;
    QString outDir = QFileDialog::getExistingDirectory(this, T(L"选择输出目录"), m_currentFolder);
    if (outDir.isEmpty()) return;

    const QStringList paths = allImagePaths();
    int done = 0;
    for (const QString &src : paths) {
        QImage img = QLensCore::decodeImage(src);
        if (img.isNull()) continue;
        QSize s = img.size();
        if (s.width() > maxSide || s.height() > maxSide)
            img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QString out = QDir(outDir).filePath(QFileInfo(src).fileName());
        if (img.save(out)) done++;
    }
    QMessageBox::information(this, T(L"批量调整大小"),
        QString("%1/%2").arg(done).arg(paths.size()));
}

// 批量重命名：模板 + 起始序号（保留原扩展名），ACDSEE 风格
void ThumbnailGrid::batchRename()
{
    bool ok = false;
    QString tmpl = QInputDialog::getText(this, T(L"批量重命名"),
        T(L"名称模板（如 photo_、IMG_）："), QLineEdit::Normal, QStringLiteral("photo_"), &ok);
    if (!ok) return;
    int start = QInputDialog::getInt(this, T(L"批量重命名"), T(L"起始序号："), 1, 0, 999999, 1, &ok);
    if (!ok) return;

    const QStringList paths = selectedImagePaths().isEmpty()
        ? allImagePaths()   // 无选中 → 全部
        : selectedImagePaths();
    int n = start;
    int done = 0;
    for (const QString &src : paths) {
        QString ext = QFileInfo(src).suffix();
        QString newName = QString("%1%2.%3").arg(tmpl).arg(n++, 4, 10, QLatin1Char('0')).arg(ext);
        QString dst = QDir(m_currentFolder).filePath(newName);
        if (QFileInfo(dst).exists()) continue;
        if (QFile::rename(src, dst)) done++;
    }
    if (done > 0) loadFolder(m_currentFolder);
    QMessageBox::information(this, T(L"批量重命名"),
        QString("%1/%2").arg(done).arg(paths.size()));
}

// 选中的图片路径（不含目录）
QStringList ThumbnailGrid::selectedImagePaths() const
{
    QStringList paths;
    for (const QModelIndex &idx : selectionModel()->selectedIndexes()) {
        if (idx.isValid() && idx.row() >= 0 && idx.row() < m_model->rowCount()) {
            const ThumbItem &it = m_model->itemAt(idx.row());
            if (!it.isDir) paths << it.path;
        }
    }
    return paths;
}

// 重命名：单选=内联编辑（资源管理器风格）；多选=批量重命名（选中项）
void ThumbnailGrid::renameSelected()
{
    QStringList sel = selectedImagePaths();
    if (sel.isEmpty()) return;
    if (sel.size() == 1) {
        // 内联编辑：直接在文件名位置编辑（delegate createEditor/setModelData 处理改文件）
        QModelIndexList idxs = selectionModel()->selectedIndexes();
        for (const QModelIndex &ix : idxs) {
            if (ix.isValid() && !m_model->itemAt(ix.row()).isDir) {
                setCurrentIndex(ix);
                edit(ix);
                break;
            }
        }
    } else {
        batchRename();  // 多选 → 批量重命名（选中项）
    }
}

// F2 重命名；Delete 删除到回收站
void ThumbnailGrid::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_F2) {
        renameSelected();
        return;
    }
    if (e->key() == Qt::Key_Delete) {
        QStringList sel = selectedImagePaths();
        for (const QString &p : sel) QFile::moveToTrash(p);
        if (!sel.isEmpty()) loadFolder(m_currentFolder);
        return;
    }
    QListView::keyPressEvent(e);
}

// 慢双击重命名（Windows 资源管理器逻辑）：间隔超过标准双击 + 第二下点在文件名区域
void ThumbnailGrid::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        QModelIndex idx = indexAt(e->pos());
        if (idx.isValid() && selectionModel()->isSelected(idx)) {
            // 文件名区域（同 delegate 绘制 textRect：图标下方）
            QRect cell = visualRect(idx);
            QRect textRect = cell.adjusted(4, m_thumbSize + 6, -4, -2);
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (textRect.contains(e->pos()) &&
                m_lastClickRow == idx.row() && now - m_lastClickTime > 500) {
                m_lastClickRow = -1;
                renameSelected();
                return;  // 不传给 QListView（避免打开）
            }
            m_lastClickRow = idx.row();
            m_lastClickTime = now;
        }
    }
    QListView::mousePressEvent(e);
}

void ThumbnailGrid::mouseReleaseEvent(QMouseEvent *e)
{
    QListView::mouseReleaseEvent(e);
}

void ThumbnailGrid::mouseDoubleClickEvent(QMouseEvent *e)
{
    QModelIndex idx = indexAt(e->pos());
    if (idx.isValid() && e->button() == Qt::LeftButton) {
        // 手动发双击（Qt 的 doubleClicked 状态机在此 Qt 版本下不可靠——pressedIndex 失效）
        const ThumbItem &it = m_model->itemAt(idx.row());
        if (it.isDir) emit folderDoubleClicked(it.path);
        else          emit imageDoubleClicked(it.path);
        return;
    }
    QListView::mouseDoubleClickEvent(e);
}

// 拖出：选中项 → 文件 URL（拖到资源管理器 = 复制）
QMimeData *ThumbModel::mimeData(const QModelIndexList &indexes) const
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QModelIndex &idx : indexes) {
        if (idx.isValid() && idx.row() >= 0 && idx.row() < (int)m_items.size())
            urls << QUrl::fromLocalFile(m_items.at(idx.row()).path);
    }
    mime->setUrls(urls);
    return mime;
}

// 拖入：允许 url 拖放
void ThumbnailGrid::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void ThumbnailGrid::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

// 拖入：图片/文件夹复制到当前文件夹（参考 Windows 资源管理器：同盘=移动，跨盘=复制）
void ThumbnailGrid::dropEvent(QDropEvent *e)
{
    if (m_currentFolder.isEmpty() || !e->mimeData()->hasUrls()) return;
    int done = 0;
    QString curRoot = QDir(m_currentFolder).rootPath();
    for (const QUrl &url : e->mimeData()->urls()) {
        QString src = url.toLocalFile();
        if (src.isEmpty()) continue;
        QFileInfo fi(src);
        QString dst = QDir(m_currentFolder).filePath(fi.fileName());
        if (QFileInfo(dst).exists() || src == dst) continue;
        bool sameDrive = (QDir(src).rootPath() == curRoot);
        if (fi.isDir()) {
            if (sameDrive) { if (QDir(src).rename(src, dst)) done++; }
            else           { if (copyDirRecursive(src, dst)) done++; }
        } else {
            if (sameDrive) { if (QFile::rename(src, dst)) done++; }
            else           { if (QFile::copy(src, dst)) done++; }
        }
    }
    if (done > 0) loadFolder(m_currentFolder);
}

// 跨盘文件夹递归复制
bool copyDirRecursive(const QString &srcDir, const QString &dstDir)
{
    QDir dst(dstDir);
    if (!dst.mkpath(dstDir)) return false;
    for (const QString &name : QDir(srcDir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString src = QDir(srcDir).filePath(name);
        QString dstPath = QDir(dstDir).filePath(name);
        if (QFileInfo(src).isDir()) {
            if (!copyDirRecursive(src, dstPath)) return false;
        } else {
            if (!QFile::copy(src, dstPath)) return false;
        }
    }
    return true;
}

ThumbnailGrid::~ThumbnailGrid() {
    m_loadToken++;
    QThreadPool::globalInstance()->clear();
}

// 选中指定图片（pending 到扫描完成；扫描完成在 applyScanResult 处理）
void ThumbnailGrid::selectImage(const QString &path) {
    m_pendingSelect = path;
}

void ThumbnailGrid::loadFolder(const QString &path) {
    // 切页瞬间宽度可能未就绪，延迟重排
    if (width() <= 0) {
        QTimer::singleShot(10, this, [this, path]{ loadFolder(path); });
        return;
    }
    m_store->open(path);   // 切换 TagStore 绑定到当前文件夹的 qltag.db
    m_currentFolder = path;
    m_loadToken++;
    // 丢弃所有排队中的旧任务（运行中的靠 token 丢弃回调）
    QThreadPool::globalInstance()->clear();
    m_filterTag.clear();  // 切文件夹重置过滤
    m_filterTags.clear();
    m_filterQc.clear();

    // 目录扫描移到 W2 worker 线程（大目录枚举不阻塞 UI）
    int token = m_loadToken;
    QThreadPool::globalInstance()->start(new ScanTask(this, path, token));
}

// W2 扫描结果回主线程：构建 items + 提交解码任务
void ThumbnailGrid::applyScanResult(const ScanResult &res, int token) {
    if (token != m_loadToken) return;
    m_subDirs = res.subDirs;
    m_imageFiles = res.imageFiles;

    // 全量 items（供过滤重显）
    QFileIconProvider ip;
    QIcon folderIcon = ip.icon(QFileIconProvider::Folder);
    QIcon fileIcon   = ip.icon(QFileIconProvider::File);

    m_allItems.clear();
    m_allItems.reserve(m_subDirs.size() + m_imageFiles.size());
    for (const QString &d : m_subDirs)
        m_allItems.push_back({d, m_currentFolder + "/" + d, true, folderIcon.pixmap(m_thumbSize, m_thumbSize), false, false});
    for (const QString &f : m_imageFiles) {
        // 缩略图生成前：用 icons/ 的格式图标（JPG.ico/PNG.ico...），无匹配回退系统图标
        QIcon ic = FileIcons::iconForExt(QFileInfo(f).suffix());
        if (ic.isNull()) ic = fileIcon;
        m_allItems.push_back({f, m_currentFolder + "/" + f, false, ic.pixmap(m_thumbSize, m_thumbSize), false, false});
    }

    applyFilter();

    // 批量预查 QC 标签（文件名 → QC 标签）：一次 JOIN 替代逐图 tagsForImage（UI 卡主因）
    m_qcTagMap = TagStore::queryFolderQcMap(m_currentFolder);

    // 高亮标签预查（单条 SQL 拿所有命中文件名，替代逐图查询）
    if (!m_highlightTag.isEmpty()) {
        QStringList hitFiles = TagStore::queryFilesWithTag(m_currentFolder, m_highlightTag);
        QSet<QString> hitSet(hitFiles.begin(), hitFiles.end());
        for (const ThumbItem &it : m_allItems) {
            if (it.isDir) continue;
            if (hitSet.contains(it.name)) {
                int row = findModelRow(it.path);
                if (row >= 0) m_model->updateHighlight(row, true);
            }
        }
    }

    // 缩略图生成/缓存尺寸固定 320：不随滑块变（10W 图缓存体积可控），显示时 delegate 缩放
    // 缓存键含 size——尺寸策略再变时旧缓存自动失效
    int ts = 320;
    QThreadPool *pool = QThreadPool::globalInstance();
    int dirCount = (int)m_subDirs.size();

    // 文件夹预览任务（线程池并行，QImage 线程安全）
    for (int di = 0; di < dirCount; ++di)
        pool->start(new FolderPreviewTask(this, m_allItems[di].path, di, ts, token));

    // 图片缩略图：全部提交解码任务（worker 内先查缓存，主线程不再碰 SQLite）
    for (int i = 0; i < (int)m_imageFiles.size(); ++i) {
        int row = dirCount + i;
        pool->start(new ThumbDecodeTask(this, m_allItems[row].path, row, ts, token));
    }
}

// 按路径在模型可见行中查找
int ThumbnailGrid::findModelRow(const QString &path) const {
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (m_model->itemAt(i).path == path) return i;
    }
    return -1;
}

// 应用过滤：重建模型可见项
void ThumbnailGrid::applyFilter() {
    QList<ThumbItem> visible;
    // 多 tag AND 筛选（m_filterTags 非空则全部命中）；固定标筛选叠加
    if (m_filterTags.isEmpty() && m_filterQc.isEmpty()) {
        visible = m_allItems;
    } else {
        visible.reserve(m_allItems.size());
        for (const ThumbItem &it : m_allItems) {
            if (it.isDir) continue;  // 过滤时隐藏文件夹
            QStringList tags = m_store->tagsForImage(it.name);
            bool ok = true;
            for (const QString &ft : m_filterTags)
                if (!tags.contains(ft)) { ok = false; break; }
            if (!ok) continue;
            if (!m_filterQc.isEmpty() && !tags.contains(m_filterQc)) continue;
            visible.push_back(it);
        }
    }
    m_model->setItems(visible);
    // 待选中图片（QuickView 传入）：选中 + 滚动到可见
    if (!m_pendingSelect.isEmpty()) {
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const ThumbItem &it = m_model->itemAt(row);
            if (!it.isDir && it.path == m_pendingSelect) {
                QModelIndex idx = m_model->index(row);
                setCurrentIndex(idx);
                scrollTo(idx, QAbstractItemView::PositionAtCenter);
                break;
            }
        }
        m_pendingSelect.clear();
    }
}

void ThumbnailGrid::setFilterTag(const QString &tag) {
    m_filterTag = tag.trimmed();
    // 逗号分隔多 tag = AND 组合筛选
    m_filterTags.clear();
    for (const QString &t : m_filterTag.split(',', Qt::SkipEmptyParts)) {
        QString s = t.trimmed();
        if (!s.isEmpty()) m_filterTags << s;
    }
    if (m_currentFolder.isEmpty()) return;
    applyFilter();
}

// 固定标筛选（与 tag 筛选 AND 组合）
void ThumbnailGrid::setFilterQc(const QString &qc) {
    m_filterQc = qc.trimmed();
    if (m_currentFolder.isEmpty()) return;
    applyFilter();
}

// 重新加载当前文件夹（QC 检测后刷新缩略图/角标）
void ThumbnailGrid::refreshCurrentFolder() {
    if (m_currentFolder.isEmpty()) return;
    loadFolder(m_currentFolder);
}

QStringList ThumbnailGrid::folderTags() const {
    // 一条 DISTINCT 查询（替代逐图查询——4 万图 = 4 万查询的性能黑洞）
    QStringList result = TagStore::queryFolderTags(m_currentFolder);
    result.sort(Qt::CaseInsensitive);
    return result;
}

void ThumbnailGrid::setHighlightTag(const QString &tag) {
    m_highlightTag = tag;
    if (m_currentFolder.isEmpty()) return;
    // 重新应用高亮
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const ThumbItem &it = m_model->itemAt(i);
        if (it.isDir) continue;
        bool hit = !tag.isEmpty() && m_store->tagsForImage(it.name).contains(tag);
        m_model->updateHighlight(i, hit);
    }
}

void ThumbnailGrid::applyImageThumb(int row, const QString &path, const QImage &img, int token) {
    // 过期任务丢弃：用户已切到别的文件夹，此回调不再有效
    if (token != m_loadToken) return;
    // 主线程：QPixmap 在这里创建（QPixmap 只能 GUI 线程用）
    QPixmap pix = QPixmap::fromImage(img);
    // 写缓存
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    pix.save(&buf, "PNG");
    ThumbnailCache::put(path, 320, bytes);

    // 过滤后行号可能变化，按路径定位
    int visibleRow = findModelRow(path);
    if (visibleRow < 0) return;  // 该图被过滤隐藏，不显示

    // 固定标（QC）emoji：收集该图命中的固定标，画到缩略图右上角（从右往左）
    // 用批量预查 map（m_qcTagMap）——不逐图查 SQLite
    QStringList emojis;
    {
        const QStringList tags = m_qcTagMap.value(QFileInfo(path).fileName());
        for (auto it = qcEmojiMap().constBegin(); it != qcEmojiMap().constEnd(); ++it)
            if (tags.contains(it.key())) emojis << it.value();
    }
    if (!emojis.isEmpty()) {
        QPixmap display = pix;
        paintQcEmojis(display, emojis);
        m_model->updatePix(visibleRow, display);
        // 同步全量缓存（筛选/取消筛选后不丢缩略图）
        for (ThumbItem &ai : m_allItems)
            if (!ai.isDir && ai.path == path) { ai.pix = display; break; }
        return;
    }
    m_model->updatePix(visibleRow, pix);
    // 同步全量缓存（筛选/取消筛选后不丢缩略图）
    for (ThumbItem &ai : m_allItems)
        if (!ai.isDir && ai.path == path) { ai.pix = pix; break; }
}

// 把 QC emoji 角标画到缩略图右上角（从右往左）
void ThumbnailGrid::paintQcEmojis(QPixmap &display, const QStringList &emojis)
{
    QPainter p(&display);
    QFont emojiFont(QStringLiteral("Segoe UI Emoji"));
    int sz = qMax(7, display.width() / 12);   // emoji 角标——缩小一倍（6→12 分母）
    emojiFont.setPixelSize(sz);
    p.setFont(emojiFont);
    int x = display.width() - 2;
    for (int i = emojis.size() - 1; i >= 0; --i) {
        QFontMetrics fm(emojiFont);
        x -= fm.horizontalAdvance(emojis[i]);
        p.drawText(QPointF(x, sz + 1), emojis[i]);
        x -= 2;
    }
    p.end();
}

// QC 检测打标后：只重画现有缩略图的 QC emoji 角标（不重扫——大目录不卡 UI）
void ThumbnailGrid::refreshQcBadges()
{
    if (m_currentFolder.isEmpty()) return;
    m_qcTagMap = TagStore::queryFolderQcMap(m_currentFolder);
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const ThumbItem &it = m_model->itemAt(row);
        if (it.isDir || it.pix.isNull()) continue;
        const QStringList tags = m_qcTagMap.value(it.name);
        QStringList emojis;
        for (auto it2 = qcEmojiMap().constBegin(); it2 != qcEmojiMap().constEnd(); ++it2)
            if (tags.contains(it2.key())) emojis << it2.value();
        if (emojis.isEmpty()) continue;
        QPixmap display = it.pix;
        paintQcEmojis(display, emojis);
        m_model->updatePix(row, display);
        for (ThumbItem &ai : m_allItems)
            if (!ai.isDir && ai.path == it.path) { ai.pix = display; break; }
    }
}

void ThumbnailGrid::applyFolderThumb(int row, const QImage &img, int token) {
    // 过期任务丢弃：用户已切到别的文件夹
    if (token != m_loadToken) return;
    // 过滤时文件夹隐藏，row 可能错位 → 按路径定位
    if (row < 0 || row >= (int)m_allItems.size()) return;
    int visibleRow = findModelRow(m_allItems[row].path);
    if (visibleRow >= 0)
        m_model->updatePix(visibleRow, QPixmap::fromImage(img));
    // 同步全量缓存（筛选/取消筛选后不丢文件夹拼图）
    m_allItems[row].pix = QPixmap::fromImage(img);
}

void ThumbnailGrid::setThumbSize(int size) {
    const int clamped = std::clamp(size, 32, 480);
    if (clamped == m_thumbSize) return;
    m_thumbSize = clamped;
    setGridSize(QSize(thumbCellSize(), thumbCellSize()));
    // 已加载缩略图按新尺寸重排（delegate 绘制时缩放）；超大图重解码由 applyImageThumb 按需触发
    emit thumbSizeChanged(clamped);
}

void ThumbnailGrid::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        int d = e->angleDelta().y() / 120;
        setThumbSize(m_thumbSize + d * 32);
        return;
    }
    QListView::wheelEvent(e);
}

void ThumbnailGrid::resizeEvent(QResizeEvent *e) {
    QListView::resizeEvent(e);
    if (!m_currentFolder.isEmpty() && e->size().width() != e->oldSize().width()) {
        // 宽度变化只影响列数，无需重载（Adjust 模式自动重排）
    }
}
