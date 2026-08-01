#include "ThumbnailGrid.h"
#include <QDir>
#include <QImageReader>
#include <QFile>
#include <QtEndian>
#include <QThreadPool>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFileIconProvider>
#include <QBuffer>
#include <QTimer>
#include <QStyledItemDelegate>
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
    explicit ThumbDelegate(ThumbModel *model, int *thumbSize, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_model(model), m_thumbSize(thumbSize) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(*m_thumbSize + 4, *m_thumbSize + 26);
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

        // 背景
        if (opt.state & QStyle::State_Selected)
            p->fillRect(cell, QColor(40, 60, 90));
        else if (opt.state & QStyle::State_MouseOver)
            p->fillRect(cell, QColor(35, 35, 35));
        else
            p->fillRect(cell, QColor(17, 17, 17));

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

        // QC 角标
        if (qcBadge && !pix.isNull()) {
            int sz = qMax(12, *m_thumbSize / 7);
            QRect badge(imgRect.right() - sz - 2, imgRect.top() + 2, sz, sz);
            p->fillRect(badge, QColor(200, 40, 40, 220));
            p->setPen(Qt::white);
            p->setFont(QFont(p->font().family(), sz * 2 / 3, QFont::Bold));
            p->drawText(badge, Qt::AlignCenter, "!");
        }

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
    // 非 PNG 或没有 iCCP → 直接走 QImageReader
    if (data.startsWith("\x89PNG\r\n\x1a\n")) {
        QByteArray cleaned = data.left(8);
        qsizetype pos = 8;
        while (pos + 8 <= data.size()) {
            const quint32 len = qFromBigEndian<quint32>(data.constData() + pos);
            const QByteArray type = data.mid(pos + 4, 4);
            const qsizetype total = 12 + len;
            if (pos + total > data.size()) break;  // 损坏，交给 Qt 处理
            if (type != "iCCP")
                cleaned += data.mid(pos, total);
            pos += total;
        }
        if (cleaned.size() > 8) data = cleaned;  // 有 chunk 才替换
    }
    QBuffer buf(&data);
    buf.open(QIODevice::ReadOnly);
    QImageReader rd(&buf, "png");
    rd.setAutoTransform(true);
    QSize orig = rd.size();
    if (orig.isValid() && orig.width() > 0 && maxPx > 0)
        rd.setScaledSize(orig.scaled(maxPx, maxPx, Qt::KeepAspectRatio));
    return rd.read();
}

class ThumbDecodeTask : public QRunnable {
public:
    ThumbDecodeTask(QPointer<ThumbnailGrid> grid, QString path, int row, int ts, int token)
        : m_grid(grid), m_path(std::move(path)), m_row(row), m_ts(ts), m_token(token) {}

    void run() override {
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
        QDir sub(m_folder);
        sub.setNameFilters({"*.jpg","*.jpeg","*.png","*.webp","*.bmp","*.gif"});
        sub.setFilter(QDir::Files | QDir::Readable);
        QStringList previews = sub.entryList();

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
            for (int pi = 0; pi < sample; ++pi) {
                // 剥离损坏 ICC 后解码（同 ThumbDecodeTask）
                QImage img = loadImageNoIcc(sub.path() + "/" + previews[pi], targetPx);
                if (img.isNull()) continue;
                int col = pi % cellCnt, row = pi / cellCnt;
                QRectF cell(inner.x() + col*cellW, inner.y() + row*cellH, cellW, cellH);
                QSize fit = img.size();
                fit.scale(cell.size().toSize(), Qt::KeepAspectRatio);
                QPointF pos = cell.center() - QPointF(fit.width()/2.0, fit.height()/2.0);
                p.drawImage(QRectF(pos, fit), img);
            }
        }
        p.end();

