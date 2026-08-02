// D3D11 渲染 —— CPU 双线性缩放 + 全窗口 frame 合成 + 直拷（保留 D3D11 供 HDR 用）
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <math.h>
#include "decoder.h"
#include "thumbstrip.h"
#include "hdr.h"
extern ThumbStrip g_strip;

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> g_dev;
static ComPtr<ID3D11DeviceContext> g_ctx;
static ComPtr<IDXGISwapChain> g_swap;
static ComPtr<ID3D11RenderTargetView> g_rtv;
static int g_w = 1, g_h = 1;
static int g_imgW = 0, g_imgH = 0;
static float g_zoom = 0.0f;  // 0=适配, 1=100%, >1 放大
static unsigned char *g_origPixels = nullptr;

bool g_d3dReady = false;
static bool g_hdrMode = false;  // 当前是否 HDR 输出
static void InitHdrShader();
static ComPtr<ID3D11VertexShader> g_hdrVS;
static ComPtr<ID3D11PixelShader> g_hdrPS;
static ComPtr<ID3D11InputLayout> g_hdrLayout;
static ComPtr<ID3D11Buffer> g_hdrVB;
static ComPtr<ID3D11Buffer> g_hdrCB;
static ComPtr<ID3D11SamplerState> g_hdrSampler;
static ComPtr<ID3D11ShaderResourceView> g_frameSRV;
static ComPtr<ID3D11Texture2D> g_frameTex;
static ComPtr<ID3D11RasterizerState> g_rsNoCull;
void RendererResize(int w, int h);
bool RendererIsHDR() { return g_hdrMode; }

void RendererInit(HWND hwnd)
{
    g_hdrMode = HdrEnabled();

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Format = g_hdrMode ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &scd, &g_swap, &g_dev, nullptr, &g_ctx);
    if (FAILED(hr)) { g_hdrMode = false; return; }
    RECT rc; GetClientRect(hwnd, &rc);
    RendererResize(rc.right, rc.bottom);

    // HDR 时设置 PQ 色彩空间（HDR10）
    if (g_hdrMode) {
        IDXGISwapChain3 *sc3 = nullptr;
        if (SUCCEEDED(g_swap->QueryInterface(__uuidof(IDXGISwapChain3), (void **)&sc3)) && sc3) {
            // scRGB 线性色彩空间（Windows HDR 内部格式，1.0=80nit）
            DXGI_COLOR_SPACE_TYPE cs = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            sc3->SetColorSpace1(cs);
            sc3->Release();
        }
        InitHdrShader();
    }
}

// ── HDR shader 管线（GPU 渲染 HDR 输出）──
// VS：全屏三角形
static const char *g_hdrVS_src = R"(
struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos, 0, 1);
    o.uv = i.uv;
    return o;
}
)";
// PS：采样 BGRA8 纹理 → sRGB→linear→增强→PQ → 输出 HDR
static const char *g_hdrPS_src = R"(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
cbuffer Params : register(b0) {
    float peakNit;
    float pad0, pad1, pad2;
};
static float srgb2lin(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static float pqEnc(float lin) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float Y = max(lin, 1e-6);
    float Yp = pow(Y, m1);
    return pow((c1 + c2 * Yp) / (1.0 + c3 * Yp), m2);
}
static float sdr2hdr(float lin) {
    // 基线：纯线性，白像素 → 峰值（自动检测），无任何曲线
    return lin * peakNit;
}
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 c = tex.Sample(smp, uv);
    float r = srgb2lin(c.r);
    float g = srgb2lin(c.g);
    float b = srgb2lin(c.b);
    // scRGB 输出：线性亮度(nit)/80（scRGB 1.0=80nit）
    return float4(sdr2hdr(r) / 80.0, sdr2hdr(g) / 80.0, sdr2hdr(b) / 80.0, 1.0);
}
)";


