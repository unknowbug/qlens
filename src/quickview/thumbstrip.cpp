// 缩略图条实现
#include "thumbstrip.h"
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
    std::lock_guard<std::mutex> lock(mtx);
    // 缩略解码：WIC 直接解到 THUMB_IMG 尺寸（快，不解全图）
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
    int i = (x + scrollX) / THUMB_W;
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
void ThumbStrip::renderToBuffer(unsigned char *dst, int winW, int winH)
{
    std::lock_guard<std::mutex> lock(mtx);
    int y0 = winH - THUMB_H;
    // 背景
    for (int y = y0; y < winH; ++y)
        for (int x = 0; x < winW; ++x) {
            unsigned char *p = dst + ((size_t)y * winW + x) * 4;
            p[0]=22; p[1]=22; p[2]=20; p[3]=255;
        }
    // 缩略图
    for (int i = 0; i < (int)items.size(); ++i) {
        int x = pad + i * THUMB_W - scrollX;
        // 只画完整缩略图：边缘被窗口切掉的半张不显示（保持干净）
        if (x < 0 || x + THUMB_W > winW) continue;
        if (items[i].px.empty()) continue;
        const std::vector<unsigned char> &px = items[i].px;
        // 画到 dst（缩放居中到 THUMB_W x THUMB_H 单元格）
        int cellW = items[i].w, cellH = items[i].h;
        int ix = x + (THUMB_W - cellW) / 2;
        int iy = y0 + (THUMB_H - cellH) / 2;
        bool isCur = (i == curIdx);
        for (int yy = 0; yy < cellH && iy + yy < winH; ++yy)
            for (int xx = 0; xx < cellW && ix + xx < winW; ++xx) {
                const unsigned char *s = &px[((size_t)yy * cellW + xx) * 4];
                unsigned char *d = dst + ((size_t)(iy+yy) * winW + (ix+xx)) * 4;
                if (isCur) {
                    // 当前图光亮：提亮 + 白底衬托
                    int v0 = (s[0]*2+255)/3, v1 = (s[1]*2+255)/3, v2 = (s[2]*2+255)/3;
                    d[0]=(unsigned char)v0; d[1]=(unsigned char)v1; d[2]=(unsigned char)v2; d[3]=255;
                } else {
                    // 非当前图：轻微压暗
                    d[0]=s[0]*3/4; d[1]=s[1]*3/4; d[2]=s[2]*3/4; d[3]=255;
                }
            }
        // Picasa 风格：当前图光亮（整体提亮），其他图正常
    }
}