        QMetaObject::invokeMethod(m_grid, [g = m_grid, r = m_row, im = collage, t = m_token]() {
            if (g) g->applyFolderThumb(r, im, t);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<ThumbnailGrid> m_grid;
    QString m_folder;
    int m_row, m_ts, m_token;
};

// ── 网格视图实现 ──

ThumbnailGrid::ThumbnailGrid(TagStore *store, QWidget *parent)
    : QListView(parent), m_store(store) {
    m_model = new ThumbModel(this);
    setModel(m_model);

    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setSelectionMode(QListView::SingleSelection);
    setUniformItemSizes(true);          // 性能：布局 O(1)
    setSpacing(2);
    setGridSize(QSize(thumbCellSize(), thumbCellSize()));
    setItemDelegate(new ThumbDelegate(m_model, &m_thumbSize, this));
    setStyleSheet("QListView{background:#111; border:none; color:#aaa;}");

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

ThumbnailGrid::~ThumbnailGrid() {
    m_loadToken++;
    QThreadPool::globalInstance()->clear();
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

    QDir dir(path);
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    m_subDirs = dir.entryList();

    QStringList filters;
    for (auto &fmt : QImageReader::supportedImageFormats())
        filters << ("*." + QString::fromLatin1(fmt));
    filters << "*.cr2" << "*.cr3" << "*.nef" << "*.arw" << "*.dng" << "*.rw2";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::Readable);
    m_imageFiles = dir.entryList();

    // 全量 items（供过滤重显）
    QFileIconProvider ip;
    QIcon folderIcon = ip.icon(QFileIconProvider::Folder);
    QIcon fileIcon   = ip.icon(QFileIconProvider::File);

    m_allItems.clear();
    m_allItems.reserve(m_subDirs.size() + m_imageFiles.size());
    for (const QString &d : m_subDirs)
        m_allItems.push_back({d, path + "/" + d, true, folderIcon.pixmap(m_thumbSize, m_thumbSize), false, false});
    for (const QString &f : m_imageFiles)
        m_allItems.push_back({f, path + "/" + f, false, fileIcon.pixmap(m_thumbSize, m_thumbSize), false, false});

    applyFilter();

    // 高亮标签预查（主线程，SQLite 安全）
    if (!m_highlightTag.isEmpty()) {
        for (const ThumbItem &it : m_allItems) {
            if (it.isDir) continue;
            if (m_store->tagsForImage(it.name).contains(m_highlightTag)) {
                int row = findModelRow(it.path);
                if (row >= 0) m_model->updateHighlight(row, true);
            }
        }
    }

    int ts = m_thumbSize;
    QThreadPool *pool = QThreadPool::globalInstance();
    int dirCount = (int)m_subDirs.size();
    int token = m_loadToken;  // 本次加载令牌

    // 文件夹预览任务
    // 文件夹预览任务
    for (int di = 0; di < dirCount; ++di)
        pool->start(new FolderPreviewTask(this, m_allItems[di].path, di, ts, token));

    // 图片：缓存命中直接显示，未命中提交解码
    for (int i = 0; i < (int)m_imageFiles.size(); ++i) {
        int row = dirCount + i;
        QString fp = m_allItems[row].path;
        QByteArray cached = ThumbnailCache::get(fp);
        if (!cached.isEmpty()) {
            QImage img;
            if (img.loadFromData(cached))
                applyImageThumb(row, fp, img, token);
            continue;
        }
        pool->start(new ThumbDecodeTask(this, fp, row, ts, token));
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
    if (m_filterTag.isEmpty()) {
        visible = m_allItems;
    } else {
        visible.reserve(m_allItems.size());
        for (const ThumbItem &it : m_allItems) {
            if (it.isDir) continue;  // 过滤时隐藏文件夹
            if (m_store->tagsForImage(it.name).contains(m_filterTag))
                visible.push_back(it);
        }
    }
    m_model->setItems(visible);
}

void ThumbnailGrid::setFilterTag(const QString &tag) {
    m_filterTag = tag.trimmed();
    if (m_currentFolder.isEmpty()) return;
    applyFilter();
}

QStringList ThumbnailGrid::folderTags() const {
    // 汇总当前文件夹所有图片用到的标签（去重排序）
    QSet<QString> tags;
    for (const ThumbItem &it : m_allItems) {
        if (it.isDir) continue;
        for (const QString &t : m_store->tagsForImage(it.name))
            tags.insert(t);
    }
    QStringList result = tags.values();
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
    ThumbnailCache::put(path, bytes);

    // 过滤后行号可能变化，按路径定位
    int visibleRow = findModelRow(path);
    if (visibleRow < 0) return;  // 该图被过滤隐藏，不显示

    // QC 角标
    bool qc = false;
    for (const QString &qcTag : {QStringLiteral("红眼"), QStringLiteral("闭眼"),
                                 QStringLiteral("模糊"), QStringLiteral("曝光过度"),
                                 QStringLiteral("色偏")}) {
        if (m_store->tagsForImage(QFileInfo(path).fileName()).contains(qcTag)) { qc = true; break; }
    }
    if (qc) {
        // 画角标到 pixmap
        QPixmap display = pix;
        QPainter p(&display);
        int sz = qMax(12, display.width() / 7);
        QRect badge(display.width() - sz - 2, 2, sz, sz);
        p.fillRect(badge, QColor(200, 40, 40, 220));
        p.setPen(Qt::white);
        p.setFont(QFont(p.font().family(), sz * 2 / 3, QFont::Bold));
        p.drawText(badge, Qt::AlignCenter, "!");
        p.end();
        m_model->updatePix(visibleRow, display);
        return;
    }
    m_model->updatePix(visibleRow, pix);
}

void ThumbnailGrid::applyFolderThumb(int row, const QImage &img, int token) {
    // 过期任务丢弃：用户已切到别的文件夹
    if (token != m_loadToken) return;
    // 过滤时文件夹隐藏，row 可能错位 → 按路径定位
    if (row < 0 || row >= (int)m_allItems.size()) return;
    int visibleRow = findModelRow(m_allItems[row].path);
    if (visibleRow >= 0)
        m_model->updatePix(visibleRow, QPixmap::fromImage(img));
}

void ThumbnailGrid::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        int d = e->angleDelta().y() / 120;
        m_thumbSize = std::clamp(m_thumbSize + d * 32, 80, 320);
        setGridSize(QSize(thumbCellSize(), thumbCellSize()));
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
