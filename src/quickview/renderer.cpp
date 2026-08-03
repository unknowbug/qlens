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
#include <atomic>
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
static int g_rotation = 0;  // 0/1/2/3 = 0/90/180/270 度
static int g_exifRot = 0;   // EXIF Orientation（自动旋转，渲染时叠加）
static bool g_isHdrImage = false;  // 源图是高位深（真 HDR 图）
static float g_highRatio = 0.0f;   // 高光像素比例（>0.8 亮度占比，亮图降高光）

bool g_d3dReady = false;
static bool g_hdrMode = false;  // 当前是否 HDR 输出
// 按钮：◀ ▶ Manage（自动隐藏）
static bool g_btnVisible = false;
static int g_btnPrevX = 0, g_btnPrevY = 0;   // ◀ 位置
static int g_btnNextX = 0, g_btnNextY = 0;   // ▶ 位置
static int g_btnMgrX = 0, g_btnMgrY = 0;     // Manage 位置
static int g_btnW = 44, g_btnH = 36;         // 按钮尺寸
static int g_closeX = 0, g_closeY = 0, g_closeW = 46, g_closeH = 32;  // 右上角关闭按钮
static bool g_closeHover = false;
static int g_hoverBtn = 0;  // 0=无 1=◀ 2=▶ 3=Manage 4=关闭
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
    float hdrSource;   // 1=源是真 HDR 图（已 tone map 到 SDR 范围，不二次增强）
    float highRatio;   // 高光像素比例（>0.8 亮度占比，亮图自动降高光）
    float pad0;
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
    // 自适应：亮图（highRatio 高）时高光软压缩，保留层次不过曝
    // 纯线性基线：lin * peakNit；亮图高光部分降低增益
    float boost = 1.0 - 0.35 * highRatio;  // 高光比例越高，整体增益越低
    float res = lin * peakNit * boost;
    // 高光软压缩：>0.7 的部分渐近压缩（避免顶到峰值纯白）
    float t = saturate((lin - 0.7) / 0.3);       // 0.7~1.0 过渡
    float compressed = 0.7 * peakNit * boost + (lin - 0.7) * peakNit * boost * 0.55;
    res = lerp(res, compressed, t * highRatio);
    return res;
}
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 c = tex.Sample(smp, uv);
    if (hdrSource > 0.5) {
        // 真 HDR 图：已 tone map 到 SDR 范围（0..1），直接按 scRGB 显示（1.0=80nit）
        // 不二次增强、不映射峰值（否则过曝）——保留 tone map 后的层次
        return float4(c.r, c.g, c.b, 1.0);
    }
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
    // Query：EXIF 旋转 + 失败分类（先于解码）
    DecodeInfo info;
    if (QueryImageInfo(path, info)) {
        if (info.error == QLERR_NOT_SUPPORTED) return;  // 不支持：不显示（后续 UI 提示）
        // EXIF 自动旋转：解码前记录方向，渲染时应用
        g_exifRot = info.exifRot;
        g_isHdrImage = (info.format == QLPF_RGBA16F || info.format == QLPF_RGBA32F);
    } else {
        g_exifRot = 0;
        g_isHdrImage = false;
    }

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

