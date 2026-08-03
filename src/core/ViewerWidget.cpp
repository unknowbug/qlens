#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ViewerWidget.h"
#include "decode_api.h"
#include "i18n.h"
#include <QHBoxLayout>
#include <QImageReader>
#include <QThread>
#include <QApplication>
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>

// 翻译辅助：msgid=中文，默认中文；.po 覆盖为目标语言
static QString T(const wchar_t *id) { return QString::fromWCharArray(I18n::Get(id)); }

ViewerWidget::ViewerWidget(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    m_scene = new QGraphicsScene(this);
    m_view  = new QGraphicsView(m_scene, this);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    m_view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_view->viewport()->setAcceptDrops(false);
    m_view->setAcceptDrops(false);
    m_view->setBackgroundBrush(Qt::black);  // 黑色背景（与文件浏览器一致）
    // 滚轮/双击事件发生在 m_view 上，拦截转发到本控件处理
    m_view->viewport()->installEventFilter(this);
    m_view->installEventFilter(this);
    l->addWidget(m_view, 1);
}

bool ViewerWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        wheelEvent(static_cast<QWheelEvent*>(event));
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::RightButton) {
            showContextMenu(me->globalPos());
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// 右键菜单（不再是左键行为）
void ViewerWidget::showContextMenu(const QPoint &gpos)
{
    QMenu menu(this);
    QAction *back    = menu.addAction(T(L"返回网格"));
    menu.addSeparator();
    QAction *prev    = menu.addAction(T(L"上一张"));
    QAction *next    = menu.addAction(T(L"下一张"));
    menu.addSeparator();
    QAction *zoomin  = menu.addAction(T(L"放大"));
    QAction *zoomout = menu.addAction(T(L"缩小"));
    QAction *full    = menu.addAction(T(L"100%"));
    QAction *fit     = menu.addAction(T(L"适应窗口"));
    menu.addSeparator();
    QAction *rotCW  = menu.addAction(T(L"顺时针旋转 90°"));
    QAction *rotCCW = menu.addAction(T(L"逆时针旋转 90°"));
    menu.addSeparator();
    QAction *copypath = menu.addAction(T(L"复制"));

    QAction *sel = menu.exec(gpos);
    if (sel == back) { emit backRequested(); }
    else if (sel == prev) { navigate(-1); }
    else if (sel == next) { navigate(1); }
    else if (sel == zoomin) { m_zoomFactor = (m_zoomFactor <= 0.0 ? 1.0 : m_zoomFactor) * 1.25; updateZoom(); }
    else if (sel == zoomout) { m_zoomFactor = (m_zoomFactor <= 0.0 ? 1.0 : m_zoomFactor) / 1.25; updateZoom(); }
    else if (sel == full) { m_zoomFactor = 1.0; updateZoom(); }
    else if (sel == fit) { m_zoomFactor = 0.0; updateZoom(); }
    else if (sel == copypath) {
        // 三合一复制（与 QuickView 一致）：文件 + 路径 + 图片
        QImage img = QLensCore::decodeImage(m_currentFile);
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(m_currentFile)});
        mime->setText(m_currentFile);
        if (!img.isNull()) mime->setImageData(img);
        QApplication::clipboard()->setMimeData(mime);
    } else if (sel == rotCW) {
        rotate(1);
    } else if (sel == rotCCW) {
        rotate(-1);
    }
}

// ── 图片加载 ──────────────────────────────

void ViewerWidget::openFile(const QString &fp)
{
    QFileInfo fi(fp);
    if (!fi.exists() || !fi.isFile()) return;

    m_currentDir  = fi.absoluteDir();
    m_currentFile = fp;

    QStringList f;
    for (auto &fmt : QImageReader::supportedImageFormats())
        f << ("*." + QString::fromLatin1(fmt));
    f << "*.cr2" << "*.cr3" << "*.nef" << "*.arw" << "*.dng" << "*.rw2";
    // qlens 解码/插件支持的格式（QImageReader 不覆盖）
    f << "*.heic" << "*.heif" << "*.avif" << "*.svg" << "*.svgz" << "*.jxr";
    m_currentDir.setNameFilters(f);
    m_currentDir.setFilter(QDir::Files | QDir::Readable);
    m_siblings = m_currentDir.entryList();
    m_currentIndex = m_siblings.indexOf(fi.fileName());

    loadImage(fp);
}

void ViewerWidget::loadImage(const QString &fp)
{
    m_original = QPixmap();
    m_zoomFactor = -1.0;
    emit imageChanged(fp, m_currentIndex);
    decodeAsync(fp, m_currentIndex);
}