// 编译 HDR shader 管线
static void InitHdrShader()
{
    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    HRESULT hr = D3DCompile(g_hdrVS_src, strlen(g_hdrVS_src), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) return;
    hr = D3DCompile(g_hdrPS_src, strlen(g_hdrPS_src), nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) return;

    g_dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_hdrVS);
    g_dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_hdrPS);

    D3D11_INPUT_ELEMENT_DESC desc[2] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_dev->CreateInputLayout(desc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_hdrLayout);

    float verts[] = { -1,-1, 0,1,  3,-1, 2,1,  -1,3, 0,-1 };
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT; bd.ByteWidth = sizeof(verts);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = { verts, 0, 0 };
    g_dev->CreateBuffer(&bd, &init, &g_hdrVB);

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DEFAULT; cbd.ByteWidth = 16;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_dev->CreateBuffer(&cbd, nullptr, &g_hdrCB);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0; sd.MaxLOD = 3.0f;
    g_dev->CreateSamplerState(&sd, &g_hdrSampler);

    // 禁用背面剔除（全屏三角形绕序不确定，避免被剔除 → 黑屏）
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    g_dev->CreateRasterizerState(&rd, &g_rsNoCull);
}

void RendererResize(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    g_w = w; g_h = h;
    g_rtv.Reset();
    if (g_swap) {
        g_swap->ResizeBuffers(2, w, h, g_hdrMode ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        ComPtr<ID3D11Texture2D> back;
        g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
        g_dev->CreateRenderTargetView(back.Get(), nullptr, &g_rtv);
    }
}

void RendererLoadImage(const std::wstring &path)
{
    DecodedImage img;
    if (!DecodeImageFile(path, img)) return;
    if (img.width < 1 || img.height < 1) return;

    // 原图像素（紧凑 BGRA，去 stride 空隙）
    delete[] g_origPixels;
    g_origPixels = new unsigned char[(size_t)img.width * img.height * 4];
    int stride = img.stride ? img.stride : img.width * 4;
    for (int y = 0; y < img.height; ++y)
        memcpy(g_origPixels + (size_t)y * img.width * 4, img.pixels + (size_t)y * stride, (size_t)img.width * 4);
    g_imgW = img.width; g_imgH = img.height;
    // 默认：图小于窗口→100%不放大，图大于窗口→适配缩小
    g_zoom = (g_imgW <= g_w && g_imgH <= g_h) ? 1.0f : 0.0f;
}

void RendererEnsureInit(HWND hwnd)
{
    static bool inited = false;
    if (!inited) { RendererInit(hwnd); inited = true; }
    g_d3dReady = g_swap && g_dev && g_ctx;
}

void RendererSetZoom(float dz)
{
    if (dz == 0.0f) { g_zoom = 0.0f; return; }        // S=全窗口
    else if (dz == 1.0f) { g_zoom = 1.0f; return; }   // F=100%
    g_zoom = (g_zoom == 0.0f) ? 1.0f : g_zoom * dz;
    if (g_zoom > 8.0f) g_zoom = 8.0f;
    if (g_zoom < 0.25f) g_zoom = 0.25f;
}

float RendererZoomFactor() { return g_zoom; }

// ── HDR 像素处理 ──────────────────────────────
// sRGB 8bit → 线性 (0..1)
static inline float srgbToLinear(unsigned char v)
{
    float c = v / 255.0f;
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
// ST.2084 PQ 编码（HDR10）
static inline float pqEncode(float lin)
{
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 32.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 128.0f;
    const float c3 = 2392.0f / 128.0f;
    float Y = fmaxf(lin, 1e-6f);
    float Yp = powf(Y, m1);
    float num = c1 + c2 * Yp;
    float den = 1.0f + c3 * Yp;
    return powf(num / den, m2);
}
// float → FP16 半精度（IEEE 754）
static inline unsigned short FloatToHalf(float f)
{
    unsigned int x;
    memcpy(&x, &f, 4);
    unsigned int sign = (x >> 16) & 0x8000;
    unsigned int exp = (x >> 23) & 0xFF;
    unsigned int mant = x & 0x7FFFFF;
    if (exp == 0xFF) return (unsigned short)(sign | 0x7C00 | (mant ? 0x200 : 0));
    if (exp == 0) return (unsigned short)sign;
    int e = (int)exp - 127 + 15;
    if (e >= 31) return (unsigned short)(sign | 0x7C00);
    if (e <= 0) {
        if (e < -10) return (unsigned short)sign;
        mant |= 0x800000;
        int shift = 14 - e;
        unsigned int half = mant >> shift;
        if (mant & ((1u << shift) - 1)) half++;
        return (unsigned short)(sign | half);
    }
    unsigned int half = (mant >> 13) | ((unsigned int)e << 10);
    if (mant & 0x1FFF) half++;
    return (unsigned short)(sign | half);
}

// SDR→HDR 增强：参考白映射 + 暗部提亮 + 高光软压缩
static inline float sdrToHdr(float lin, float peakNit)
{
    // 基线：纯线性，白 → 峰值
    return lin * peakNit;
}

// HDR LUT：sRGB 8bit → PQ-encoded FP16（预计算增强，避免每像素 powf）
static unsigned short g_hdrLUT[256];
static bool g_hdrLUTReady = false;
static void InitHdrLUT(float peakNit)
{
    if (g_hdrLUTReady) return;
    for (int i = 0; i < 256; ++i) {
        float lin = srgbToLinear((unsigned char)i);
        g_hdrLUT[i] = FloatToHalf(pqEncode(sdrToHdr(lin, peakNit)));
    }
    g_hdrLUTReady = true;
}

void RendererRender()
{
    if (!g_rtv) return;
    float clear[4] = { 0.07f, 0.07f, 0.08f, 1.0f };
    g_ctx->ClearRenderTargetView(g_rtv.Get(), clear);

    // 提前声明（确保 HDR/SDR 分支都可见）
    std::vector<unsigned char> frame;
    D3D11_TEXTURE2D_DESC sd = {};
    ComPtr<ID3D11Texture2D> fullTex;
    ComPtr<ID3D11Texture2D> back;

    int viewH = g_h - THUMB_H;
    if (g_origPixels && g_imgW > 0 && g_imgH > 0 && g_w > 0 && viewH > 0) {
        float aw = (float)g_w / g_imgW, ah = (float)viewH / g_imgH;
        float fit = (aw < ah) ? aw : ah;
        float s = (g_zoom == 0.0f) ? fit : (g_zoom == 1.0f ? 1.0f : fit * g_zoom);
        int sw = (int)(g_imgW * s), sh = (int)(g_imgH * s);
        if (sw < 1) sw = 1; if (sh < 1) sh = 1;

        // CPU 双线性缩放（消除毛边）
        std::vector<unsigned char> scaled((size_t)sw * sh * 4);
        float stepX = 1.0f / s, stepY = 1.0f / s;
        for (int y = 0; y < sh; ++y) {
            float fy = y * stepY;
            int y0 = (int)fy; if (y0 >= g_imgH) y0 = g_imgH - 1;
            int y1 = y0 + 1; if (y1 >= g_imgH) y1 = g_imgH - 1;
            float wy = fy - y0;
            const unsigned char *r0 = g_origPixels + (size_t)y0 * g_imgW * 4;
            const unsigned char *r1 = g_origPixels + (size_t)y1 * g_imgW * 4;
            for (int x = 0; x < sw; ++x) {
                float fx = x * stepX;
                int x0 = (int)fx; if (x0 >= g_imgW) x0 = g_imgW - 1;
                int x1 = x0 + 1; if (x1 >= g_imgW) x1 = g_imgW - 1;
                float wx = fx - x0;
                unsigned char *d = &scaled[((size_t)y * sw + x) * 4];
                for (int ch = 0; ch < 3; ++ch) {
                    float top = r0[x0*4+ch]*(1-wx) + r0[x1*4+ch]*wx;
                    float bot = r1[x0*4+ch]*(1-wx) + r1[x1*4+ch]*wx;
                    d[ch] = (unsigned char)(top*(1-wy) + bot*wy);
                }
                d[3] = 255;
            }
        }

        // 合成全窗口 buffer：主图上部 + 缩略图条底部
        frame.assign((size_t)g_w * g_h * 4, 0);
        int dx = (g_w - sw) / 2, dy = (viewH - sh) / 2;
        if (dx < 0) dx = 0; if (dy < 0) dy = 0;
        for (int y = 0; y < sh && y + dy < viewH; ++y)
            memcpy(&frame[((size_t)(y+dy) * g_w + dx) * 4], &scaled[(size_t)y * sw * 4], (size_t)sw * 4);
        g_strip.renderToBuffer(frame.data(), g_w, g_h);

        sd.Width = g_w; sd.Height = g_h; sd.MipLevels = 1; sd.ArraySize = 1;
        sd.Usage = D3D11_USAGE_IMMUTABLE; sd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        sd.SampleDesc.Count = 1;

        if (g_hdrMode) {
            // HDR：frame(BGRA8) → RGBA 纹理 → shader 渲染（sRGB→linear→增强→PQ）
            // 转 RGBA（交换 B/R，D3D 纹理按 RGBA 解释）
            std::vector<unsigned char> rgba(frame.size());
            for (size_t i = 0; i < frame.size(); i += 4) {
                rgba[i+0] = frame[i+2];  // R
                rgba[i+1] = frame[i+1];  // G
                rgba[i+2] = frame[i];    // B
                rgba[i+3] = frame[i+3];
            }
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = g_w; td.Height = g_h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA tInit = { rgba.data(), (UINT)(g_w*4), 0 };
            g_frameTex.Reset(); g_frameSRV.Reset();
            { HRESULT h1 = g_dev->CreateTexture2D(&td, &tInit, &g_frameTex);
              HRESULT h2 = SUCCEEDED(h1) ? g_dev->CreateShaderResourceView(g_frameTex.Get(), nullptr, &g_frameSRV) : h1;
              if (SUCCEEDED(h1) && SUCCEEDED(h2) && g_hdrVS && g_hdrPS) {
                // 常量缓冲：peak
                float peak = HdrDisplayPeakBrightness();
                if (peak <= 0.0f) peak = 1000.0f;
                float cbData[4] = { peak, 0, 0, 0 };
                g_ctx->UpdateSubresource(g_hdrCB.Get(), 0, nullptr, cbData, 0, 0);

                g_ctx->OMSetRenderTargets(1, g_rtv.GetAddressOf(), nullptr);
                D3D11_VIEWPORT vp = { 0,0,(float)g_w,(float)g_h,0,1 };
                g_ctx->RSSetViewports(1, &vp);
                g_ctx->RSSetState(g_rsNoCull.Get());
                UINT stride = 16, offset = 0;
                g_ctx->IASetVertexBuffers(0, 1, g_hdrVB.GetAddressOf(), &stride, &offset);
                g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                g_ctx->IASetInputLayout(g_hdrLayout.Get());
                g_ctx->VSSetShader(g_hdrVS.Get(), nullptr, 0);
                g_ctx->PSSetShader(g_hdrPS.Get(), nullptr, 0);
                g_ctx->PSSetShaderResources(0, 1, g_frameSRV.GetAddressOf());
                g_ctx->PSSetSamplers(0, 1, g_hdrSampler.GetAddressOf());
                g_ctx->PSSetConstantBuffers(0, 1, g_hdrCB.GetAddressOf());
                g_ctx->Draw(3, 0);
                }
            }
            }
        else {
            // SDR：BGRA8 直拷
            sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            D3D11_SUBRESOURCE_DATA sInit = { frame.data(), (UINT)(g_w*4), 0 };
            if (SUCCEEDED(g_dev->CreateTexture2D(&sd, &sInit, &fullTex)) &&
                SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
                D3D11_BOX box = { 0,0,0,(UINT)g_w,(UINT)g_h,1 };
                g_ctx->CopySubresourceRegion(back.Get(), 0, 0, 0, 0, fullTex.Get(), 0, &box);
            }
        }
    }
    g_swap->Present(1, 0);
}