// ── 异步解码接口（#12：主图解码移后台线程 + 取消）──
// 后台线程调用：解码到独立缓冲（不碰渲染状态，线程安全）
bool RendererDecodeToBuffer(const std::wstring &path, int frame, unsigned char **outPix, int *outW, int *outH, int *outRot, bool *outHdr, float *outHighRatio)
{
    if (!outPix || !outW || !outH || !outRot || !outHdr || !outHighRatio) return false;
    *outPix = nullptr;
    *outHdr = false;
    *outHighRatio = 0.0f;

    // Query：EXIF 旋转 + 失败分类（后台线程安全）
    DecodeInfo info;
    int exifRot = 0;
    if (QueryImageInfo(path, info)) {
        if (info.error == QLERR_NOT_SUPPORTED) return false;
        exifRot = info.exifRot;
        *outHdr = (info.format == QLPF_RGBA16F || info.format == QLPF_RGBA32F);
    }

    DecodedImage img;
    if (!DecodeImageFile(path, img, frame)) return false;
    if (img.width < 1 || img.height < 1) return false;

    // 紧凑 BGRA（去 stride 空隙）
    unsigned char *pix = new unsigned char[(size_t)img.width * img.height * 4];
    int stride = img.stride ? img.stride : img.width * 4;
    for (int y = 0; y < img.height; ++y)
        memcpy(pix + (size_t)y * img.width * 4, img.pixels + (size_t)y * stride, (size_t)img.width * 4);

    // 统计高光比例（>0.8 亮度占比）——用于亮图自动降高光
    {
        long long hi = 0, total = (long long)img.width * img.height;
        if (total > 0) {
            for (long long i = 0; i < total; ++i) {
                const unsigned char *p = pix + (size_t)i * 4;
                int lum = ((int)p[0] + (int)p[1] + (int)p[2]) / 3;
                if (lum > 204) hi++;  // 0.8 * 255
            }
            *outHighRatio = (float)hi / (float)total;
        }
    }

    *outPix = pix;
    *outW = img.width;
    *outH = img.height;
    *outRot = exifRot;
    return true;
}

// UI 线程调用：提交解码结果到渲染状态（替换旧图）
void RendererCommitImage(unsigned char *pix, int w, int h, int exifRot, bool isHdr, float highRatio)
{
    if (!pix || w < 1 || h < 1) return;
    delete[] g_origPixels;
    g_origPixels = pix;  // 所有权转移
    g_imgW = w; g_imgH = h;
    g_exifRot = exifRot;
    g_isHdrImage = isHdr;
    g_highRatio = highRatio;
    g_rotation = 0;  // 新图重置手动旋转（Q/E 只对当前图）
    // 默认：图小于窗口→100%不放大，图大于窗口→适配缩小
    g_zoom = (g_imgW <= g_w && g_imgH <= g_h) ? 1.0f : 0.0f;
}

// 取消令牌：每次请求递增
static std::atomic<int> g_requestId{0};
int RendererNextRequestId() { return ++g_requestId; }

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
// 旋转（1=右旋90, 3=左旋90, 2=180）
void RendererRotate(int steps)
{
    g_rotation = (g_rotation + steps + 4) % 4;
}
int RendererRotation() { return g_rotation; }

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

// 按钮显隐控制
void RendererShowButtons(bool show) { g_btnVisible = show; }
int RendererHitTestButton(int x, int y);  // 前向声明
// 更新 hover（鼠标移动时调用）
void RendererUpdateHover(int x, int y)
{
    g_hoverBtn = RendererHitTestButton(x, y);
}
int RendererCurrentHover() { return g_hoverBtn; }
bool RendererButtonsVisible() { return g_btnVisible; }
// 点击检测：返回 0=无 1=◀ 2=▶ 3=Manage
int RendererHitTestButton(int x, int y)
{
    // 位置实时计算（需要窗口尺寸，从 g_w/g_h 得）
    int winW = g_w, winH = g_h;
    // 关闭按钮（右上角圆弧切口，常驻）
    int closeX = winW - 60, closeY = 0;
    if (x >= closeX && x < winW && y >= closeY && y < 60)
        return 4;
    if (!g_btnVisible) return 0;
    int y0 = winH - 64 - 60;
    int cx = winW / 2;
    int mgrX = cx - 30, prevX = cx - 82, nextX = cx + 38;
    auto hit = [&](int bx, int by, int bw) {
        return x >= bx && x < bx + bw && y >= y0 && y < y0 + g_btnH;
    };
    if (hit(prevX, y0, 44)) return 1;
    if (hit(nextX, y0, 44)) return 2;
    if (hit(mgrX, y0, 60)) return 3;
    return 0;
}