void ViewerWidget::decodeAsync(const QString &fp, int idx)
{
    int vp = std::max(m_view->width(), m_view->height()) / 2;
    if (vp < 64) vp = 1200;

    auto *t = QThread::create([this, fp, vp, idx]() {
        QImageReader r(fp); r.setAutoTransform(true);
        QSize fs = r.size();
        if (fs.isValid() && fs.width() > 0 && (fs.width() > vp || fs.height() > vp))
            r.setScaledSize(fs.scaled(vp, vp, Qt::KeepAspectRatio));
        QImage img = r.read();
        if (img.isNull()) {
            // qlens 回退（插件格式：HEIC/SVG/JXR 等 QImageReader 不支持）
            img = QLensCore::decodeImage(fp);
            if (img.isNull()) return;
        }
        QPixmap pix = QPixmap::fromImage(img);
        // 二次平滑缩放（setScaledSize 不带抗锯齿）
        if (pix.width() > vp || pix.height() > vp)
            pix = pix.scaled(vp, vp, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        QMetaObject::invokeMethod(this, [this, idx, pix]() {
            if (idx != m_currentIndex) return;
            m_original = pix;
            m_scene->clear();
            m_pixmapItem = nullptr;

            QSize vs = m_view->viewport()->size();
            if (vs.isEmpty()) vs = m_view->size();
            if (vs.isEmpty()) vs = QSize(800, 600);
            QRectF sr = pix.rect();
            sr.setSize(QSizeF(
                std::max(pix.width()  * 1.2, vs.width()  * 1.5),
                std::max(pix.height() * 1.2, vs.height() * 1.5)));
            m_scene->setSceneRect(sr);
            m_pixmapItem = m_scene->addPixmap(pix);
            m_pixmapItem->setPos((sr.width() - pix.width()) / 2.0,
                                 (sr.height() - pix.height()) / 2.0);
            m_view->centerOn(m_pixmapItem);

            m_zoomFactor = (pix.width() < m_view->width() && pix.height() < m_view->height())
                ? 1.0 : 0.0;
            updateZoom();
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

// ── 导航 ──────────────────────────────

void ViewerWidget::navigate(int direction)
{
    if (m_siblings.isEmpty() || m_currentIndex < 0) return;
    int n = m_currentIndex + direction;
    if (n < 0 || n >= m_siblings.size()) return;
    m_currentIndex = n;
    m_currentFile  = m_currentDir.absoluteFilePath(m_siblings[n]);
    loadImage(m_currentFile);
}

// ── 缩放 ──────────────────────────────

void ViewerWidget::updateZoom()
{
    if (m_original.isNull() || !m_pixmapItem) return;
    if (m_zoomFactor > 0.0) {
        m_view->resetTransform();
        m_view->scale(m_zoomFactor, m_zoomFactor);
        m_view->centerOn(m_pixmapItem);
    } else {
        QRectF br = m_pixmapItem->sceneBoundingRect();
        if (br.width() <= m_view->viewport()->width() && br.height() <= m_view->viewport()->height()) {
            m_view->resetTransform();
            m_view->centerOn(m_pixmapItem);
        } else {
            m_view->fitInView(br, Qt::KeepAspectRatio);
        }
    }
    m_view->viewport()->update();  // 强制重绘（缩放/适配后立即生效）
}

// 旋转 90°（dir=1 顺时针，-1 逆时针）
void ViewerWidget::rotate(int dir)
{
    if (m_original.isNull() || !m_pixmapItem) return;
    QTransform t;
    t.rotate(dir * 90.0);
    m_original = m_original.transformed(t, Qt::SmoothTransformation);
    m_pixmapItem->setPixmap(m_original);
    updateZoom();
}

// 尺寸变化后重新适配（修复首次打开时窗口尺寸未就绪导致 fit 缩小）
void ViewerWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_zoomFactor == 0.0 && !m_original.isNull())
        updateZoom();
}

// ── 事件 ──────────────────────────────

void ViewerWidget::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Left: case Qt::Key_Up:   navigate(-1); break;
    case Qt::Key_Right: case Qt::Key_Down:
    case Qt::Key_Space:                    navigate(1);  break;
    case Qt::Key_Home:
        m_currentIndex = 0; loadImage(m_currentDir.absoluteFilePath(m_siblings[0])); break;
    case Qt::Key_End:
        m_currentIndex = (int)m_siblings.size() - 1;
        loadImage(m_currentDir.absoluteFilePath(m_siblings[m_currentIndex])); break;
    case Qt::Key_Plus: case Qt::Key_Equal:
        if (m_zoomFactor <= 0.0) m_zoomFactor = 1.0;
        m_zoomFactor = std::min(m_zoomFactor * 1.25, 10.0); updateZoom(); break;
    case Qt::Key_Minus:
        if (m_zoomFactor > 0.0) {
            m_zoomFactor = std::max(m_zoomFactor / 1.25, 0.0);
            updateZoom();
        } break;
    case Qt::Key_1: case Qt::Key_F: m_zoomFactor = 1.0; updateZoom(); break;
    case Qt::Key_0: case Qt::Key_S: m_zoomFactor = 0.0; updateZoom(); break;
    case Qt::Key_Q: rotate(-1); break;   // 逆时针 90°
    case Qt::Key_E: rotate(1); break;    // 顺时针 90°
    default: QWidget::keyPressEvent(e);
    }
}

void ViewerWidget::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        double d = e->angleDelta().y() / 120.0;
        if (d > 0) {
            if (m_zoomFactor <= 0.0) m_zoomFactor = 1.0;
            m_zoomFactor = std::min(m_zoomFactor * 1.15, 10.0);
        } else if (m_zoomFactor > 0.0) {
            m_zoomFactor = std::max(m_zoomFactor / 1.15, 0.0);
        }
        updateZoom();
    } else {
        navigate(e->angleDelta().y() > 0 ? -1 : 1);
    }
}
