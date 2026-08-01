#define NOMINMAX

#include "MainWindow.h"

#include <vector>

#include <QVBoxLayout>

#include <QMenu>

#include <QFileDialog>

#include <QMimeData>

#include <QDragEnterEvent>

#include <QDropEvent>

#include <QImageReader>

#include <QApplication>

#include <QClipboard>

#include <QResizeEvent>
#include <QScrollArea>


#include <QGraphicsPixmapItem>

#include <QThread>

#include <QMouseEvent>
#include <QScrollBar>
#include <QProcess>
#include <QFileInfo>



MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)

{

    setAcceptDrops(true);

    setMouseTracking(true);

    setStyleSheet("QMainWindow, QGraphicsView { background: #1e1e1e; }");



    auto *c = new QWidget(this); auto *l = new QVBoxLayout(c);

    l->setContentsMargins(0,0,0,0); setCentralWidget(c);



    m_scene = new QGraphicsScene(this);

    m_view = new QGraphicsView(m_scene, this);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    m_view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    m_view->setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_view->viewport()->setAcceptDrops(false);
    m_view->setAcceptDrops(false);
    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setMouseTracking(true);
    m_view->installEventFilter(this);
    l->addWidget(m_view, 1);



    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);



    // ── 关闭按钮 ──

    auto *closeBtn = new QPushButton("\u00d7", this);

    closeBtn->setObjectName("closeBtn");

    closeBtn->setGeometry(0,0,52,40);

    closeBtn->setStyleSheet("QPushButton{background:rgba(0,0,0,0);color:#555;font-size:26px;border-left:1px solid #444;border-bottom:1px solid #444;border-top:none;border-right:none;border-bottom-left-radius:52px 32px}"

                            "QPushButton:hover{background:rgba(60,60,60,220);color:#ccc;border-left:1px solid #777;border-bottom:1px solid #777;border-top:none;border-right:none}");

    connect(closeBtn, &QPushButton::clicked, this, &QMainWindow::close);

    closeBtn->raise();


    // ── 导航按钮 ──

    auto mkBtn = [&](const QString &text, QString obj) -> QPushButton* {

        auto *b = new QPushButton(text, c);

        b->setObjectName(obj);

        b->setStyleSheet("QPushButton{background:rgba(40,40,40,54);color:#fff;font-size:28px;"

                         "border:1px solid #666;padding:6px 16px;border-radius:4px}"

                         "QPushButton:hover{background:rgba(80,80,80,220)}");

        return b;

    };

    m_btnPrev = mkBtn("\u25c0", "btnPrev");  // ◀

    m_btnNext = mkBtn("\u25b6", "btnNext");  // ▶

    m_btnManager = mkBtn("\u25a0", "btnManager");  // ■（管理器入口）

    m_btnManager->setStyleSheet("QPushButton{background:rgba(40,40,40,54);color:#aaa;font-size:14px;"

                                "border:1px solid #555;padding:4px 10px;border-radius:4px}"

                                "QPushButton:hover{background:rgba(80,80,80,200);color:#fff}");

    m_btnManager->setText(tr("Manage"));

    connect(m_btnPrev, &QPushButton::clicked, [this]{ navigate(-1); });

    connect(m_btnNext, &QPushButton::clicked, [this]{ navigate(1); });

    connect(m_btnManager, &QPushButton::clicked, [this]{
        launchManager();
    });



    // ── 文件名 ──

    m_lblFile = new QLabel(this);
    m_lblFile->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_lblFile->setWordWrap(false);
    m_lblFile->setStyleSheet("QLabel{color:#ccc;font-size:13px;background:transparent;padding:2px 8px}");
    m_lblFile->raise();



    // 缩略图条
    m_thumbStrip = new QScrollArea(c);
    m_thumbStrip->setFixedHeight(80);
    m_thumbStrip->show();
    m_thumbStrip->setStyleSheet("QScrollArea{background:rgba(0,0,0,0);border:none}"
        "QScrollBar:horizontal{height:0px;}"
        "QScrollBar::handle:horizontal{height:0px;}");
    m_thumbInner = new QWidget();
    m_thumbInner->setMinimumHeight(72);
    m_thumbInner->setStyleSheet("background:rgba(30,30,30,77)");
    m_thumbLayout = new QHBoxLayout(m_thumbInner);
    m_thumbLayout->setContentsMargins(4,2,4,2);
    m_thumbLayout->setSpacing(4);
    m_thumbStrip->setWidget(m_thumbInner);
    l->addWidget(m_thumbStrip);

    // ── 隐藏定时器 ──

    m_hideTimer = new QTimer(this);

    m_hideTimer->setSingleShot(true);

    connect(m_hideTimer, &QTimer::timeout, [this]{ showUI(false); });

    m_btnPrev->raise(); m_btnNext->raise(); m_btnManager->raise();
    m_lblFile->raise();
    if (m_thumbStrip) m_thumbStrip->raise();

    showUI(false);

}



