// 无边框窗口 —— 懒加载 D3D11 + 翻页
#include <windows.h>
#include <string>
#include <vector>
#include <shlwapi.h>
#include "thumbstrip.h"
#include <thread>
void RendererInit(HWND hwnd);
void RendererLoadImage(const std::wstring &path);
void RendererResize(int w, int h);
void RendererRender();
void RendererEnsureInit(HWND hwnd);
void RendererSetZoom(float dz);
void RendererLoadImage(const std::wstring &path);
ThumbStrip g_strip;
static void GenerateThumbs();
static std::wstring g_curFile;
static HWND g_hwnd = nullptr;
static std::vector<std::wstring> g_files;
static int g_curIdx = -1;
static const wchar_t *IMG_EXTS[] = { L".jpg",L".jpeg",L".png",L".webp",L".bmp",L".gif",L".tif",L".tiff" };
static void LoadDirFiles(const std::wstring &path)
{
    g_files.clear(); g_curIdx = -1;
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, path.c_str());
    PathRemoveFileSpecW(dir);
    std::wstring search = std::wstring(dir) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring name = fd.cFileName;
            for (auto ext : IMG_EXTS) {
                if (_wcsicmp(PathFindExtensionW(name.c_str()), ext) == 0) {
                    g_files.push_back(std::wstring(dir) + L"\\" + name);
                    break;
                }
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    // 匹配当前文件索引（统一分隔符）
    for (size_t i = 0; i < g_files.size(); ++i) {
        std::wstring a = g_files[i], b = path;
        for (auto &ch : a) if (ch == L'/') ch = L'\\';
        for (auto &ch : b) if (ch == L'/') ch = L'\\';
        if (_wcsicmp(a.c_str(), b.c_str()) == 0) { g_curIdx = (int)i; break; }
    }
}
void LoadFileByPath(HWND hwnd, const wchar_t *path)
{
    if (hwnd) g_hwnd = hwnd;
    g_curFile = path;
    LoadDirFiles(path);
    RendererEnsureInit(g_hwnd);
    RendererLoadImage(g_curFile);
    GenerateThumbs();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, TRUE);
}

// 统一翻页：加载图片 + 更新缩略图条
static void NavTo(int idx, HWND hwnd)
{
    if (idx < 0 || idx >= (int)g_files.size()) return;
    g_curIdx = idx;
    g_curFile = g_files[idx];
    RendererLoadImage(g_curFile);
    // 批量补生成可视范围缩略图（按可视数量）
    if (idx < (int)g_strip.items.size()) {
        RECT rc; if (hwnd) GetClientRect(hwnd, &rc);
        int vis = (hwnd ? rc.right : 1200) / THUMB_W + 4;
        int lo = idx - vis, hi = idx + vis;
        if (lo < 0) lo = 0;
        if (hi > (int)g_files.size()) hi = (int)g_files.size();
        for (int i = lo; i < hi; ++i)
            if (g_strip.items[i].px.empty()) g_strip.loadImage(g_files[i], i);
    }
    g_strip.curIdx = idx;
    RECT rc; if (hwnd) GetClientRect(hwnd, &rc);
    g_strip.scrollTo(idx, hwnd ? rc.right : 1200);
    if (hwnd) InvalidateRect(hwnd, nullptr, TRUE);
}

// 生成当前文件夹所有图片的缩略图（后台线程，不卡首帧）
static void GenerateThumbs()
{
    g_strip.clear();
    g_strip.items.resize(g_files.size());
    for (int i = 0; i < (int)g_files.size(); ++i) {
        g_strip.items[i].bmp = nullptr;
        g_strip.items[i].path = g_files[i];
    }
    g_strip.curIdx = g_curIdx;
    RECT rc; if (g_hwnd) GetClientRect(g_hwnd, &rc);
    g_strip.scrollTo(g_curIdx, g_hwnd ? rc.right : 1200);

    // 后台线程生成缩略图（拷贝路径列表，避免竞争 g_files）
    std::vector<std::wstring> paths = g_files;
    int cur = g_curIdx;
    int startLo = cur, startHi = cur;  // 先生成当前图
    HWND h = g_hwnd;
    RECT crc; if (h) GetClientRect(h, &crc);
    int vis = (h ? crc.right : 1200) / THUMB_W + 4;
    std::thread([paths, cur, vis, h]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        int lo = cur - vis, hi = cur + vis;
        if (lo < 0) lo = 0;
        if (hi > (int)paths.size()) hi = (int)paths.size();
        // 螺旋：当前图 → 右侧 → 左侧（让当前图先显示）
        std::vector<int> order;
        order.push_back(cur);
        for (int d = 1; d <= vis; ++d) {
            if (cur + d < hi) order.push_back(cur + d);
            if (cur - d >= lo) order.push_back(cur - d);
        }
        for (int idx : order) {
            if (idx < 0 || idx >= (int)paths.size()) continue;
            g_strip.loadImage(paths[idx], idx);
        }
        // 完成后通知主线程刷新
        if (h) PostMessageW(h, WM_USER + 1, 0, 0);
    }).detach();
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wp;
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH) > 0) {
            g_hwnd = hwnd;
            LoadFileByPath(hwnd, path);
            SetFocus(hwnd);
        }
        DragFinish(hDrop);
        return 0;
    }
    case WM_SIZE:
        RendererResize(LOWORD(lp), HIWORD(lp));
        // 窗口 resize 后重新居中缩略图条（超宽屏/全屏适配）
        if (!g_files.empty() && g_curIdx >= 0) {
            g_strip.scrollTo(g_curIdx, LOWORD(lp));
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        extern bool g_d3dReady;
        if (!g_d3dReady) {
            RECT r; GetClientRect(hwnd, &r);
            FillRect(ps.hdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        if (g_d3dReady) RendererRender();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN: {
        bool handled = true;
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        else if (wp == VK_LEFT || wp == VK_UP) {
            if (g_curIdx > 0) NavTo(g_curIdx - 1, hwnd);
        }
        else if (wp == VK_RIGHT || wp == VK_DOWN || wp == VK_SPACE) {
            if (g_curIdx < (int)g_files.size()-1) NavTo(g_curIdx + 1, hwnd);
        }
        else if (wp == 'F' || wp == 'f') { RendererSetZoom(1.0f); InvalidateRect(hwnd, nullptr, TRUE); }
        else if (wp == 'S' || wp == 's') { RendererSetZoom(0.0f); InvalidateRect(hwnd, nullptr, TRUE); }
        else handled = false;
        return handled ? 0 : DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_MOUSEWHEEL: {
        short delta = (short)HIWORD(wp);
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl) {
            RendererSetZoom(delta > 0 ? 1.25f : 0.8f);
        } else if (delta > 0) {
            if (g_curIdx > 0) NavTo(g_curIdx - 1, hwnd);
        } else {
            if (g_curIdx < (int)g_files.size()-1) NavTo(g_curIdx + 1, hwnd);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        RECT rc; GetClientRect(hwnd, &rc);
        int idx = g_strip.hitTest(x, y, rc.bottom);
        if (idx >= 0 && idx < (int)g_files.size()) NavTo(idx, hwnd);
        SetFocus(hwnd);
        return 0;
    }
    case WM_USER + 1:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
HWND g_mainHwnd = nullptr;
bool CreateMainWindow(HINSTANCE hInst)
{
    g_mainHwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"QLensQuickView", L"QLens",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        200, 200, 1200, 800,
        nullptr, nullptr, hInst, nullptr);
    if (!g_mainHwnd) return false;
    DragAcceptFiles(g_mainHwnd, TRUE);
    SetFocus(g_mainHwnd);
    return true;
}