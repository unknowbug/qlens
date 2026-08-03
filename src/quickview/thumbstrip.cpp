// 缩略图条实现
#include "thumbstrip.h"
#include "thumbstrip.h"

// half 转换（scRGB 线性 <-> 16bit 半精度）
static inline float thHalf2Float(unsigned short h) {
    unsigned sign = (h >> 15) & 1, exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
    if (exp == 0) return (mant == 0) ? 0.0f : (float)mant * (sign ? -1 : 1) * 5.9604645e-8f;
    if (exp == 31) return (mant == 0) ? 1e30f : 1e30f;
    float v = ldexpf((float)(mant | 0x400), (int)exp - 25);
    return v * (sign ? -1 : 1);
}
static inline unsigned short thFloat2Half(float f) {
    if (f <= 0.0f) return 0;
    if (f >= 65504.0f) return 0x7BFF;
    unsigned sign = f < 0 ? 0x8000u : 0u;
    if (f < 0) f = -f;
    int e; float m = frexpf(f, &e);  // f = m*2^e, m in [0.5,1)
    unsigned me = (unsigned)(e + 14);  // half 指数偏置 15，frexp e+1 → 偏置
    if (me >= 31) return (unsigned short)(sign | 0x7BFF);
    if (me == 0) return (unsigned short)sign;
    unsigned mm = (unsigned)(m * 2048.0f + 0.5f);  // 10bit 尾数（m in [0.5,1) → 0x400-0x7FF）
    if (mm >= 0x800) { mm >>= 1; me++; }
    return (unsigned short)(sign | (me << 10) | (mm & 0x3FF));
}
static inline float thSrgb2Lin(float s) {
    return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f);
}

#include "decoder.h"
#include <vector>

ThumbStrip::~ThumbStrip() { clear(); }

void ThumbStrip::clear()
{
    for (auto &it : items) if (it.bmp) DeleteObject(it.bmp);
    items.clear();
    scrollX = 0; curIdx = -1;
}

