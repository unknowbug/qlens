// HDR 支持检测（DXGI）
#pragma once
#include <windows.h>

// 检测主显示器是否 HDR（HDR10/PQ 输出可用）
bool HdrIsDisplayHDR();
// 按窗口所在显示器检测（多显示器，副屏准确）
bool HdrIsDisplayHDRFor(HWND hwnd);
float HdrDisplayPeakBrightnessFor(HWND hwnd);
// 显示器峰值亮度（nit），非 HDR 时返回 0
float HdrDisplayPeakBrightness();
// 是否启用 HDR 渲染（显示器 HDR 时自动启用）
bool HdrEnabled();
