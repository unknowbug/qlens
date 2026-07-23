#include "ThumbnailGrid.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QThread>
#include <QWheelEvent>

ThumbnailGrid::ThumbnailGrid(QWidget *parent) : QScrollArea(parent) {
    setWidgetResizable(true);
    setStyleSheet("QScrollArea{background:#111; border:none;}");

    m_gridWidget = new QWidget(this);
    m_gridWidget->setStyleSheet("background:#111;");
    m_gridLayout = new QGridLayout(m_gridWidget);
    m_gridLayout->setContentsMargins(8, 8, 8, 8);
    m_gridLayout->setSpacing(4);
    m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setWidget(m_gridWidget);
}

void ThumbnailGrid::loadFolder(const QString &path) {
    m_currentFolder = path;
    m_imageFiles.clear();
    m_subDirs.clear();

    QDir dir(path);

    // 子文件夹（排在前面，不设 thumbnail 过滤）
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    m_subDirs = dir.entryList();

    // 图片文件
    QStringList filters;
    for (auto &fmt : QImageReader::supportedImageFormats())
        filters << ("*." + QString::fromLatin1(fmt));
    filters << "*.cr2" << "*.cr3" << "*.nef" << "*.arw" << "*.dng" << "*.rw2";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::Readable);
    m_imageFiles = dir.entryList();

    int total = m_subDirs.size() + m_imageFiles.size();

    // 清空旧按钮
    QLayoutItem *child;
    while ((child = m_gridLayout->takeAt(0))) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    if (total == 0) return;

    int cols = std::max(1, m_gridWidget->width() / (m_thumbSize + 8));
    m_thumbCols = cols;

    for (int i = 0; i < total; ++i) {
        auto *btn = new QPushButton(m_gridWidget);
        btn->setFixedSize(m_thumbSize, m_thumbSize);

        bool isDir = i < m_subDirs.size();
        btn->setToolTip(isDir ? m_subDirs[i] : m_imageFiles[i - m_subDirs.size()]);
        btn->setProperty("isDir", isDir);
        if (!isDir) btn->setProperty("thumbIndex", i - m_subDirs.size());

        if (isDir) {
            btn->setText(m_subDirs[i]);
            btn->setStyleSheet(
                "QPushButton{background:#1a1a1a; border:1px solid #444; color:#aaa; font-size:12px;}"
                "QPushButton:hover{border:1px solid #888;}");
        } else {
            btn->setText(m_imageFiles[i - m_subDirs.size()]);
            btn->setStyleSheet(
                "QPushButton{background:#1a1a1a; border:1px solid #333; color:#888; font-size:12px;}"
                "QPushButton:hover{border:1px solid #888;}");
        }

        btn->installEventFilter(this);
        m_gridLayout->addWidget(btn, i / cols, i % cols);
    }

    // 异步加载缩略图（仅图片部分）
    QString folder = path;
    int ts = m_thumbSize;
    int colCount = cols;
    int dirCount = m_subDirs.size();
    QStringList files = m_imageFiles;
    auto *t = QThread::create([this, folder, ts, colCount, dirCount, files]() {
        for (int i = 0; i < (int)files.size(); ++i) {
            QString fp = folder + "/" + files[i];
            QImageReader rd(fp); rd.setAutoTransform(true);
            QSize orig = rd.size();
            if (orig.isValid() && orig.width() > 0)
                rd.setScaledSize(orig.scaled(ts, ts, Qt::KeepAspectRatio));
            QImage img = rd.read();
            if (img.isNull()) continue;
            QPixmap pix = QPixmap::fromImage(img);
            int gridIdx = dirCount + i;
            QMetaObject::invokeMethod(this, [this, gridIdx, pix, colCount]() {
                auto *item = m_gridLayout->itemAtPosition(gridIdx / colCount, gridIdx % colCount);
                if (item && item->widget()) {
                    auto *btn = qobject_cast<QPushButton*>(item->widget());
                    if (btn) {
                        btn->setIcon(QIcon(pix));
                        btn->setIconSize(QSize(m_thumbSize - 8, m_thumbSize - 8));
                        btn->setText("");
                    }
                }
            }, Qt::QueuedConnection);
        }
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void ThumbnailGrid::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        int d = e->angleDelta().y() / 120;
        m_thumbSize = std::clamp(m_thumbSize + d * 32, 80, 512);
        int iconSz = m_thumbSize - 8;
        for (int i = 0; i < m_gridLayout->count(); ++i) {
            auto *btn = qobject_cast<QPushButton*>(m_gridLayout->itemAt(i)->widget());
            if (btn) {
                btn->setFixedSize(m_thumbSize, m_thumbSize);
                btn->setIconSize(QSize(iconSz, iconSz));
            }
        }
        return;
    }
    QScrollArea::wheelEvent(e);
}

bool ThumbnailGrid::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto *btn = qobject_cast<QPushButton*>(obj);
        if (!btn) return QScrollArea::eventFilter(obj, event);
        QString name = btn->toolTip();
        if (btn->property("isDir").toBool()) {
            emit folderDoubleClicked(m_currentFolder + "/" + name);
        } else {
            int idx = btn->property("thumbIndex").toInt();
            if (idx >= 0 && idx < (int)m_imageFiles.size())
                emit imageDoubleClicked(m_currentFolder + "/" + m_imageFiles[idx]);
        }
        return true;
    }
    return QScrollArea::eventFilter(obj, event);
}