void MainWindow::launchManager()
{
    QString path = QCoreApplication::applicationDirPath() + "/qlens_manager.exe";
    QStringList args;
    if (!m_currentFile.isEmpty()) args << m_currentFile;
    if (QProcess::startDetached(path, args)) {
        // 启动成功，快速查看器退出，图片管理交给 Manager
        close();
    }
}

void MainWindow::showUI(bool v)
{
    m_btnPrev->setVisible(v); m_btnNext->setVisible(v);
    m_btnManager->setVisible(v);
    m_lblFile->setVisible(v);
    if (v) {
        setCursor(Qt::ArrowCursor);
    } else {
        setCursor(Qt::BlankCursor);
    }
}



void MainWindow::mouseMoveEvent(QMouseEvent *e)

{

    showUI(true);

    m_hideTimer->start(2000);

    QMainWindow::mouseMoveEvent(e);

}



void MainWindow::openFile(const QString &fp)

{

    if (!QFile::exists(fp)) return;

    QFileInfo fi(fp);

    m_currentDir = fi.dir(); m_currentFile = fp;

    QStringList f;

    for (auto &fmt : QImageReader::supportedImageFormats()) f << ("*." + QString::fromLatin1(fmt));

    f << "*.cr2"<<"*.cr3"<<"*.nef"<<"*.arw"<<"*.dng"<<"*.rw2";

    m_currentDir.setNameFilters(f); m_currentDir.setFilter(QDir::Files|QDir::Readable);

    m_siblings = m_currentDir.entryList();

    m_currentIndex = m_siblings.indexOf(fi.fileName());

    m_lblFile->setText(fi.fileName());

    loadImage(fp);
    buildThumbnails();

}



void MainWindow::loadImage(const QString &fp)

{

    m_original = QPixmap();

    m_scene->clear();

    decodeAsync(fp, m_currentIndex);

}



void MainWindow::decodeAsync(const QString &fp, int idx)

{

    int vp = std::max(m_view->width(), m_view->height());

    if (vp < 64) vp = std::max(this->width(), this->height()) / 2;



    auto *t = QThread::create([this, fp, vp, idx]() {

        QImageReader r(fp); r.setAutoTransform(true);

        QSize fs = r.size();

        if (fs.isValid() && fs.width() > 0)

            if (fs.width() > vp || fs.height() > vp)

                r.setScaledSize(fs.scaled(vp, vp, Qt::KeepAspectRatio));

        QImage img = r.read(); if (img.isNull()) return;

        QPixmap pix = QPixmap::fromImage(img);

        QMetaObject::invokeMethod(this, [this, idx, pix]() {

            if (idx != m_currentIndex) return;

            m_original = pix;

            m_scene->clear();

            QSize vs = m_view->viewport()->size();

            if (vs.isEmpty()) vs = m_view->size();

            if (vs.isEmpty()) vs = QSize(800, 600);

            QRectF sr = pix.rect();
            // 场景至少是图片的 1.2 倍，或视口 1.5 倍，取大
            sr.setSize(QSizeF(
                std::max(pix.width() * 1.2, vs.width() * 1.5),
                std::max(pix.height() * 1.2, vs.height() * 1.5)));
            m_scene->setSceneRect(sr);

            auto *item = m_scene->addPixmap(pix);

            item->setPos((sr.width() - pix.width()) / 2.0,

                         (sr.height() - pix.height()) / 2.0);

            m_zoomFactor = (pix.width() < m_view->width()

                && pix.height() < m_view->height()) ? 1.0 : 0.0;

            updateZoom();

            m_view->centerOn(item);

            updateTitle();

        }, Qt::QueuedConnection);

    });

    connect(t, &QThread::finished, t, &QObject::deleteLater);

    t->start();

}



