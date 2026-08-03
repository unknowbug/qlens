// 无边框窗口 —— 懒加载 D3D11 + 翻页
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <string>
#include <vector>
#include <shlwapi.h>
#include <shellapi.h>
#include <commdlg.h>
#include "thumbstrip.h"
#include "decoder.h"
#include <thread>
#include <atomic>

// DROPFILES（若头文件未定义）
#ifndef DROPFILES
typedef struct _DROPFILES {
    DWORD pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
} DROPFILES, *LPDROPFILES;
#endif
void RendererInit(HWND hwnd);
void RendererLoadImage(const std::wstring &path);
void RendererResize(int w, int h);
void RendererRender();
void RendererEnsureInit(HWND hwnd);
void RendererSetZoom(float dz);
void RendererShowButtons(bool show);
bool RendererButtonsVisible();
int RendererHitTestButton(int x, int y);
void RendererUpdateHover(int x, int y);
// 异步解码接口
bool RendererDecodeToBuffer(const std::wstring &path, int frame, unsigned char **outPix, int *outW, int *outH, int *outRot, bool *outHdr, float *outHighRatio);
void RendererCommitImage(unsigned char *pix, int w, int h, int exifRot, bool isHdr, float highRatio);
int RendererNextRequestId();
void RendererPan(int dx, int dy);
void RendererResetPan();
void RendererRotate(int steps);
static void CopyCurrentImage();
static void SaveAsDialog(HWND hwnd);
static void DeleteCurrentToRecycle();
void RendererLoadImage(const std::wstring &path);
ThumbStrip g_strip;
static void GenerateThumbs();
static std::wstring g_curFile;
static HWND g_hwnd = nullptr;
static std::vector<std::wstring> g_files;
static int g_curIdx = -1;
static const wchar_t *IMG_EXTS[] = { L".jpg",L".jpeg",L".png",L".webp",L".bmp",L".gif",L".tif",L".tiff",L".svg" };
// 异步解码：最新请求 ID（取消令牌）
static std::atomic<int> g_pendingReqId{0};
static const UINT WM_ASYNC_DECODED = WM_APP + 1;
// 最近一次解码错误（0=无；显示在画面中央）
static int g_lastError = 0;
// GIF 动画检测（前向声明）
static void StartAnimationIfGif(const std::wstring &path);
// 主图拖动平移状态
static bool g_dragging = false;
static int g_dragLastX = 0, g_dragLastY = 0;
// 异步解码结果（跨线程传递）
struct DecodedResult {
    unsigned char *pix;
    int w, h, rot;
    bool isHdr;  // 源是高位深（真 HDR 图）
    int error;   // QLensError（0=成功）
    float highRatio;  // 高光像素比例（亮图降高光）
};

// ── GIF 动画状态 ──
static int g_animFrames = 0;       // 总帧数（>1 = 动画）
static int g_animCur = 0;          // 当前帧
static int g_animDelays[256];      // 每帧 delay(ms)
static bool g_animOn = false;      // 动画播放中
static bool g_animPending = false; // 有帧解码请求在途

// 异步加载主图：后台线程解码，完成 PostMessage 通知 UI
static void RequestLoadAsync(const std::wstring &path, int frame = 0)
{
    int reqId = RendererNextRequestId();
    g_pendingReqId.store(reqId);
    std::thread([path, frame, reqId]() {
        // 后台线程必须初始化 COM（WIC 的 CoCreateInstance 依赖）
        HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        unsigned char *pix = nullptr;
        int w = 0, h = 0, rot = 0;
        bool isHdr = false;
        float highRatio = 0.0f;
        int err = 0;
        // 先 Query 拿错误分类（失败时 error 有值）
        DecodeInfo qi;
        if (QueryImageInfo(path, qi) && qi.error != QLERR_OK) err = qi.error;
        bool ok = RendererDecodeToBuffer(path, frame, &pix, &w, &h, &rot, &isHdr, &highRatio);
        // 完成时通知 UI（带 reqId；UI 侧校验是否已过期）
        HWND hw = g_hwnd;
        if (hw) PostMessageW(hw, WM_ASYNC_DECODED, (WPARAM)reqId,
            ok ? (LPARAM)new DecodedResult{ pix, w, h, rot, isHdr, 0, highRatio }
               : (LPARAM)new DecodedResult{ nullptr, 0, 0, false, false, err ? err : QLERR_CORRUPT, 0 });
        else if (pix) delete[] pix;
        if (SUCCEEDED(co)) CoUninitialize();
    }).detach();
}
// 检测是否为多帧动画（GIF 等），是则启动播放
static void StartAnimationIfGif(const std::wstring &path)
{
    g_animOn = false;
    g_animPending = false;
    g_animFrames = 0;
    g_animCur = 0;
    DecodeInfo qi;
    if (QueryImageInfo(path, qi) && qi.frames > 1) {
        g_animFrames = qi.frames;
        for (int i = 0; i < qi.frameDelayCount && i < 256; ++i)
            g_animDelays[i] = qi.frameDelays[i];
        if (g_animDelays[0] <= 0) g_animDelays[0] = 100;
        g_animOn = true;
        g_animPending = true;  // 首帧已由 RequestLoadAsync 解码，完成后安排下一帧
    }
}
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
    StartAnimationIfGif(g_curFile);
    RequestLoadAsync(g_curFile);
    GenerateThumbs();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, TRUE);
}

