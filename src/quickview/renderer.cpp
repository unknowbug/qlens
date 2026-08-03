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

// half <-> float（scRGB 线性半精度）——位操作快速版
static inline float rHalf2Float(unsigned short h) {
    // half 位布局 → float（scRGB 范围 0-8 无 inf/nan 场景）
    unsigned sign = (h & 0x8000u) << 16;
    unsigned exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
    if (exp == 0) return mant == 0 ? 0.0f
        : (float)mant * (sign ? -1.0f : 1.0f) * 5.9604645e-8f;  // 次正规
    unsigned fe = exp - 15 + 127;
    unsigned b = sign | (fe << 23) | (mant << 13);
    float f; memcpy(&f, &b, 4);
    return f;
}
static inline unsigned short rFloat2Half(float f) {
    // float → half（位操作，忽略次正规下溢——scRGB 值 ≥0 不会过小）
    unsigned b; memcpy(&b, &f, 4);
    unsigned sign = (b >> 16) & 0x8000u;
    int e = (int)((b >> 23) & 0xFFu) - 127 + 15;
    unsigned m = (b >> 13) & 0x3FFu;
    if (e >= 31) return (unsigned short)(sign | 0x7BFFu);  // 溢出 → inf
    if (e <= 0) return (unsigned short)sign;               // 下溢 → 0
    return (unsigned short)(sign | ((unsigned)e << 10) | m);
}
static inline float rSrgb2Lin(float s) {
    return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f);
}
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
static int g_pixFmt = 0;           // 0=BGRA8, 1=RGBA16F（scRGB 线性，HDR 直通）
static bool g_debugMode = false;   // DEBUG 开关（F12）——显示格式状态
static HWND g_hwnd = nullptr;      // 主窗口（HDR 检测按窗口所在显示器）
void RendererToggleDebug() { g_debugMode = !g_debugMode; }
bool RendererIsDebug() { return g_debugMode; }
// 主图平移（放大后拖动查看）
static int g_panX = 0, g_panY = 0;
static float g_lastFit = 1.0f;  // 最近一次渲染的 fit 比例（滚轮从适配缩放时用）
// 缩放缓冲缓存（拖动时避免重算缩放，只重合成）
static std::vector<unsigned char> g_scaledCache;
static int g_scaledW = 0, g_scaledH = 0;
static const unsigned char *g_scaledSrc = nullptr;  // 缓存对应的 g_origPixels
static int g_scaledRot = -1;                        // 缓存对应的旋转
static float g_scaledZoom = -1.0f;                  // 缓存对应的缩放

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
static int g_frameFmt = -1;  // 纹理格式缓存（-1=未创建, 0=R8G8B8A8, 1=R16G16B16A16_FLOAT）
static ComPtr<ID3D11RasterizerState> g_rsNoCull;
void RendererResize(int w, int h);
bool RendererIsHDR() { return g_hdrMode; }