void MainWindow::updateZoom()

{

    if (m_original.isNull()) return;

    if (m_zoomFactor <= 0.0) {
        auto items = m_scene->items();
        if (!items.isEmpty()) {
            auto *item = items.first();
            QRectF br = item->sceneBoundingRect();
            // 小图不放大，直接 100% 居中
            if (br.width() <= m_view->viewport()->width() && br.height() <= m_view->viewport()->height()) {
                m_view->resetTransform();
                m_view->centerOn(item);
            } else {
                m_view->fitInView(br, Qt::KeepAspectRatio);
            }
        }
    } else {
        m_view->resetTransform();
        m_view->scale(m_zoomFactor, m_zoomFactor);
    }

}



void MainWindow::navigate(int d)

{

    if (m_siblings.isEmpty() || m_currentIndex < 0) return;

    int n = m_currentIndex + d;

    if (n < 0 || n >= m_siblings.size()) return;

    m_currentIndex = n; m_currentFile = m_currentDir.absoluteFilePath(m_siblings[n]);

    m_lblFile->setText(QFileInfo(m_currentFile).fileName());

    loadImage(m_currentFile);
    scrollThumbToCenter();
}



// ── 事件 ──



void MainWindow::keyPressEvent(QKeyEvent *e)

{

    switch (e->key()) {

    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Space: navigate(1); break;

    case Qt::Key_Left: case Qt::Key_Up: navigate(-1); break;

    case Qt::Key_Escape: case Qt::Key_Q: close(); break;

    case Qt::Key_Plus: case Qt::Key_Equal:

        if (m_zoomFactor <= 0.0) m_zoomFactor = 1.0;

        m_zoomFactor = std::min(m_zoomFactor * 1.25, 10.0); updateZoom(); break;

    case Qt::Key_Minus:

        if (m_zoomFactor > 0.0) {

            m_zoomFactor = std::max(m_zoomFactor / 1.25, 0.0); updateZoom();

        } break;

    case Qt::Key_0: case Qt::Key_F: m_zoomFactor = 1.0; updateZoom(); break;
    case Qt::Key_1: case Qt::Key_S: m_zoomFactor = 0.0; updateZoom(); break;

    case Qt::Key_C:

        if ((e->modifiers() & Qt::ControlModifier) && !m_currentFile.isEmpty()) {

            auto *clip = QApplication::clipboard(); auto *m = new QMimeData;

            if (!m_original.isNull()) m->setImageData(m_original.toImage());

            QList<QUrl> u; u << QUrl::fromLocalFile(m_currentFile); m->setUrls(u);

            clip->setMimeData(m);

        }

        break;

    default: QMainWindow::keyPressEvent(e);

    }

}



void MainWindow::wheelEvent(QWheelEvent *e)

{

    double d = e->angleDelta().y() / 120.0;

    if (e->modifiers() & Qt::ControlModifier) {

        if (d > 0) { if (m_zoomFactor <= 0.0) m_zoomFactor = 1.0;

            m_zoomFactor = std::min(m_zoomFactor * 1.15, 10.0); }

        else if (m_zoomFactor > 0.0)

            m_zoomFactor = std::max(m_zoomFactor / 1.15, 0.0);

        updateZoom();

    } else navigate(d > 0 ? -1 : 1);

}



void MainWindow::contextMenuEvent(QContextMenuEvent *e)

