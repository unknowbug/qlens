#pragma once
#include <QGuiApplication>
#include "export.h"

// HDR 检测（平台抽象）
// 各平台实现在 src/platform/ 目录
struct HdrDetect {
    // 检查当前显示器是否支持 HDR
    static QLENS_EXPORT bool isHdrSupported();

    // 当前 HDR 状态：0=SDR, 1=HDR
    static QLENS_EXPORT int hdrState();

    // HDR 参数（仅在使用 hdr-ext 渲染扩展时有效）
    struct Params {
        float paper_white = 300.0f;
        float sdr_boost = 1.5f;
        float tone_map = 1.0f;
        float contrast = 1.0f;
    };
    static QLENS_EXPORT Params params();
    static QLENS_EXPORT void setParams(const Params &p);
};