// ── 注册系统默认看图器（HKCU，无需管理员）──
void RegisterFileAssociations()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    // 1. ProgID：QLensQuickView
    std::wstring progKey = L"Software\\Classes\\QLensQuickView";
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, progKey.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        const wchar_t *desc = L"QLens Image Viewer";
        RegSetValueExW(hk, L"", 0, REG_SZ, (const BYTE*)desc, (DWORD)(wcslen(desc)*sizeof(wchar_t)));
        RegCloseKey(hk);
    }
    // DefaultIcon
    std::wstring iconKey = progKey + L"\\DefaultIcon";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, iconKey.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        std::wstring ico = std::wstring(L"\"") + exe + L"\",0";
        RegSetValueExW(hk, L"", 0, REG_SZ, (const BYTE*)ico.c_str(), (DWORD)(ico.size()*sizeof(wchar_t)));
        RegCloseKey(hk);
    }
    // open command
    std::wstring cmdKey = progKey + L"\\shell\\open\\command";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, cmdKey.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        std::wstring cmd = std::wstring(L"\"") + exe + L"\" \"%1\"";
        RegSetValueExW(hk, L"", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.size()*sizeof(wchar_t)));
        RegCloseKey(hk);
    }

    // 2. 扩展名关联
    const wchar_t *exts[] = { L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".svg" };
    for (auto ext : exts) {
        std::wstring openKey = std::wstring(L"Software\\Classes\\") + ext + L"\\OpenWithProgids";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, openKey.c_str(), 0, nullptr, 0,
            KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
            const wchar_t *pid = L"QLensQuickView";
            RegSetValueExW(hk, pid, 0, REG_SZ, (const BYTE*)L"", 1);
            RegCloseKey(hk);
        }
    }
}

// 右键菜单项 ID
enum { IDM_LEFT = 1, IDM_RIGHT, IDM_ZOOMIN, IDM_ZOOMOUT, IDM_COPY, IDM_SAVEAS, IDM_DELETE };