{

    QMenu m(this);

    if (!m_currentFile.isEmpty()) {

        m.addAction(tr("Copy"), [this]() {

            auto *c = QApplication::clipboard(); auto *d = new QMimeData;

            if (!m_original.isNull()) d->setImageData(m_original.toImage());

            QList<QUrl> u; u << QUrl::fromLocalFile(m_currentFile); d->setUrls(u);

            c->setMimeData(d);

        });

        m.addSeparator();

        m.addAction(tr("Save As..."), [this]() {

            QString d = QFileDialog::getSaveFileName(this, {}, QFileInfo(m_currentFile).completeBaseName(),

                "JPEG (*.jpg);;PNG (*.png);;WebP (*.webp)");

            if (!d.isEmpty() && !m_original.isNull()) m_original.toImage().save(d);

        });

    }

    m.addAction(tr("Open File..."), [this]() {

        QString f = QFileDialog::getOpenFileName(this, tr("Open Image"));

        if (!f.isEmpty()) openFile(f);

    });

    m.exec(e->globalPos());

}



bool MainWindow::eventFilter(QObject *obj, QEvent *event)

{

    if (event->type() == QEvent::MouseMove) {
        showUI(true);
        m_hideTimer->start(2000);
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        launchManager();
        return true;
    }
    if (event->type() == QEvent::Wheel) {

        wheelEvent(static_cast<QWheelEvent*>(event));

        return true;

    }

    if (event->type() == QEvent::DragEnter) {

        dragEnterEvent(static_cast<QDragEnterEvent*>(event));

        return true;

    }

    if (event->type() == QEvent::Drop) {

        dropEvent(static_cast<QDropEvent*>(event));

        return true;

    }

    return QMainWindow::eventFilter(obj, event);

}



void MainWindow::dragEnterEvent(QDragEnterEvent *e)

{

    if (e->mimeData()->hasUrls()) e->acceptProposedAction();

}



void MainWindow::dropEvent(QDropEvent *e)

{

    auto u = e->mimeData()->urls();

    if (!u.isEmpty()) openFile(u.first().toLocalFile());

}



void MainWindow::resizeEvent(QResizeEvent *e)

{

    QMainWindow::resizeEvent(e);

    int w = width();

    // 关闭按钮

    if (auto *b = findChild<QPushButton*>("closeBtn"))

        { b->move(w-56, -10); b->raise(); }

    // 导航按钮

    int cx = w / 2;

    int bh = 160, by = (height() - bh) / 2, bw = 44;

    if (m_btnPrev) { m_btnPrev->setGeometry(8, by, bw, bh); m_btnPrev->raise(); }

    if (m_btnNext) { m_btnNext->setGeometry(w-8-bw, by, bw, bh); m_btnNext->raise(); }

    if (m_btnManager) { m_btnManager->setGeometry(cx-40, height()-130, 80, 36); m_btnManager->raise(); }

    // 文件名

    if (m_lblFile) { m_lblFile->setGeometry(10, height()-110, w-20, 24); m_lblFile->raise(); }
    // strip is in layout, no manual position needed

    if (m_zoomFactor <= 0.0) updateZoom();

}



void MainWindow::updateTitle()

{

    if (m_currentFile.isEmpty()) { setWindowTitle("QLens"); return; }

    QString z = m_zoomFactor > 0.0 ? QString(" %1%").arg(int(m_zoomFactor*100)) : QString();

    setWindowTitle(QString("QLens \u2014 %1%2 (%3/%4)")

        .arg(QFileInfo(m_currentFile).fileName()).arg(z).arg(m_currentIndex+1).arg(m_siblings.size()));

}




