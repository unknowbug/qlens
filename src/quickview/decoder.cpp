// WIC 解码实现（ComPtr RAII，避免手动 Release 错误）
// 新接口：QueryImageInfo（能力探测）+ DecodeImageAny（按需解码，插件优先）
#include "decoder.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <math.h>

using Microsoft::WRL::ComPtr;

// ── 工具：EXIF Orientation 读取（WIC metadata reader）──
static int ReadExifOrientation(IWICBitmapFrameDecode *frame)
{
    ComPtr<IWICMetadataQueryReader> mr;
    if (FAILED(frame->GetMetadataQueryReader(&mr)) || !mr) return 0;
    PROPVARIANT val;
    PropVariantInit(&val);
    HRESULT hr = mr->GetMetadataByName(L"/app1/ifd/{ushort=274}", &val);  // Orientation
    int rot = 0;
    if (SUCCEEDED(hr) && val.vt == VT_UI2) {
        switch (val.uiVal) {
            case 1: rot = 1; break;   // 正常
            case 3: rot = 3; break;   // 180
            case 6: rot = 6; break;   // 右旋 90
            case 8: rot = 8; break;   // 左旋 90
            default: rot = 1; break;
        }
    }
    PropVariantClear(&val);
    return rot;
}

// ── 能力查询：WIC 探测尺寸/帧数/EXIF/ICC ──
static bool WicQuery(const std::wstring &path, DecodeInfo &info)
{
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        // 区分错误：文件打不开=IO，格式无解码器=NOT_SUPPORTED，其他=损坏
        if (HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) == hr || HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) == hr ||
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) == hr || HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) == hr)
            info.error = QLERR_IO;
        else if (hr == WINCODEC_ERR_UNSUPPORTEDOPERATION || hr == 0x88982F03 /* WINCODEC_ERR_UNSUPPORTEDOPERATION */)
            info.error = QLERR_NOT_SUPPORTED;
        else
            info.error = QLERR_CORRUPT;
        return false;
    }

    UINT nFrames = 0;
    decoder->GetFrameCount(&nFrames);
    if (nFrames < 1) { info.error = QLERR_CORRUPT; return false; }
    info.frames = (int)nFrames;

    // GIF 动画元数据：每帧 delay（GCE）/ 循环次数（NETSCAPE）
    if (nFrames > 1) {
        info.frameDelayCount = (int)min(nFrames, 256u);
        for (UINT i = 0; i < (UINT)info.frameDelayCount; ++i) {
            ComPtr<IWICBitmapFrameDecode> fr;
            if (SUCCEEDED(decoder->GetFrame(i, &fr)) && fr) {
                ComPtr<IWICMetadataQueryReader> mr;
                if (SUCCEEDED(fr->GetMetadataQueryReader(&mr)) && mr) {
                    PROPVARIANT v; PropVariantInit(&v);
                    // GIF 帧 delay 单位 1/100s
                    if (SUCCEEDED(mr->GetMetadataByName(L"/grctlext/Delay", &v)) && v.vt == VT_UI2)
                        info.frameDelays[info.frameDelayCount > (int)i ? i : info.frameDelayCount-1] = v.uiVal * 10;
                    PropVariantClear(&v);
                }
            }
        }
        // loopCount：NETSCAPE2.0 扩展
        ComPtr<IWICMetadataQueryReader> mr;
        if (SUCCEEDED(decoder->GetMetadataQueryReader(&mr)) && mr) {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(mr->GetMetadataByName(L"/appext/Data", &v)) && v.vt == (VT_UI1 | VT_VECTOR) && v.caub.cElems >= 3) {
                // NETSCAPE2.0: 数据字节 [1] 循环数低位 [2] 高位；0=无限
                unsigned loops = (unsigned)v.caub.pElems[1] | ((unsigned)v.caub.pElems[2] << 8);
                info.loopCount = (loops == 0) ? 0 : (int)loops;
            }
            PropVariantClear(&v);
        }
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) { info.error = QLERR_CORRUPT; return false; }

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (w < 1 || h < 1) { info.error = QLERR_CORRUPT; return false; }

    // 像素格式（探测是否高位深/HDR）
    WICPixelFormatGUID pf = {};
    frame->GetPixelFormat(&pf);
    if (pf == GUID_WICPixelFormat64bppRGBA || pf == GUID_WICPixelFormat64bppBGRA ||
        pf == GUID_WICPixelFormat48bppRGB || pf == GUID_WICPixelFormat64bppPRGBA ||
        pf == GUID_WICPixelFormat32bppRGBA1010102 || pf == GUID_WICPixelFormat32bppRGBA1010102XR)
        info.format = QLPF_RGBA16F;  // 高位深源，解码时转 16F
    else
        info.format = QLPF_BGRA8;

    info.suggestW = (int)w;
    info.suggestH = (int)h;
    info.exifRot = ReadExifOrientation(frame.Get());
    // EXIF 旋转后显示尺寸（Orientation 6/8 = 90° 换宽高）
    info.rotateW = (info.exifRot == 6 || info.exifRot == 8) ? (int)h : (int)w;
    info.rotateH = (info.exifRot == 6 || info.exifRot == 8) ? (int)w : (int)h;
    info.hasAlpha = (pf != GUID_WICPixelFormat24bppRGB && pf != GUID_WICPixelFormat32bppBGR &&
                    pf != GUID_WICPixelFormat24bppBGR && pf != GUID_WICPixelFormat8bppGray);
    info.error = QLERR_OK;
    return true;
}