// 用 GDI 渲染单个按钮到内存 DIB（纯色背景 + 文字，无形状装饰）
static void RenderButtonGDI(unsigned char *frame, int winW, int winH,
                            int bx, int by, int bw, int bh,
                            const wchar_t *text, bool hover,
                            int cornerR = 8, int cornerRY = 8)
{
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { DeleteObject(bmp); DeleteDC(memDC); ReleaseDC(nullptr, screenDC); return; }
    HGDIOBJ oldBmp = SelectObject(memDC, bmp);

    // 纯色背景（hover 变亮）
    int c = hover ? 90 : 60;
    HBRUSH bg = CreateSolidBrush(RGB(c, c, c + 6));
    HGDIOBJ oldBr = SelectObject(memDC, bg);
    if (cornerR > 100) {
        // 关闭按钮：窗口外圆切入（保留形状）
        float ccx = winW + 6.0f, ccy = -24.0f, cr = 60.0f;
        for (int yy = 0; yy < 60; ++yy)
            for (int xx = winW - 60; xx < winW; ++xx) {
                float dx = xx + 0.5f - ccx, dy = yy + 0.5f - ccy;
                if (dx*dx + dy*dy <= cr*cr) {
                    unsigned char *d = (unsigned char*)bits + ((size_t)(yy-by) * bw + (xx-bx)) * 4;
                    d[0]=(unsigned char)c; d[1]=(unsigned char)c; d[2]=(unsigned char)(c+6); d[3]=255;
                }
            }
    } else {
        // 标准矩形（无圆角）
        Rectangle(memDC, 0, 0, bw, bh);
    }
    SelectObject(memDC, oldBr);
    DeleteObject(bg);

    // 文字（白色，系统字体）
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, hover ? RGB(255, 255, 255) : RGB(220, 220, 220));
    HFONT font = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldF = SelectObject(memDC, font);
    RECT tr = { 0, 0, bw, bh };
    DrawTextW(memDC, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(memDC, oldF);
    DeleteObject(font);

    // 合进 frame（半透明 alpha 0.6，透出主图但干净）
    for (int yy = 0; yy < bh; ++yy)
        for (int xx = 0; xx < bw; ++xx) {
            int fx = bx + xx, fy = by + yy;
            if (fx < 0 || fx >= winW || fy < 0 || fy >= winH) continue;
            unsigned char *src = (unsigned char*)bits + ((size_t)yy * bw + xx) * 4;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0) continue;
            unsigned char *dst = frame + ((size_t)fy * winW + fx) * 4;
            dst[0] = (unsigned char)(src[0]*0.6 + dst[0]*0.4);
            dst[1] = (unsigned char)(src[1]*0.6 + dst[1]*0.4);
            dst[2] = (unsigned char)(src[2]*0.6 + dst[2]*0.4);
            dst[3]=255;
        }

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// 绘制按钮到 frame（缩略图条上方居中）
static void DrawButtons(unsigned char *frame, int winW, int winH)
{
    // 右上角关闭按钮（常驻，无边框窗口需要）
    // 直接从窗口外切圆弧：圆心在 (winW+40, -40)，半径 80，窗口内区域 = 圆弧切口
    g_closeX = winW - 60; g_closeY = 0;
    g_closeW = 60; g_closeH = 60;
    {
        // 圆心 (winW+22, -22) 半径 70：窗口右上角被圆切入（适中）
        float ccx = winW + 6.0f, ccy = -24.0f, cr = 60.0f;
        int c = g_hoverBtn == 4 ? 85 : 55;
        for (int yy = 0; yy < 60; ++yy)
            for (int xx = winW - 60; xx < winW; ++xx) {
                if (xx < 0 || xx >= winW || yy < 0 || yy >= winH) continue;
                float dx = xx + 0.5f - ccx, dy = yy + 0.5f - ccy;
                if (dx*dx + dy*dy <= cr*cr) {
                    unsigned char *d = frame + ((size_t)yy * winW + xx) * 4;
                    d[0] = (unsigned char)((c)*0.7 + d[0]*0.3);
                    d[1] = (unsigned char)((c)*0.7 + d[1]*0.3);
                    d[2] = (unsigned char)((c+6)*0.7 + d[2]*0.3);
                    d[3] = 255;
                }
            }
        // 画 ✕（圆弧内，细线缩小，像关闭符）
        int cx0 = winW - 18, cy0 = 14;  // X 中心（最终位置）
        int xc = g_hoverBtn == 4 ? 190 : 140;
        for (int t = -4; t <= 4; ++t) {
            for (int s = 0; s < 2; ++s) {
                int x = cx0 + (s ? t : -t), y = cy0 + t;
                if (x < 0 || x >= winW || y < 0 || y >= winH) continue;
                float dx = x + 0.5f - ccx, dy = y + 0.5f - ccy;
                if (dx*dx + dy*dy > cr*cr) continue;
                unsigned char *d = frame + ((size_t)y * winW + x) * 4;
                d[0]=(unsigned char)(xc*0.6 + d[0]*0.4);
                d[1]=(unsigned char)(xc*0.6 + d[1]*0.4);
                d[2]=(unsigned char)(xc*0.6 + d[2]*0.4);
                d[3]=255;
            }
        }
    }
    if (!g_btnVisible) return;
    if (!g_btnVisible) return;
    int y0 = winH - 64 - 60;  // 缩略图条上方 60px
    int cx = winW / 2;
    // 对称：Manage 中心 cx，箭头中心距 cx ±60px
    g_btnMgrX  = cx - 30; g_btnMgrY  = y0;   // Manage 宽60，中心 cx
    g_btnPrevX = cx - 82; g_btnPrevY = y0;   // ◀ 宽44，中心 cx-60
    g_btnNextX = cx + 38; g_btnNextY = y0;   // ▶ 宽44，中心 cx+60
    RenderButtonGDI(frame, winW, winH, g_btnPrevX, g_btnPrevY, 44, 36, L"\u25C0", g_hoverBtn == 1, 14, 14);
    RenderButtonGDI(frame, winW, winH, g_btnMgrX,  g_btnMgrY,  60, 36, L"Manage", g_hoverBtn == 3, 14, 14);
    RenderButtonGDI(frame, winW, winH, g_btnNextX, g_btnNextY, 44, 36, L"\u25B6", g_hoverBtn == 2, 14, 14);
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
        // 总旋转：EXIF 自动 + 手动 Q/E 叠加
        int rotAll = (g_rotation + (g_exifRot == 6 ? 1 : g_exifRot == 8 ? 3 : g_exifRot == 3 ? 2 : 0)) % 4;
        bool swap = (rotAll == 1) || (rotAll == 3);
        int dispW = swap ? g_imgH : g_imgW;
        int dispH = swap ? g_imgW : g_imgH;
        float aw = (float)g_w / dispW, ah = (float)viewH / dispH;
        float fit = (aw < ah) ? aw : ah;
        float s = (g_zoom == 0.0f) ? fit : (g_zoom == 1.0f ? 1.0f : fit * g_zoom);
        int sw = (int)(dispW * s), sh = (int)(dispH * s);
        if (sw < 1) sw = 1; if (sh < 1) sh = 1;

        // CPU 双线性缩放（消除毛边），采样时应用旋转
        std::vector<unsigned char> scaled((size_t)sw * sh * 4);
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                // 目标 (x,y) → 原图采样坐标（旋转映射）
                float u, v;
                switch (rotAll) {
                    case 1:  // 右旋90
                        u = (float)y / sh; v = 1.0f - (float)x / sw; break;
                    case 3:  // 左旋90
                        u = 1.0f - (float)y / sh; v = (float)x / sw; break;
                    case 2:  // 180
                        u = 1.0f - (float)x / sw; v = 1.0f - (float)y / sh; break;
                    default:
                        u = (float)x / sw; v = (float)y / sh; break;
                }
                float sx = u * g_imgW, sy = v * g_imgH;
                int x0 = (int)sx; if (x0 >= g_imgW) x0 = g_imgW - 1;
                int x1 = x0 + 1; if (x1 >= g_imgW) x1 = g_imgW - 1;
                int y0 = (int)sy; if (y0 >= g_imgH) y0 = g_imgH - 1;
                int y1 = y0 + 1; if (y1 >= g_imgH) y1 = g_imgH - 1;
                float wx = sx - x0, wy = sy - y0;
                const unsigned char *r0 = g_origPixels + (size_t)y0 * g_imgW * 4;
                const unsigned char *r1 = g_origPixels + (size_t)y1 * g_imgW * 4;
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
        DrawButtons(frame.data(), g_w, g_h);

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
                float cbData[4] = { peak, g_isHdrImage ? 1.0f : 0.0f, g_highRatio, 0 };
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