// ── 缩略图条 ──
void MainWindow::buildThumbnails()
{
    if (m_siblings.isEmpty()) return;
    // 清空旧按钮
    QLayoutItem *child;
    while ((child = m_thumbLayout->takeAt(0))) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    m_thumbCount = 0;
    int total = (int)m_siblings.size();
    int start = (m_currentIndex >= 0 && m_currentIndex < total) ? m_currentIndex : 0;
    m_thumbLayout->setContentsMargins(4, 2, 4, 2);
    for (int i = 0; i < total; ++i)
        thumbAddBtn(i, {});
    m_thumbInner->setFixedWidth(total * (m_thumbSize + 12) + 8);
    scrollThumbToCenter();
    m_thumbInner->show(); m_thumbInner->raise();
    m_thumbStrip->repaint();
    // 螺旋顺序：当前图 → 右侧 → 左侧
    std::vector<int> order;
    order.reserve(total);
    order.push_back(start);
    for (int d = 1; d < total; ++d) {
        int r = start + d, l = start - d;
        if (r < total) order.push_back(r);
        if (l >= 0)    order.push_back(l);
    }
    // 拷贝路径列表到线程（避免访问主线程的 m_siblings）
    QStringList paths;
    for (int idx : order)
        paths << m_currentDir.absoluteFilePath(m_siblings[idx]);
    int ts = m_thumbSize;
    auto *t = QThread::create([this, paths, ts]() {
        for (const QString &path : paths) {
            QImageReader rd(path);
            rd.setAutoTransform(true);
            rd.setScaledSize(QSize(ts, ts));
            QImage img = rd.read();
            if (img.isNull()) continue;
            QPixmap pix = QPixmap::fromImage(img);
            QString name = QFileInfo(path).fileName();
            QMetaObject::invokeMethod(this, [this, name, pix]() {
                thumbUpdateByName(name, pix);
            }, Qt::QueuedConnection);
        }
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void MainWindow::thumbUpdateByName(const QString &name, const QPixmap &icon)
{
    auto btns = m_thumbInner->findChildren<QPushButton*>();
    for (auto *btn : btns) {
        if (btn->toolTip() != name) continue;
        btn->setIcon(QIcon(icon));
        btn->setIconSize(QSize(m_thumbSize, m_thumbSize));
        btn->setText("");
        return;
    }
}

void MainWindow::thumbAddBtn(int idx, const QPixmap &pix)
{
    auto *btn = new QPushButton(m_thumbInner);
    btn->setFixedSize(m_thumbSize + 8, m_thumbSize + 8);
    if (pix.isNull()) {
        btn->setText("…");
        btn->setStyleSheet("QPushButton{color:#777;background:#2a2a2a;border:1px solid #444}");
    } else {
        btn->setIcon(QIcon(pix));
        btn->setIconSize(QSize(m_thumbSize, m_thumbSize));
        btn->setStyleSheet("QPushButton{background:rgba(0,0,0,0);border:1px solid #555;padding:0}"
            "QPushButton:hover{border:1px solid #aaa}");
    }
    btn->setToolTip(m_siblings[idx]);
    btn->setCursor(Qt::PointingHandCursor);
    int cap = idx;
    connect(btn, &QPushButton::clicked, [this, cap]() {
        if (cap != m_currentIndex && cap >= 0 && cap < m_siblings.size())
            openFile(m_currentDir.absoluteFilePath(m_siblings[cap]));
    });
    m_thumbLayout->addWidget(btn);
    m_thumbCount++;
    if (m_thumbCount >= (int)m_siblings.size()) scrollThumbToCenter();
}

void MainWindow::scrollThumbToCenter()
{
    if (!m_thumbStrip || !m_thumbInner || m_thumbCount <= 0 || m_siblings.isEmpty()) return;
    int vpW = m_thumbStrip->viewport()->width();
    if (vpW <= 0) vpW = m_thumbStrip->width() - 4;
    if (vpW <= 0) { QTimer::singleShot(50, this, [this]{ scrollThumbToCenter(); }); return; }
    int btnW = m_thumbSize + 12, half = (m_thumbSize + 8) / 2;
    int pad  = vpW / 2 - half;
    if (pad < 0) pad = 0;
    m_thumbLayout->setContentsMargins(pad, 2, pad, 2);
    m_thumbLayout->activate();
    m_thumbInner->setFixedWidth(2 * pad + m_thumbCount * btnW);

    auto *sb = m_thumbStrip->horizontalScrollBar();
    if (sb) {
        // 强制刷新 scrollbar range（构建按钮后几何可能还没更新）
        QApplication::processEvents();
        int targetX = pad + m_currentIndex * btnW + half - vpW / 2;
        if (targetX < 0) targetX = 0;
        sb->setValue(targetX);
    }
    if (m_lblFile) m_lblFile->raise();
}