// ── 能力查询：统一入口（插件 query 优先，WIC 兜底）──
bool QueryImageInfo(const std::wstring &path, DecodeInfo &info)
{
    // 插件 query 优先
    size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = path.substr(dot + 1);
        for (auto &ch : ext) if (ch >= L'A' && ch <= L'Z') ch += 32;
        const DecodePlugin *dp = QLensPlugins::FindDecoder(ext);
        if (dp && dp->query) {
            DecodeInfo pi = {};
            if (dp->query(path.c_str(), &pi)) { info = pi; return true; }
        }
    }
    return WicQuery(path, info);
}

// ── 按需解码（插件优先，WIC 兜底）──
bool DecodeImageAny(const std::wstring &path, int frame, int targetW, int targetH, ImageBuffer &out)
{
    if (QLensPlugins::DecodeAny(path, frame, targetW, targetH, &out)) return true;

    // WIC 解码（含高位深转换）
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return false;

    ComPtr<IWICBitmapFrameDecode> f0;
    hr = decoder->GetFrame((UINT)frame, &f0);
    if (FAILED(hr) || !f0) return false;

    UINT w = 0, h = 0;
    f0->GetSize(&w, &h);
    if (w < 1 || h < 1) return false;

    // 目标尺寸缩放
    ComPtr<IWICBitmapSource> src = f0;
    if (targetW > 0 && targetH > 0 && ((int)w > targetW || (int)h > targetH)) {
        float s = (float)targetW / w;
        if ((float)targetH / h < s) s = (float)targetH / h;
        UINT sw = (UINT)(w * s), sh = (UINT)(h * s);
        if (sw < 1) sw = 1; if (sh < 1) sh = 1;
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && scaler) {
            if (SUCCEEDED(scaler->Initialize(f0.Get(), sw, sh, WICBitmapInterpolationModeLinear)) && scaler) {
                src = scaler;
                w = sw; h = sh;
            }
        }
    }

    // 像素格式：源高位深 → RGBA16F，否则 BGRA8
    WICPixelFormatGUID pf = {};
    f0->GetPixelFormat(&pf);
    bool hi = (pf == GUID_WICPixelFormat64bppRGBA || pf == GUID_WICPixelFormat64bppBGRA ||
               pf == GUID_WICPixelFormat48bppRGB || pf == GUID_WICPixelFormat64bppPRGBA ||
               pf == GUID_WICPixelFormat32bppRGBA1010102 || pf == GUID_WICPixelFormat32bppRGBA1010102XR);

    out.width = (int)w;
    out.height = (int)h;
    if (hi) {
        // 高位深 → RGBA16F（HDR 路径）
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(factory->CreateFormatConverter(&conv)) || !conv) return false;
        hr = conv->Initialize(src.Get(), GUID_WICPixelFormat64bppRGBAHalf,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return false;
        out.format = QLPF_RGBA16F;
        out.stride = (int)w * 8;
        unsigned char *pix = new (std::nothrow) unsigned char[(size_t)w * h * 8];
        if (!pix) return false;
        hr = conv->CopyPixels(nullptr, (UINT)out.stride, (UINT)((size_t)w * h * 8), pix);
        if (FAILED(hr)) { delete[] pix; return false; }
        out.pixels = pix;
        out.freeFn = [](void *p) { delete[] (unsigned char*)p; };
    } else {
        // SDR → BGRA8
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(factory->CreateFormatConverter(&conv)) || !conv) return false;
        hr = conv->Initialize(src.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return false;
        out.format = QLPF_BGRA8;
        UINT stride = (w * 4 + 3) & ~3u;
        out.stride = (int)stride;
        unsigned char *pix = new (std::nothrow) unsigned char[(size_t)stride * h];
        if (!pix) return false;
        hr = conv->CopyPixels(nullptr, stride, stride * h, pix);
        if (FAILED(hr)) { delete[] pix; return false; }
        out.pixels = pix;
        out.freeFn = [](void *p) { delete[] (unsigned char*)p; };
    }
    return true;
}

