// 缩略图条 —— GDI 绘制，底部 80px
#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <mutex>

#define THUMB_H 64      // 条高度（Picasa 风格，紧凑）
#define THUMB_W 72      // 每个缩略图宽度
#define THUMB_IMG_W 64  // 缩略图内图片宽度
#define THUMB_IMG_H 48  // 缩略图内图片高度

struct ThumbStrip {
    struct Item { HBITMAP bmp; int w, h; std::wstring path; std::vector<unsigned char> px; };
    std::vector<Item> items;
    int scrollX = 0;
    int curIdx = -1;
    int pad = 0;  // 左右 padding（边界居中用）
    std::mutex mtx;  // 保护 items（后台线程生成 vs 主线程渲染）

    ~ThumbStrip();
    void clear();
    bool loadImage(const std::wstring &path, int idx);
    void draw(HDC dc, int winW, int winH);
    int hitTest(int x, int y, int winH);
    void scrollTo(int idx, int winW);
    // 把缩略图条像素画进 dst（BGRA，winW*winH*4），底部 THUMB_H 区域
    void renderToBuffer(unsigned char *dst, int winW, int winH);
};
