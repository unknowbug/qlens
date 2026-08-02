// WIC 解码实现（ComPtr RAII，避免手动 Release 错误）
#include "decoder.h"
#include <wincodec.h>
#include <wrl/client.h>
#include "plugins.h"

using Microsoft::WRL::ComPtr;

bool DecodeImageFile(const std::wstring &path, DecodedImage &out)
{
    // 先试插件解码（WIC 不支持的格式）
    if (PluginDecodeFallback(path, out)) return true;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) return false;

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (w < 1 || h < 1) return false;

    ComPtr<IWICFormatConverter> conv;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr) || !conv) return false;

    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    out.width = (int)w;
    out.height = (int)h;
    out.pixels = new unsigned char[(size_t)w * h * 4];
    hr = conv->CopyPixels(nullptr, w * 4, w * h * 4, out.pixels);
    return SUCCEEDED(hr);
}

// 缩略解码：WIC 直接解码到目标尺寸（比全尺寸解码快得多）
bool DecodeImageThumb(const std::wstring &path, DecodedImage &out, int targetW, int targetH)
{
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) return false;

    UINT fw = 0, fh = 0;
    frame->GetSize(&fw, &fh);
    if (fw < 1 || fh < 1) return false;

    // 计算保持比例的缩放
    UINT sw = fw, sh = fh;
    if (fw > (UINT)targetW || fh > (UINT)targetH) {
        float s = (float)targetW / fw;
        if ((float)targetH / fh < s) s = (float)targetH / fh;
        sw = (UINT)(fw * s); sh = (UINT)(fh * s);
        if (sw < 1) sw = 1; if (sh < 1) sh = 1;
    }

    ComPtr<IWICBitmapScaler> scaler;
    hr = factory->CreateBitmapScaler(&scaler);
    if (FAILED(hr) || !scaler) return false;
    hr = scaler->Initialize(frame.Get(), sw, sh, WICBitmapInterpolationModeLinear);
    if (FAILED(hr)) return false;

    ComPtr<IWICFormatConverter> conv;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr) || !conv) return false;
    hr = conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    out.width = (int)sw;
    out.height = (int)sh;
    UINT stride = (sw * 4 + 3) & ~3u;
    out.stride = (int)stride;
    out.pixels = new unsigned char[(size_t)stride * sh];
    hr = conv->CopyPixels(nullptr, stride, stride * sh, out.pixels);
    return SUCCEEDED(hr);
}

// 插件解码回退：按扩展名查插件
bool PluginDecodeFallback(const std::wstring &path, DecodedImage &out)
{
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot + 1);
    for (auto &ch : ext) if (ch >= L'A' && ch <= L'Z') ch += 32;

    const DecodePlugin *dp = QLensPlugins::FindDecoder(ext);
    if (!dp || !dp->decode) return false;

    ImageBuffer buf = {};
    bool decOK = dp->decode(path.c_str(), &buf);
    if (!decOK) return false;
    if (buf.width < 1 || buf.height < 1 || !buf.pixels) { if (dp->release) dp->release(&buf); return false; }

    out.width = buf.width;
    out.height = buf.height;
    int stride = buf.stride ? buf.stride : buf.width * 4;
    out.stride = buf.width * 4;
    out.pixels = new unsigned char[(size_t)out.width * out.height * 4];
    for (int y = 0; y < out.height; ++y)
        memcpy(out.pixels + (size_t)y * out.stride, buf.pixels + (size_t)y * stride, (size_t)out.stride);
    if (dp->release) dp->release(&buf);
    return true;
}