// ── 兼容旧接口：全尺寸 BGRA8 解码（内部走新接口 + 转换，frame 指定多帧格式帧）──
bool DecodeImageFile(const std::wstring &path, DecodedImage &out, int frame)
{
    ImageBuffer ib = {};
    if (!DecodeImageAny(path, frame, 0, 0, ib)) return false;
    // 16F → 转 BGRA8（HDR→SDR tone map，保留高光）
    if (ib.format == QLPF_RGBA16F) {
        out.width = ib.width; out.height = ib.height;
        out.stride = ib.width * 4;
        out.pixels = new unsigned char[(size_t)ib.width * ib.height * 4];
        const unsigned short *src = (const unsigned short*)ib.pixels;
        // FP16 (half) → float
        auto half2float = [](unsigned short h) -> float {
            unsigned sign = (h >> 15) & 1;
            unsigned exp = (h >> 10) & 0x1F;
            unsigned mant = h & 0x3FF;
            if (exp == 0) return (mant == 0) ? 0.0f : ldexpf((float)mant, -24) * (sign ? -1 : 1);
            if (exp == 31) return (mant == 0) ? INFINITY : NAN;
            return ldexpf((float)(mant | 0x400), (int)exp - 25) * (sign ? -1 : 1);
        };
        // 线性 → sRGB
        auto lin2srgb = [](float v) -> float {
            if (v <= 0.0031308f) return v * 12.92f;
            return 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
        };
        // Reinhard tone map：把 HDR 线性值压到 [0,1]（保留高光细节，不过曝成白板）
        auto toneMap = [](float lin) -> float {
            if (lin <= 0.0f) return 0.0f;
            return lin / (1.0f + lin);
        };
        for (size_t i = 0; i < (size_t)ib.width * ib.height; ++i) {
            float r = half2float(src[i*4+0]);
            float g = half2float(src[i*4+1]);
            float b = half2float(src[i*4+2]);
            float a = half2float(src[i*4+3]);
            auto c8 = [&](float v) -> unsigned char {
                float t = toneMap(v);
                float s = lin2srgb(t);
                if (s < 0) s = 0; if (s > 1) s = 1;
                return (unsigned char)(s * 255);
            };
            unsigned char *d = out.pixels + i * 4;
            d[0] = c8(b);  // BGRA
            d[1] = c8(g);
            d[2] = c8(r);
            d[3] = (unsigned char)((a < 0 ? 0 : a > 1 ? 1 : a) * 255);
        }
        if (ib.freeFn) ib.freeFn(ib.pixels);
        return true;
    }
    // BGRA8 直接拷
    out.width = ib.width; out.height = ib.height;
    out.stride = ib.width * 4;
    out.pixels = new unsigned char[(size_t)ib.width * ib.height * 4];
    for (int y = 0; y < ib.height; ++y)
        memcpy(out.pixels + (size_t)y * out.stride, ib.pixels + (size_t)y * ib.stride, (size_t)ib.width * 4);
    if (ib.freeFn) ib.freeFn(ib.pixels);
    return true;
}

// ── 兼容旧接口：缩略解码 ──
bool DecodeImageThumb(const std::wstring &path, DecodedImage &out, int targetW, int targetH)
{
    ImageBuffer ib = {};
    if (!DecodeImageAny(path, 0, targetW, targetH, ib)) return false;
    if (ib.format == QLPF_RGBA16F) {
        // 16F 缩略图 → BGRA8（同样钳制）
        return DecodeImageFile(path, out);  // 简单回退：全尺寸再转（缩略场景可接受）
    }
    out.width = ib.width; out.height = ib.height;
    out.stride = ib.width * 4;
    out.pixels = new unsigned char[(size_t)ib.width * ib.height * 4];
    for (int y = 0; y < ib.height; ++y)
        memcpy(out.pixels + (size_t)y * out.stride, ib.pixels + (size_t)y * ib.stride, (size_t)ib.width * 4);
    if (ib.freeFn) ib.freeFn(ib.pixels);
    return true;
}

// ── 兼容旧接口：插件解码回退 ──
bool PluginDecodeFallback(const std::wstring &path, DecodedImage &out)
{
    ImageBuffer ib = {};
    if (!QLensPlugins::DecodeAny(path, 0, 0, 0, &ib)) return false;
    out.width = ib.width; out.height = ib.height;
    out.stride = ib.width * 4;
    out.pixels = new unsigned char[(size_t)ib.width * ib.height * 4];
    for (int y = 0; y < ib.height; ++y)
        memcpy(out.pixels + (size_t)y * out.stride, ib.pixels + (size_t)y * ib.stride, (size_t)ib.width * 4);
    if (ib.freeFn) ib.freeFn(ib.pixels);
    return true;
}