// 复制当前图到剪贴板（多格式：文件 CF_HDROP + 路径 CF_UNICODETEXT + 图片 CF_DIB）
static void CopyCurrentImage()
{
    if (g_curFile.empty()) return;
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    // 1. CF_UNICODETEXT：文件路径
    {
        size_t len = g_curFile.size();
        HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
        if (hText) {
            wchar_t *dst = (wchar_t*)GlobalLock(hText);
            memcpy(dst, g_curFile.c_str(), len * sizeof(wchar_t));
            dst[len] = 0;
            GlobalUnlock(hText);
            SetClipboardData(CF_UNICODETEXT, hText);
        }
    }

    // 2. CF_HDROP：文件（支持拖放/文件管理器粘贴）
    {
        // 结构：DROPFILES + 路径 + 双 null
        size_t pathBytes = (g_curFile.size() + 1) * sizeof(wchar_t);
        size_t total = sizeof(DROPFILES) + pathBytes + sizeof(wchar_t);
        HGLOBAL hDrop = GlobalAlloc(GMEM_MOVEABLE, total);
        if (hDrop) {
            DROPFILES *df = (DROPFILES*)GlobalLock(hDrop);
            df->pFiles = sizeof(DROPFILES);
            df->fWide = TRUE;
            wchar_t *p = (wchar_t*)((char*)df + sizeof(DROPFILES));
            memcpy(p, g_curFile.c_str(), pathBytes);
            p[g_curFile.size()] = 0;  // 路径后已有一个 null，再加一个结束
            GlobalUnlock(hDrop);
            SetClipboardData(CF_HDROP, hDrop);
        }
    }

    // 3. CF_DIB：图片位图（大图压缩到最长边 1600，仿 Picasa 所见即所得）
    {
        DecodedImage img;
        if (DecodeImageFile(g_curFile, img) && img.width > 0 && img.height > 0) {
            // 若最长边 > 1600，降采样（Picasa 风格：粘贴 QQ 不爆大小）
            int w = img.width, h = img.height;
            const int MAX_SIDE = 1600;
            std::vector<unsigned char> buf;  // compressed BGRA pixels
            if (w > MAX_SIDE || h > MAX_SIDE) {
                float scale = (float)MAX_SIDE / (w > h ? w : h);
                int sw = (int)(w * scale), sh = (int)(h * scale);
                if (sw < 1) sw = 1; if (sh < 1) sh = 1;
                buf.resize((size_t)sw * sh * 4);
                int stride = img.stride ? img.stride : w * 4;
                // 双线性缩小
                for (int y = 0; y < sh; ++y) {
                    float fy = (y + 0.5f) / scale - 0.5f;
                    int y0 = (int)fy; if (y0 < 0) y0 = 0; if (y0 >= h) y0 = h - 1;
                    int y1 = y0 + 1; if (y1 >= h) y1 = h - 1;
                    float wy = fy - y0;
                    const unsigned char *r0 = img.pixels + (size_t)y0 * stride;
                    const unsigned char *r1 = img.pixels + (size_t)y1 * stride;
                    for (int x = 0; x < sw; ++x) {
                        float fx = (x + 0.5f) / scale - 0.5f;
                        int x0 = (int)fx; if (x0 < 0) x0 = 0; if (x0 >= w) x0 = w - 1;
                        int x1 = x0 + 1; if (x1 >= w) x1 = w - 1;
                        float wx = fx - x0;
                        unsigned char *d = &buf[((size_t)y * sw + x) * 4];
                        for (int ch = 0; ch < 4; ++ch) {
                            float a = r0[x0*4+ch]*(1-wx) + r0[x1*4+ch]*wx;
                            float b = r1[x0*4+ch]*(1-wx) + r1[x1*4+ch]*wx;
                            d[ch] = (unsigned char)(a*(1-wy) + b*wy);
                        }
                    }
                }
                w = sw; h = sh;
            }
            const unsigned char *srcPix = buf.empty() ? img.pixels : buf.data();
            int srcStride = buf.empty() ? (img.stride ? img.stride : img.width * 4) : w * 4;

            size_t headerSz = sizeof(BITMAPINFOHEADER);
            size_t pixSz = (size_t)w * h * 4;
            HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, headerSz + pixSz);
            if (hDib) {
                BITMAPINFOHEADER *bi = (BITMAPINFOHEADER*)GlobalLock(hDib);
                bi->biSize = (DWORD)headerSz;
                bi->biWidth = w;
                bi->biHeight = -h;  // top-down
                bi->biPlanes = 1;
                bi->biBitCount = 32;
                bi->biCompression = BI_RGB;
                unsigned char *pix = (unsigned char*)bi + headerSz;
                for (int y = 0; y < h; ++y)
                    memcpy(pix + (size_t)y * w * 4, srcPix + (size_t)y * srcStride, (size_t)w * 4);
                GlobalUnlock(hDib);
                SetClipboardData(CF_DIB, hDib);
            }
        }
    }

    CloseClipboard();
}

// 删除当前图到回收站
static void DeleteCurrentToRecycle()
{
    if (g_curFile.empty()) return;
    SHFILEOPSTRUCTW fo = {};
    fo.wFunc = FO_DELETE;
    // 路径需要双 null 结尾
    std::wstring path = g_curFile;
    path.push_back(0);
    fo.pFrom = path.c_str();
    fo.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;  // 回收站 + 不确认
    SHFileOperationW(&fo);
    // 删除后刷新文件列表 + 重建缩略图导航条
    if (!g_files.empty()) {
        g_files.erase(g_files.begin() + g_curIdx);
        g_strip.clear();
        g_strip.items.resize(g_files.size());
        if (g_curIdx >= (int)g_files.size()) g_curIdx = (int)g_files.size() - 1;
        if (g_curIdx >= 0) {
            g_curFile = g_files[g_curIdx];
            RequestLoadAsync(g_curFile);
        }
        GenerateThumbs();  // 重新生成缩略图（后台线程）
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, TRUE);
    }
}

