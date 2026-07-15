#pragma once
#include <QtCore>

// 跨平台导出宏
#if defined(_MSC_VER)
    #define QLENS_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
    #define QLENS_EXPORT __attribute__((visibility("default")))
#else
    #define QLENS_EXPORT
#endif
