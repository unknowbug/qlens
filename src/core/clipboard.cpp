#include "clipboard.h"
#include <QClipboard>
#include <QMimeData>
#include <QApplication>
#include <QUrl>
#include <QBuffer>

void Clipboard::copyImage(const QImage &img)
{
    auto *clip = QApplication::clipboard();
    auto *mime = new QMimeData;

    // 格式1：图片数据（PNG）
    mime->setImageData(img);

    // 格式2：文件路径（URI list）
    clip->setMimeData(mime);
}

void Clipboard::copyPath(const QString &filePath)
{
    auto *clip = QApplication::clipboard();
    auto *mime = new QMimeData;

    // 带图片 + 文件路径
    QImage img(filePath);
    if (!img.isNull())
        mime->setImageData(img);

    QList<QUrl> urls;
    urls << QUrl::fromLocalFile(filePath);
    mime->setUrls(urls);

    clip->setMimeData(mime);
}
