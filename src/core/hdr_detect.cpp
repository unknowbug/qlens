#include "hdr_detect.h"
#include <QApplication>
#include <QScreen>

static HdrDetect::Params s_params;

bool HdrDetect::isHdrSupported()
{
    // Windows: 用 DXGI 检测（platform/win/hdr_win.cpp）
    // Linux/macOS: 各自的实现
    // 当前返回 false，平台实现后覆盖
    return false;
}

int HdrDetect::hdrState()
{
    return isHdrSupported() ? 1 : 0;
}

HdrDetect::Params HdrDetect::params() { return s_params; }
void HdrDetect::setParams(const Params &p) { s_params = p; }