// 另存为对话框 + 保存
static void SaveAsDialog(HWND hwnd)
{
    if (g_curFile.empty()) return;
    wchar_t file[MAX_PATH] = {};
    wcscpy_s(file, g_curFile.c_str());
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        // 复制原文件到目标
        CopyFileW(g_curFile.c_str(), ofn.lpstrFile, FALSE);
    }
}

// 统一翻页：加载图片 + 更新缩略图条（异步解码，不卡 UI）
static void NavTo(int idx, HWND hwnd)
{
    if (idx < 0 || idx >= (int)g_files.size()) return;
    g_curIdx = idx;
    g_curFile = g_files[idx];
    StartAnimationIfGif(g_curFile);
    RequestLoadAsync(g_curFile);
    // 缩略图异步生成：仅更新高亮 + 滚动（不同步解码，避免卡 UI）
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
    case WM_ASYNC_DECODED: {
        // 主图异步解码完成：校验是否最新请求（取消过期解码）
        int reqId = (int)wp;
        if (reqId != g_pendingReqId.load()) {
            // 过期请求：释放结果
            DecodedResult *r = (DecodedResult*)lp;
            if (r) { if (r->pix) delete[] r->pix; delete r; }
            return 0;
        }
        DecodedResult *r = (DecodedResult*)lp;
        if (r && r->pix) {
            g_lastError = 0;
            RendererCommitImage(r->pix, r->w, r->h, r->rot, r->isHdr, r->highRatio);
            InvalidateRect(hwnd, nullptr, TRUE);
            // 动画：若当前帧解码完成且动画在播，安排下一帧
            if (g_animOn && g_animFrames > 1) {
                g_animPending = false;
                int delay = (g_animCur < g_animFrames && g_animDelays[g_animCur] > 0)
                          ? g_animDelays[g_animCur] : 100;
                SetTimer(hwnd, 2, delay, nullptr);  // 定时切下一帧
            }
        } else if (r) {
            // 解码失败：显示错误提示
            g_lastError = r->error;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        if (r) delete r;  // pix 所有权已转移给 renderer（失败时 pix 为空）
        return 0;
    }
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
        // 解码失败提示（画面中央）
        if (g_lastError != 0) {
            const wchar_t *msg = L"无法加载图片";
            if (g_lastError == QLERR_NOT_SUPPORTED) msg = L"不支持的图片格式";
            else if (g_lastError == QLERR_IO) msg = L"无法读取文件";
            RECT rc; GetClientRect(hwnd, &rc);
            SetBkMode(ps.hdc, TRANSPARENT);
            SetTextColor(ps.hdc, RGB(200, 200, 200));
            HFONT f = CreateFontW(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
            HFONT oldF = (HFONT)SelectObject(ps.hdc, f);
            RECT tr = rc;
            DrawTextW(ps.hdc, msg, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(ps.hdc, oldF);
            DeleteObject(f);
        }
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
        else if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) { CopyCurrentImage(); }
        else if (wp == 'Q' || wp == 'q') { RendererRotate(3); InvalidateRect(hwnd, nullptr, TRUE); }  // 左旋
        else if (wp == 'E' || wp == 'e') { RendererRotate(1); InvalidateRect(hwnd, nullptr, TRUE); }  // 右旋
        else if (wp == VK_DELETE) { DeleteCurrentToRecycle(); }
        else if (wp == 'S' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) { SaveAsDialog(hwnd); }
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
    case WM_CONTEXTMENU: {
        // 弹出右键菜单（含快捷键标注）
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_LEFT,    L"左旋转\tQ");
        AppendMenuW(menu, MF_STRING, IDM_RIGHT,   L"右旋转\tE");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_ZOOMIN,  L"放大\tCtrl +");
        AppendMenuW(menu, MF_STRING, IDM_ZOOMOUT, L"缩小\tCtrl -");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_COPY,    L"复制\tCtrl+C");
        AppendMenuW(menu, MF_STRING, IDM_SAVEAS,  L"另存为...\tCtrl+Shift+S");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_DELETE,  L"删除\tDel");
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        switch (cmd) {
            case IDM_LEFT:    RendererRotate(3); InvalidateRect(hwnd, nullptr, TRUE); break;
            case IDM_RIGHT:   RendererRotate(1); InvalidateRect(hwnd, nullptr, TRUE); break;
            case IDM_ZOOMIN:  RendererSetZoom(1.25f); InvalidateRect(hwnd, nullptr, TRUE); break;
            case IDM_ZOOMOUT: RendererSetZoom(0.8f); InvalidateRect(hwnd, nullptr, TRUE); break;
            case IDM_COPY:    CopyCurrentImage(); break;
            case IDM_SAVEAS:  SaveAsDialog(hwnd); break;
            case IDM_DELETE:  DeleteCurrentToRecycle(); break;
        }
        return 0;
    }
    case WM_NCHITTEST: {
        // 无边框窗口：允许拖动（返回 HTCAPTION），但关闭按钮区域返回 HTCLIENT
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(hwnd, &pt);
        int btn = RendererHitTestButton(pt.x, pt.y);
        if (btn == 4) return HTCLIENT;  // 关闭按钮可点击
        // 顶部 40px 可拖动
        RECT rc; GetClientRect(hwnd, &rc);
        if (pt.y < 40) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        // 拖动平移（放大图时）
        if (g_dragging) {
            int dx = pt.x - g_dragLastX;
            int dy = pt.y - g_dragLastY;
            RendererPan(dx, dy);
            g_dragLastX = pt.x; g_dragLastY = pt.y;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        RendererUpdateHover(pt.x, pt.y);
        if (!RendererButtonsVisible()) {
            RendererShowButtons(true);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        SetTimer(hwnd, 1, 2000, nullptr);  // 2 秒后隐藏
        InvalidateRect(hwnd, nullptr, TRUE);  // 刷新 hover 状态
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            KillTimer(hwnd, 1);
            if (RendererButtonsVisible()) {
                RendererShowButtons(false);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        } else if (wp == 2) {
            // GIF 动画：切下一帧
            KillTimer(hwnd, 2);
            if (g_animOn && g_animFrames > 1 && !g_animPending) {
                g_animCur = (g_animCur + 1) % g_animFrames;
                g_animPending = true;
                RequestLoadAsync(g_curFile, g_animCur);
            }
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        RECT rc; GetClientRect(hwnd, &rc);
        // 按钮点击检测
        int btn = RendererHitTestButton(x, y);
        if (btn == 4) { DestroyWindow(hwnd); return 0; }  // 关闭
        if (btn == 1) { if (g_curIdx > 0) NavTo(g_curIdx - 1, hwnd); return 0; }
        if (btn == 2) { if (g_curIdx < (int)g_files.size()-1) NavTo(g_curIdx + 1, hwnd); return 0; }
        if (btn == 3) {
            // 启动 Manager
            wchar_t exe[MAX_PATH];
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            wchar_t dir[MAX_PATH];
            wcscpy_s(dir, exe);
            PathRemoveFileSpecW(dir);
            ShellExecuteW(nullptr, L"open", (std::wstring(dir) + L"\\qlens_manager.exe").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        int idx = g_strip.hitTest(x, y, rc.bottom);
        if (idx >= 0 && idx < (int)g_files.size()) NavTo(idx, hwnd);
        else if (y < rc.bottom - THUMB_H) {
            // 主图区域：开始拖动平移
            g_dragging = true;
            g_dragLastX = x; g_dragLastY = y;
            SetCapture(hwnd);
        }
        SetFocus(hwnd);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_dragging) {
            g_dragging = false;
            ReleaseCapture();
        }
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
        WS_POPUP | WS_VISIBLE | WS_MAXIMIZEBOX,  // 无边框 + 最大化
        0, 0, 1200, 800,
        nullptr, nullptr, hInst, nullptr);
    if (!g_mainHwnd) return false;
    DragAcceptFiles(g_mainHwnd, TRUE);
    // 默认最大化到工作区（不含任务栏）
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_mainHwnd, nullptr, wa.left, wa.top,
        wa.right - wa.left, wa.bottom - wa.top, SWP_NOZORDER);
    SetFocus(g_mainHwnd);
    return true;
}