void RendererInit(HWND hwnd)
{
    g_hwnd = hwnd;
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
    float uiRatio;     // UI 区域比例（缩略图条起点 = viewH/winH），UI 保持 SDR
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
    float boost = 1.0 - 0.25 * highRatio;  // 高光比例越高，整体增益越低（更温和）
    float res = lin * peakNit * boost;
    // 高光软压缩：>0.7 的部分渐近压缩（避免顶到峰值纯白）
    float t = saturate((lin - 0.7) / 0.3);       // 0.7~1.0 过渡
    float compressed = 0.7 * peakNit * boost + (lin - 0.7) * peakNit * boost * 0.7;
    res = lerp(res, compressed, t * highRatio);
    return res;
}
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 c = tex.Sample(smp, uv);
    if (hdrSource > 0.5) {
        // UI 区域（缩略图条/按钮，uv.y > uiRatio）：sRGB 值增强到峰值（与其他 SDR UI 一致亮度）
        if (uiRatio > 0.0f && uv.y > uiRatio) {
            float3 lin = float3(srgb2lin(c.r), srgb2lin(c.g), srgb2lin(c.b));
            return float4(lin * (peakNit / 80.0), 1.0);
        }
        // 16F 主图：scRGB 物理直通（1.0=80nit，高光>1=真实 HDR 亮度）
        // 亮度固定硬件参数，不做自适应 tone map（工程决策：显示器各不相同，不能逐图调）
        return float4(c.rgb, 1.0);
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
    g_frameTex.Reset(); g_frameSRV.Reset(); g_frameFmt = -1;   // 尺寸变了，纹理必须重建（否则 UpdateSubresource 尺寸不匹配）
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
// outFmt: 0=BGRA8, 1=RGBA16F（scRGB 线性，HDR 直通）
bool RendererDecodeToBuffer(const std::wstring &path, int frame, unsigned char **outPix, int *outW, int *outH, int *outRot, bool *outHdr, float *outHighRatio, int *outFmt)
{
    if (!outPix || !outW || !outH || !outRot || !outHdr || !outHighRatio || !outFmt) return false;
    *outPix = nullptr;
    *outHdr = false;
    *outHighRatio = 0.0f;
    *outFmt = 0;

    // Query：EXIF 旋转 + 失败分类 + 是否 16F（后台线程安全）
    DecodeInfo info;
    int exifRot = 0;
    bool is16f = false;
    if (QueryImageInfo(path, info)) {
        if (info.error == QLERR_NOT_SUPPORTED) return false;
        exifRot = info.exifRot;
        is16f = (info.format == QLPF_RGBA16F || info.format == QLPF_RGBA32F);
        *outHdr = is16f;
    }

    if (is16f) {
        // 16F 直通：DecodeImageAny 拿 16F（scRGB 线性），后台线程预转 float 缓冲（w*h*4 float）
        // 渲染时直接 float 插值（避免每像素 half 转换——拖动流畅）
        ImageBuffer ib;
        if (!DecodeImageAny(path, frame, 0, 0, ib)) return false;
        if (ib.width < 1 || ib.height < 1 || ib.format != QLPF_RGBA16F) {
            if (ib.freeFn) ib.freeFn(ib.pixels);
            return false;
        }
        float *fp = new float[(size_t)ib.width * ib.height * 4];
        const unsigned short *sp = (const unsigned short*)ib.pixels;
        for (int y = 0; y < ib.height; ++y) {
            const unsigned short *row = (const unsigned short*)(ib.pixels + (size_t)y * ib.stride);
            float *drow = fp + (size_t)y * ib.width * 4;
            for (int x = 0; x < ib.width; ++x) {
                const unsigned short *s = row + x * 4;
                float *d = drow + x * 4;
                d[0] = rHalf2Float(s[0]); d[1] = rHalf2Float(s[1]);
                d[2] = rHalf2Float(s[2]); d[3] = rHalf2Float(s[3]);
            }
        }
        if (ib.freeFn) ib.freeFn(ib.pixels);
        *outPix = (unsigned char*)fp;
        *outW = ib.width; *outH = ib.height; *outRot = exifRot;
        *outFmt = 1;  // 1 = RGBA32F 缓冲（scRGB 线性）
        return true;
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
// fmt: 0=BGRA8, 1=RGBA16F（scRGB 线性，HDR 直通）
void RendererCommitImage(unsigned char *pix, int w, int h, int exifRot, bool isHdr, float highRatio, int fmt)
{
    if (!pix || w < 1 || h < 1) return;
    delete[] g_origPixels;
    g_origPixels = pix;  // 所有权转移
    g_imgW = w; g_imgH = h;
    g_exifRot = exifRot;
    g_isHdrImage = isHdr;
    g_highRatio = highRatio;
    g_pixFmt = fmt;
    g_rotation = 0;  // 新图重置手动旋转（Q/E 只对当前图）
    g_panX = 0; g_panY = 0;  // 新图重置平移
    g_scaledCache.clear();   // 新图：清缩放缓存（key 已失效）
    // 默认：图小于窗口→100%不放大，图大于窗口→适配缩小
    g_zoom = (g_imgW <= g_w && g_imgH <= g_h) ? 1.0f : 0.0f;
}

// ── 主图平移（放大后拖动查看）──
void RendererPan(int dx, int dy)
{
    g_panX += dx;
    g_panY += dy;
}
void RendererResetPan()
{
    g_panX = 0; g_panY = 0;
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
    // 从"窗口适配"开始缩放时重置平移（图回居中）；已有缩放保持 pan
    if (g_zoom == 0.0f) { g_panX = 0; g_panY = 0; }
    g_scaledCache.clear();  // 缩放变化：清缓存（下次渲染同步重算）
    if (dz == 0.0f) { g_zoom = 0.0f; return; }        // S=全窗口适配
    else if (dz == 1.0f) { g_zoom = 1.0f; return; }   // F=100% 原尺寸
    // 滚轮：基于当前显示比例平滑缩放。g_zoom=0(适配) 时用 g_lastFit 换算
    float cur = (g_zoom == 0.0f) ? g_lastFit : g_zoom;
    g_zoom = cur * dz;
    if (g_zoom < 0.05f) g_zoom = 0.05f;
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
                            int cornerR = 8, int cornerRY = 8,
                            bool is16f = false)
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
            unsigned char *dst = frame + ((size_t)fy * winW + fx) * (is16f ? 8 : 4);
            if (is16f) {
                unsigned short *dh = (unsigned short*)dst;
                // sRGB BGRA(src) → RGBA 混合（shader UI 统一增强）
                dh[0] = rFloat2Half((src[2]*0.6f + rHalf2Float(dh[0])*255.0f*0.4f)/255.0f);
                dh[1] = rFloat2Half((src[1]*0.6f + rHalf2Float(dh[1])*255.0f*0.4f)/255.0f);
                dh[2] = rFloat2Half((src[0]*0.6f + rHalf2Float(dh[2])*255.0f*0.4f)/255.0f);
                dh[3] = rFloat2Half(1.0f);
            } else {
                dst[0] = (unsigned char)(src[0]*0.6 + dst[0]*0.4);
                dst[1] = (unsigned char)(src[1]*0.6 + dst[1]*0.4);
                dst[2] = (unsigned char)(src[2]*0.6 + dst[2]*0.4);
                dst[3]=255;
            }
        }

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// 绘制按钮到 frame（缩略图条上方居中）
static void DrawButtons(unsigned char *frame, int winW, int winH, bool is16f = false)
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
                    unsigned char *d = frame + ((size_t)yy * winW + xx) * (is16f ? 8 : 4);
                    if (is16f) {
                        unsigned short *dh = (unsigned short*)d;
                        dh[0] = rFloat2Half(((c+6)*0.7f + rHalf2Float(dh[0])*255.0f*0.3f)/255.0f);
                        dh[1] = rFloat2Half((c*0.7f + rHalf2Float(dh[1])*255.0f*0.3f)/255.0f);
                        dh[2] = rFloat2Half((c*0.7f + rHalf2Float(dh[2])*255.0f*0.3f)/255.0f);
                        dh[3] = rFloat2Half(1.0f);
                    } else {
                        d[0] = (unsigned char)((c)*0.7 + d[0]*0.3);
                        d[1] = (unsigned char)((c)*0.7 + d[1]*0.3);
                        d[2] = (unsigned char)((c+6)*0.7 + d[2]*0.3);
                        d[3] = 255;
                    }
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
                unsigned char *d = frame + ((size_t)y * winW + x) * (is16f ? 8 : 4);
                if (is16f) {
                    unsigned short *dh = (unsigned short*)d;
                    dh[0] = rFloat2Half((xc*0.6f + rHalf2Float(dh[0])*255.0f*0.4f)/255.0f);
                    dh[1] = rFloat2Half((xc*0.6f + rHalf2Float(dh[1])*255.0f*0.4f)/255.0f);
                    dh[2] = rFloat2Half((xc*0.6f + rHalf2Float(dh[2])*255.0f*0.4f)/255.0f);
                    dh[3] = rFloat2Half(1.0f);
                } else {
                    d[0]=(unsigned char)(xc*0.6 + d[0]*0.4);
                    d[1]=(unsigned char)(xc*0.6 + d[1]*0.4);
                    d[2]=(unsigned char)(xc*0.6 + d[2]*0.4);
                    d[3]=255;
                }
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
    RenderButtonGDI(frame, winW, winH, g_btnPrevX, g_btnPrevY, 44, 36, L"\u25C0", g_hoverBtn == 1, 14, 14, is16f);
    RenderButtonGDI(frame, winW, winH, g_btnMgrX,  g_btnMgrY,  60, 36, L"Manage", g_hoverBtn == 3, 14, 14, is16f);
    RenderButtonGDI(frame, winW, winH, g_btnNextX, g_btnNextY, 44, 36, L"\u25B6", g_hoverBtn == 2, 14, 14, is16f);
}

// 绘制 DEBUG 信息（左上角，F12 切换）——多行完整显示
static void DrawDebugInfo(unsigned char *frame, int winW, int winH, bool is16f)
{
    wchar_t buf[512];
    const wchar_t *fmt = (g_pixFmt == 1) ? L"16F" : L"8bit";
    int rotAll = (g_rotation + (g_exifRot == 6 ? 1 : g_exifRot == 8 ? 3 : g_exifRot == 3 ? 2 : 0)) % 4;
    // 当前窗口所在显示器（多显示器准确）
    wchar_t monName[64] = L"?";
    if (g_hwnd) {
        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW mi; mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            const wchar_t *p = wcsrchr(mi.szDevice, L'\\');
            wcsncpy_s(monName, p ? p + 1 : mi.szDevice, 63);
        }
    }
    swprintf_s(buf, 512, L"FMT: %s\nIMG: %dx%d\nWIN: %dx%d\nZOOM: %.2f\nROT: %d (exif %d)\nPAN: %d,%d\nMON: %s HDR:%d PEAK:%.0f",
        fmt, g_imgW, g_imgH, winW, winH, g_zoom, rotAll, g_exifRot, g_panX, g_panY,
        monName, HdrIsDisplayHDRFor(g_hwnd) ? 1 : 0, HdrDisplayPeakBrightnessFor(g_hwnd));

    // 按 \n 拆行（手动逐行 DrawText，可靠多行）
    const wchar_t *lines[8];
    int lineCount = 0;
    {
        wchar_t *p = buf;
        while (*p && lineCount < 10) {
            lines[lineCount++] = p;
            wchar_t *nl = wcschr(p, L'\n');
            if (!nl) break;
            *nl = 0;
            p = nl + 1;
        }
    }

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HFONT font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
    HGDIOBJ oldF = SelectObject(memDC, font);
    int lineH = 20, bw = 320, bh = lineH * lineCount + 12;
    int bx = 12, by = 12;  // 左上角

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { DeleteObject(bmp); SelectObject(memDC, oldF); DeleteObject(font); DeleteDC(memDC); ReleaseDC(nullptr, screenDC); return; }
    HGDIOBJ oldBmp = SelectObject(memDC, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH oldBr = (HBRUSH)SelectObject(memDC, bg);
    Rectangle(memDC, 0, 0, bw, bh);
    SelectObject(memDC, oldBr);
    DeleteObject(bg);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(0, 255, 0));
    for (int i = 0; i < lineCount; ++i) {
        RECT tr = { 12, 6 + i * lineH, bw - 4, bh };
        DrawTextW(memDC, lines[i], -1, &tr, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
    }

    // 合进 frame（不透明，清晰可读）
    for (int yy = 0; yy < bh; ++yy)
        for (int xx = 0; xx < bw; ++xx) {
            int fx = bx + xx, fy = by + yy;
            if (fx < 0 || fx >= winW || fy < 0 || fy >= winH) continue;
            unsigned char *src = (unsigned char*)bits + ((size_t)yy * bw + xx) * 4;
            unsigned char *dst = frame + ((size_t)fy * winW + fx) * (is16f ? 8 : 4);
            if (is16f) {
                unsigned short *dh = (unsigned short*)dst;
                dh[0] = rFloat2Half(src[2]/255.0f);  // R（BGRA→RGBA）
                dh[1] = rFloat2Half(src[1]/255.0f);
                dh[2] = rFloat2Half(src[0]/255.0f);
                dh[3] = rFloat2Half(1.0f);
            } else {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255;
            }
        }

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    SelectObject(memDC, oldF);
    DeleteObject(font);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// 绘制当前文件名到 frame（缩略图条上方靠右，半透明黑底白字）
static void DrawFilename(unsigned char *frame, int winW, int winH, const wchar_t *name, bool is16f = false)
{
    if (!name || !name[0]) return;
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    // 先量文字宽度
    HFONT font = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    HGDIOBJ oldF = SelectObject(memDC, font);
    SIZE sz;
    GetTextExtentPoint32W(memDC, name, (int)wcslen(name), &sz);
    int bw = sz.cx + 24, bh = 30;
    if (bw < 40) bw = 40;
    // 位置：缩略图条上方（主图区底部边缘），靠右 16px
    int y0 = winH - THUMB_H;
    int by = y0 - bh - 10;  // 缩略图条上方 10px
    int bx = winW - bw - 16;

    // 离屏 DIB（黑底）
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(memDC, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH oldBr = (HBRUSH)SelectObject(memDC, bg);
    Rectangle(memDC, 0, 0, bw, bh);
    SelectObject(memDC, oldBr);
    DeleteObject(bg);
    // 白字
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(235, 235, 235));
    SelectObject(memDC, font);
    RECT tr = { 12, 0, bw - 8, bh };
    DrawTextW(memDC, name, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 合进 frame（半透明 55%）
    for (int yy = 0; yy < bh; ++yy)
        for (int xx = 0; xx < bw; ++xx) {
            int fx = bx + xx, fy = by + yy;
            if (fx < 0 || fx >= winW || fy < 0 || fy >= winH) continue;
            unsigned char *src = (unsigned char*)bits + ((size_t)yy * bw + xx) * 4;
            unsigned char *dst = frame + ((size_t)fy * winW + fx) * (is16f ? 8 : 4);
            if (is16f) {
                unsigned short *dh = (unsigned short*)dst;
                dh[0] = rFloat2Half((src[2]*0.55f + rHalf2Float(dh[0])*255.0f*0.45f)/255.0f);
                dh[1] = rFloat2Half((src[1]*0.55f + rHalf2Float(dh[1])*255.0f*0.45f)/255.0f);
                dh[2] = rFloat2Half((src[0]*0.55f + rHalf2Float(dh[2])*255.0f*0.45f)/255.0f);
                dh[3] = rFloat2Half(1.0f);
            } else {
                dst[0] = (unsigned char)(src[0]*0.55 + dst[0]*0.45);
                dst[1] = (unsigned char)(src[1]*0.55 + dst[1]*0.45);
                dst[2] = (unsigned char)(src[2]*0.55 + dst[2]*0.45);
                dst[3] = 255;
            }
        }

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    SelectObject(memDC, oldF);
    DeleteObject(font);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

void RendererRender()
{
    if (!g_rtv) return;
    float clear[4] = { 0.07f, 0.07f, 0.08f, 1.0f };
    g_ctx->ClearRenderTargetView(g_rtv.Get(), clear);

    // 提前声明（确保 HDR/SDR 分支都可见）
    std::vector<unsigned char> frame;
    std::vector<unsigned short> frame16;  // 16F 直通路径（RGBA16F half，scRGB 线性）
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
        g_lastFit = fit;  // 存 fit 供滚轮从适配缩放时换算
        // g_zoom: 0=fit(适配), 其他=相对原图比例（0.5=50%, 1=100%, 2=200%）
        float s = (g_zoom == 0.0f) ? fit : g_zoom;
        int sw = (int)(dispW * s), sh = (int)(dispH * s);
        if (sw < 1) sw = 1; if (sh < 1) sh = 1;

        // 方案 C：按需渲染可视区——直接对窗口像素采样原图（无 scaled 缓存）
        // 循环次数 = 窗口像素（恒定），与图尺寸/缩放倍率无关
        bool use16 = (g_pixFmt == 1 && g_hdrMode);  // 16F + HDR 屏直通；非 HDR 屏回退 8bit
        if (use16) frame16.assign((size_t)g_w * g_h * 4, 0);
        else frame.assign((size_t)g_w * g_h * 4, 0);
        // 显示比例：sw/sh = 图显示尺寸（可能远超窗口，但只渲染可视区）
        float dispS = s;
        int dispWpx = (int)(dispW * dispS), dispHpx = (int)(dispH * dispS);
        if (dispWpx < 1) dispWpx = 1; if (dispHpx < 1) dispHpx = 1;
        // 图左上角在窗口坐标（居中 + pan）
        int imgX = (g_w - dispWpx) / 2 + g_panX;
        int imgY = (viewH - dispHpx) / 2 + g_panY;
        // 对窗口每个可见像素，映射回原图坐标做双线性采样
        for (int wy = 0; wy < viewH; ++wy) {
            // 目标窗口像素 (wx, wy) → 图内坐标（显示坐标）
            int iy = wy - imgY;              // 图内 y（显示坐标）
            if (iy < 0 || iy >= dispHpx) continue;
            unsigned char *rowDst = &frame[((size_t)wy * g_w) * 4];
            for (int wx = 0; wx < g_w; ++wx) {
                int ix = wx - imgX;          // 图内 x（显示坐标）
                if (ix < 0 || ix >= dispWpx) continue;
                // 图内显示坐标 → 原图坐标（含旋转映射）
                float u = (float)ix / dispWpx, v = (float)iy / dispHpx;
                float sx, sy;
                switch (rotAll) {
                    case 1: sx = (1.0f - v) * g_imgW; sy = u * g_imgH; break;
                    case 3: sx = v * g_imgW; sy = (1.0f - u) * g_imgH; break;
                    case 2: sx = (1.0f - u) * g_imgW; sy = (1.0f - v) * g_imgH; break;
                    default: sx = u * g_imgW; sy = v * g_imgH; break;
                }
                int x0 = (int)sx; if (x0 >= g_imgW) x0 = g_imgW - 1;
                int x1 = x0 + 1; if (x1 >= g_imgW) x1 = g_imgW - 1;
                int y0 = (int)sy; if (y0 >= g_imgH) y0 = g_imgH - 1;
                int y1 = y0 + 1; if (y1 >= g_imgH) y1 = g_imgH - 1;
                float wxw = sx - x0, wyw = sy - y0;
                if (g_pixFmt == 1 && g_hdrMode) {
                    // 16F 主图采样（float 缓冲，scRGB 线性——直接插值，无 half 转换）
                    const float *r0f = (const float*)(g_origPixels + (size_t)y0 * g_imgW * 16) + x0 * 4;
                    const float *r1f = (const float*)(g_origPixels + (size_t)y1 * g_imgW * 16) + x0 * 4;
                    unsigned short *dh = &frame16[((size_t)wy * g_w + wx) * 4];
                    for (int ch = 0; ch < 3; ++ch) {
                        float top = r0f[ch]*(1-wxw) + r0f[4+ch]*wxw;
                        float bot = r1f[ch]*(1-wxw) + r1f[4+ch]*wxw;
                        dh[ch] = rFloat2Half(top*(1-wyw) + bot*wyw);
                    }
                    dh[3] = rFloat2Half(1.0f);
                } else if (g_pixFmt == 1) {
                    // 16F + 非 HDR 屏：tone map 到 8bit frame（HDR→SDR 回退，防黑屏）
                    const float *r0f = (const float*)(g_origPixels + (size_t)y0 * g_imgW * 16) + x0 * 4;
                    const float *r1f = (const float*)(g_origPixels + (size_t)y1 * g_imgW * 16) + x0 * 4;
                    unsigned char *d = &rowDst[(size_t)wx * 4];
                    for (int ch = 0; ch < 3; ++ch) {
                        float top = r0f[ch]*(1-wxw) + r0f[4+ch]*wxw;
                        float bot = r1f[ch]*(1-wxw) + r1f[4+ch]*wxw;
                        float v = top*(1-wyw) + bot*wyw;   // scRGB 线性（0-5+）
                        float tm = v / (1.0f + v);          // Reinhard → 0-1
                        float s = tm <= 0.0031308f ? tm*12.92f : 1.055f*powf(tm, 1.0f/2.4f) - 0.055f;
                        if (s < 0.0f) s = 0.0f; if (s > 1.0f) s = 1.0f;
                        d[ch] = (unsigned char)(s * 255.0f);
                    }
                    d[3] = 255;
                } else {
                    const unsigned char *r0 = g_origPixels + (size_t)y0 * g_imgW * 4;
                    const unsigned char *r1 = g_origPixels + (size_t)y1 * g_imgW * 4;
                    unsigned char *d = &rowDst[(size_t)wx * 4];
                    for (int ch = 0; ch < 3; ++ch) {
                        float top = r0[x0*4+ch]*(1-wxw) + r0[x1*4+ch]*wxw;
                        float bot = r1[x0*4+ch]*(1-wxw) + r1[x1*4+ch]*wxw;
                        d[ch] = (unsigned char)(top*(1-wyw) + bot*wyw);
                    }
                    d[3] = 255;
                }
            }
        }
        g_strip.renderToBuffer(use16 ? (unsigned char*)frame16.data() : frame.data(), g_w, g_h, g_pixFmt == 1);

        DrawButtons(use16 ? (unsigned char*)frame16.data() : frame.data(), g_w, g_h, g_pixFmt == 1);
        // 文件名（缩略图条上方靠右）
        {
            extern std::wstring g_curFile;
            if (!g_curFile.empty()) {
                const wchar_t *n = g_curFile.c_str();
                const wchar_t *sl = wcsrchr(n, L'\\');
                const wchar_t *sl2 = wcsrchr(n, L'/');
                if (sl2 && (!sl || sl2 > sl)) sl = sl2;
                if (sl) n = sl + 1;
                DrawFilename(use16 ? (unsigned char*)frame16.data() : frame.data(), g_w, g_h, n, g_pixFmt == 1);
            }
        }
        if (g_debugMode)
            DrawDebugInfo(use16 ? (unsigned char*)frame16.data() : frame.data(), g_w, g_h, g_pixFmt == 1);

        sd.Width = g_w; sd.Height = g_h; sd.MipLevels = 1; sd.ArraySize = 1;
        sd.Usage = D3D11_USAGE_IMMUTABLE; sd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        sd.SampleDesc.Count = 1;

        if (g_hdrMode) {
            if (use16) {
                // 16F 直通：frame16（half RGBA scRGB 线性）→ R16G16B16A16_FLOAT 纹理
                D3D11_TEXTURE2D_DESC td16 = {};
                td16.Width = g_w; td16.Height = g_h; td16.MipLevels = 1; td16.ArraySize = 1;
                td16.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td16.SampleDesc.Count = 1;
                td16.Usage = D3D11_USAGE_DEFAULT; td16.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (!g_frameTex || g_frameFmt != 1) {
                    g_frameTex.Reset(); g_frameSRV.Reset();
                    g_dev->CreateTexture2D(&td16, nullptr, &g_frameTex);
                    g_dev->CreateShaderResourceView(g_frameTex.Get(), nullptr, &g_frameSRV);
                    g_frameFmt = 1;
                }
                if (g_frameTex)
                    g_ctx->UpdateSubresource(g_frameTex.Get(), 0, nullptr, frame16.data(), (UINT)(g_w*8), 0);
            } else {
                // 8bit HDR：frame(BGRA8) → RGBA 纹理（sRGB→linear→增强→PQ）
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
                td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (!g_frameTex || g_frameFmt != 0) {
                    g_frameTex.Reset(); g_frameSRV.Reset();
                    g_dev->CreateTexture2D(&td, nullptr, &g_frameTex);
                    g_dev->CreateShaderResourceView(g_frameTex.Get(), nullptr, &g_frameSRV);
                    g_frameFmt = 0;
                }
                // 每帧 UpdateSubresource（避免重建/释放竞态）
                if (g_frameTex)
                    g_ctx->UpdateSubresource(g_frameTex.Get(), 0, nullptr, rgba.data(), (UINT)(g_w*4), 0);
            }
            { HRESULT h1 = g_frameTex ? S_OK : E_FAIL;
              HRESULT h2 = g_frameSRV ? S_OK : E_FAIL;
              if (SUCCEEDED(h1) && SUCCEEDED(h2) && g_hdrVS && g_hdrPS) {
                // 常量缓冲：peak
                float peak = HdrDisplayPeakBrightness();
                if (peak <= 0.0f) peak = 1000.0f;
                float cbData[4] = { peak, g_isHdrImage ? 1.0f : 0.0f, g_highRatio,
                    g_h > 0 ? (float)(g_h - THUMB_H) / (float)g_h : 1.0f };  // uiRatio
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