// 解码图片 → 生成缩略图 HBITMAP
bool ThumbStrip::loadImage(const std::wstring &path, int idx)
{
    // 解码在锁外（WIC 慢，避免阻塞 UI 渲染的 renderToBuffer）
    DecodedImage img;
    if (!DecodeImageThumb(path, img, THUMB_IMG_W, THUMB_IMG_H)) {
        // 回退全尺寸解码
        if (!DecodeImageFile(path, img)) return false;
    }
    if (img.width < 1 || img.height < 1) return false;
    int w = img.width, h = img.height;
    if (w < 1) w = 1; if (h < 1) h = 1;

    // 转成紧凑 BGRA（去掉 stride 对齐空隙）
    std::vector<unsigned char> sbuf((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        const unsigned char *row = img.pixels + (size_t)y * img.stride;
        for (int x = 0; x < w; ++x) {
            unsigned char *d = &sbuf[((size_t)y * w + x) * 4];
            const unsigned char *p = row + (size_t)x * 4;
            d[0]=p[2]; d[1]=p[1]; d[2]=p[0]; d[3]=255;  // BGRA->RGB
        }
    }

    // 只存像素（BGRA），不创建 HBITMAP（避免 DIB 越界写导致 heap corruption）
    std::vector<unsigned char> px((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char *s = &sbuf[((size_t)y * w + x) * 4];
            unsigned char *d = &px[((size_t)y * w + x) * 4];
            d[0]=s[2]; d[1]=s[1]; d[2]=s[0]; d[3]=255;  // RGB->BGRA
        }

    // 短锁：只写 items
    std::lock_guard<std::mutex> lock(mtx);
    if (idx < (int)items.size()) {
        items[idx].bmp = nullptr;
        items[idx].w = w; items[idx].h = h;
        items[idx].px = std::move(px);
    }
    return true;
}

void ThumbStrip::draw(HDC dc, int winW, int winH)
{
    int y0 = winH - THUMB_H;
    RECT bg = { 0, y0, winW, winH };
    HBRUSH br = CreateSolidBrush(RGB(20,20,22));
    FillRect(dc, &bg, br);
    DeleteObject(br);

    HDC mem = CreateCompatibleDC(dc);
    for (int i = 0; i < (int)items.size(); ++i) {
        int x = pad + i * THUMB_W - scrollX;
        if (x + THUMB_W < 0 || x > winW) continue;
        if (!items[i].bmp) continue;
        int ix = x + (THUMB_W - items[i].w) / 2;
        int iy = y0 + (THUMB_H - items[i].h) / 2;
        HGDIOBJ old = SelectObject(mem, items[i].bmp);
        StretchBlt(dc, ix, iy, items[i].w, items[i].h, mem, 0, 0, items[i].w, items[i].h, SRCCOPY);
        SelectObject(mem, old);
        RECT fr = { x, y0, x+THUMB_W, y0+THUMB_H };
        HBRUSH b = CreateSolidBrush(i == curIdx ? RGB(0,120,215) : RGB(60,60,64));
        FrameRect(dc, &fr, b);
        DeleteObject(b);
    }
    DeleteDC(mem);
}

int ThumbStrip::hitTest(int x, int y, int winH)
{
    if (y < winH - THUMB_H || y > winH) return -1;
    // 与 draw 一致：位置 = pad + i*THUMB_W - scrollX → i = (x + scrollX - pad) / THUMB_W
    int i = (x + scrollX - pad) / THUMB_W;
    if (i >= 0 && i < (int)items.size()) return i;
    return -1;
}

void ThumbStrip::scrollTo(int idx, int winW)
{
    if (winW <= 0 || items.empty()) return;
    pad = winW / 2 - THUMB_W / 2;      // 左右 padding，让首尾图也能居中
    if (pad < 0) pad = 0;
    int contentW = (int)items.size() * THUMB_W + 2 * pad;
    int center = pad + idx * THUMB_W + THUMB_W / 2;   // 当前图中心（含 pad）
    int target = center - winW / 2;
    if (target < 0) target = 0;
    int maxX = contentW - winW;
    if (maxX < 0) maxX = 0;
    if (target > maxX) target = maxX;
    scrollX = target;
}

// 像素级绘制缩略图条到 dst（BGRA 缓冲）
void ThumbStrip::renderToBuffer(unsigned char *dst, int winW, int winH, bool is16f)
{
    std::lock_guard<std::mutex> lock(mtx);
    int y0 = winH - THUMB_H;
    // 背景
    for (int y = y0; y < winH; ++y)
        for (int x = 0; x < winW; ++x) {
            unsigned char *p = dst + ((size_t)y * winW + x) * (is16f ? 8 : 4);
            if (is16f) {
                unsigned short *ph = (unsigned short*)p;
                // sRGB BGRA→RGBA 存（shader UI 区域统一 srgb2lin+增强）
                ph[0]=thFloat2Half(20.0f/255.0f); ph[1]=thFloat2Half(22.0f/255.0f); ph[2]=thFloat2Half(22.0f/255.0f); ph[3]=thFloat2Half(1.0f);
            } else {
                p[0]=22; p[1]=22; p[2]=20; p[3]=255;
            }
        }
    // 缩略图
    for (int i = 0; i < (int)items.size(); ++i) {
        int x = pad + i * THUMB_W - scrollX;
        // 只画完整缩略图：边缘被窗口切掉的半张不显示（保持干净）
        if (x < 0 || x + THUMB_W > winW) continue;
        if (items[i].px.empty()) continue;
        const std::vector<unsigned char> &px = items[i].px;
        // 缩放缩略图到格子内（防全尺寸缩略图铺开覆盖主图/越界）
        int sw = items[i].w, sh = items[i].h;
        if (sw < 1) sw = 1; if (sh < 1) sh = 1;
        float scale = (float)THUMB_W / sw;
        float sh2 = (float)(THUMB_H - 6) / sh;
        if (sh2 < scale) scale = sh2;   // 取较小缩放（保持比例）
        if (scale > 1.0f) scale = 1.0f; // 不放大（小图保持原样）
        int dw = (int)(sw * scale); if (dw < 1) dw = 1;
        int dh = (int)(sh * scale); if (dh < 1) dh = 1;
        int ix = x + (THUMB_W - dw) / 2;
        int iy = y0 + (THUMB_H - dh) / 2;
        bool isCur = (i == curIdx);
        for (int yy = 0; yy < dh; ++yy) {
            int sy = (int)(yy / scale); if (sy >= sh) sy = sh - 1;
            for (int xx = 0; xx < dw; ++xx) {
                int sx = (int)(xx / scale); if (sx >= sw) sx = sw - 1;
                const unsigned char *s = &px[((size_t)sy * sw + sx) * 4];
                unsigned char *d = dst + ((size_t)(iy+yy) * winW + (ix+xx)) * (is16f ? 8 : 4);
                // 钳制绘制范围（防任何越界写）
                if ((size_t)(iy+yy) < (size_t)winH && (size_t)(ix+xx) < (size_t)winW) {
                if (is16f) {
                    unsigned short *dh = (unsigned short*)d;
                    // sRGB BGRA(s[0]=B,s[1]=G,s[2]=R) → RGBA 存
                    if (isCur) {
                        dh[0]=thFloat2Half((s[2]*2+255.0f)/3/255.0f); dh[1]=thFloat2Half((s[1]*2+255.0f)/3/255.0f); dh[2]=thFloat2Half((s[0]*2+255.0f)/3/255.0f); dh[3]=thFloat2Half(1.0f);
                    } else {
                        dh[0]=thFloat2Half(s[2]*0.75f/255.0f); dh[1]=thFloat2Half(s[1]*0.75f/255.0f); dh[2]=thFloat2Half(s[0]*0.75f/255.0f); dh[3]=thFloat2Half(1.0f);
                    }
                } else {
                    if (isCur) {
                        int v0 = (s[0]*2+255)/3, v1 = (s[1]*2+255)/3, v2 = (s[2]*2+255)/3;
                        d[0]=(unsigned char)v0; d[1]=(unsigned char)v1; d[2]=(unsigned char)v2; d[3]=255;
                    } else {
                        d[0]=s[0]*3/4; d[1]=s[1]*3/4; d[2]=s[2]*3/4; d[3]=255;
                    }
                }
                }
            }
        }
        // Picasa 风格：当前图光亮（整体提亮），其他图正常
    }
}
