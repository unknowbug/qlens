#pragma once
#include <QImage>
#include <QString>
#include "export.h"

// ICC 色彩管理
struct ColorManager {
    // 初始化 LCMS2。detectMonitor = true 时自动加载显示器配置文件
    static QLENS_EXPORT bool init(bool detectMonitor = true);

    // 将图像从嵌入 ICC 转换到显示器色彩空间
    static QLENS_EXPORT QImage applyProfile(const QImage &img);

    // 手动加载 .icc / .icm 文件
    static QLENS_EXPORT bool loadProfile(const QString &iccPath);

    // 当前是否有有效的显示器配置文件
    static QLENS_EXPORT bool hasProfile();
};
