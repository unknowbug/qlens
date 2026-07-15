#include "loader.h"
#include <QFile>
#include <QFileInfo>
#include <libraw.h>

QImage RawLoader::loadThumbnail(const QString &filePath, int maxDim)
{
    libraw_data_t *raw = libraw_init(0);
    if (!raw) return {};

    QByteArray path8 = filePath.toUtf8();
    if (libraw_open_file(raw, path8.constData()) != LIBRAW_SUCCESS) {
        libraw_close(raw);
        return {};
    }

    // 读取 embedded JPEG 预览
    if (libraw_unpack_thumb(raw) != LIBRAW_SUCCESS) {
        libraw_recycle(raw);
        libraw_close(raw);
        return {};
    }

    libraw_processed_image_t *thumb = libraw_dcraw_make_mem_thumb(raw, nullptr);
    if (!thumb) {
        libraw_recycle(raw);
        libraw_close(raw);
        return {};
    }

    QImage img;
    if (thumb->type == LIBRAW_IMAGE_JPEG) {
        img = QImage::fromData(thumb->data, (int)thumb->data_size, "JPEG");
    }

    libraw_dcraw_clear_mem(thumb);
    libraw_recycle(raw);
    libraw_close(raw);

    if (img.isNull()) return {};

    // 限缩
    if (std::max(img.width(), img.height()) > maxDim)
        img = img.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

QImage RawLoader::loadFull(const QString &filePath, int maxDim)
{
    // JPEG/PNG/WebP 等格式直接由 QImage 加载
    QImage img(filePath);
    // RAW 全尺寸解码：将在后续版本中实现
    // 当前阶段使用 loadThumbnail（embedded JPEG 预览）
    if (img.isNull())
        return loadThumbnail(filePath, maxDim);
    if (std::max(img.width(), img.height()) > maxDim)
        img = img.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}
