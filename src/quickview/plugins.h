// 插件接口 —— 核心定义，插件 DLL 实现
// 设计：解码器插件（Query 能力 + Decode 按需解码，支持矢量/多帧/HDR 元数据）
// 插件 DLL 只实现 query/decode，不碰任何 UI/渲染
#pragma once
#include <windows.h>
#include <string>
#include <vector>

// ── 像素格式 ──
enum QLensPixelFormat {
    QLPF_BGRA8 = 0,   // 8bit/通道 BGRA（SDR）
    QLPF_RGBA16F = 1, // FP16 RGBA（HDR）
    QLPF_RGBA32F = 2, // FP32 RGBA（HDR）
};

// ── 传递函数（HDR 分流依据）──
enum QLensTransfer {
    QLTR_SRGB = 0,    // SDR sRGB
    QLTR_PQ = 1,      // HDR10 PQ (ST.2084)
    QLTR_HLG = 2,     // HLG
    QLTR_LINEAR = 3,  // 线性 scRGB
};

// ── 错误码（query 失败路径）──
enum QLensError {
    QLERR_OK = 0,
    QLERR_NOT_SUPPORTED = 1,  // 格式不支持（无解码器）
    QLERR_CORRUPT = 2,        // 文件损坏
    QLERR_IO = 3,             // IO 错误（读取失败）
};

// ── 像素缓冲（跨 DLL 安全：纯内存，无 D3D 对象）──
struct ImageBuffer {
    int width = 0;
    int height = 0;
    int stride = 0;           // 每行字节
    int format = QLPF_BGRA8;  // QLensPixelFormat
    unsigned char *pixels = nullptr;
    void (*freeFn)(void *pixels) = nullptr;  // 释放函数（插件提供，跨 DLL 安全）
};

// ── 解码信息（query 返回，一次定型覆盖所有坑）──
struct DecodeInfo {
    int  format = QLPF_BGRA8;        // QLensPixelFormat
    int  frames = 1;                 // 帧数（1=静态）
    bool vector = false;             // 无固有尺寸（SVG 等）
    int  suggestW = 0, suggestH = 0; // 建议尺寸（矢量/ICO 用）
    int  rotateW = 0, rotateH = 0;   // EXIF 旋转后显示尺寸（Orientation 6/8 换宽高）
    bool hasAlpha = false;
    int  alphaMode = 0;              // 0=straight, 1=premultiplied
    int  exifRot = 0;                // EXIF Orientation: 0/1/3/6/8
    bool hasICC = false;
    float peakNit = 0.0f;            // HDR 峰值亮度（cd/m²）
    int  transfer = QLTR_SRGB;       // QLensTransfer
    bool gainMap = false;            // Ultra HDR 增益图
    int  frameDelays[256];           // 每帧 delay（ms），最多 256 帧
    int  frameDelayCount = 0;
    int  loopCount = 0;              // 动画循环次数（0=无限）
    int  error = QLERR_OK;           // QLensError
};

// ── 解码器插件 ──
struct DecodePlugin {
    const char *ext;                    // 扩展名（小写，多个用 | 分隔，如 "svg|svgz"）
    const char *signature;              // 可选：文件签名（十六进制字节表，如 "3C3F786D6C"），空=仅扩展名
    bool (*query)(const wchar_t *path, DecodeInfo *info);
    bool (*decode)(const wchar_t *path, int frame, int targetW, int targetH, ImageBuffer *out);
};

// ── 插件注册表 ──
namespace QLensPlugins {

void RegisterDecoder(const DecodePlugin &p);
const DecodePlugin *FindDecoder(const std::wstring &extLower);
const DecodePlugin *FindDecoderBySignature(const unsigned char *magic, int len);
bool LoadPluginsFromDir(const std::wstring &dir);
// 统一解码入口：插件优先，WIC 兜底
bool DecodeAny(const std::wstring &path, int frame, int targetW, int targetH, ImageBuffer *out);

}  // namespace QLensPlugins
