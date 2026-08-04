#include <windows.h>
#include <stdio.h>
#include "i18n.h"
#include "../common/crashlog.h"

#define IDI_QLENS 101  // qlens.rc 图标资源

extern "C" bool QLensPlugins_LoadFromExeDir();

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
bool CreateMainWindow(HINSTANCE);
extern HWND g_mainHwnd;
void LoadFileByPath(HWND, const wchar_t*);
void SetStartMonitorIndex(int);
void RegisterFileAssociations();

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR pCmdLine, int)
{
    // 高 DPI 感知（Win10 1703+ Per-Monitor V2）——分发到高分屏不模糊/不错位
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL(WINAPI *SetDpiCtxFn)(DPI_AWARENESS_CONTEXT);
        auto fn = (SetDpiCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    CrashLog_Init(L"QLens", L"0.2.2");   // 崩溃捕获：版本/系统/调用栈+模块偏移 → %APPDATA%\QLens\crash.log
    if (CrashLog_HasRecentCrash()) {
        wchar_t path[MAX_PATH];
        CrashLog_Path(path, MAX_PATH);
        wchar_t msg[1024];
        swprintf_s(msg, 1024,
            L"上次运行 QLens 时发生崩溃。\n\n崩溃日志（含版本/系统/调用栈）：\n%ls\n\n请将此文件发送给开发者，便于定位修复。",
            path);
        MessageBoxW(nullptr, msg, L"QLens 崩溃提示", MB_ICONWARNING | MB_OK);
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc = {};
    wc.style = CS_DBLCLKS;  // 支持双击（WM_LBUTTONDBLCLK）
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(IDI_QLENS));
    wc.lpszClassName = L"QLensQuickView";
    if (!RegisterClassW(&wc)) return 1;
    if (!CreateMainWindow(hi)) return 1;
    // 窗口/任务栏图标（含小图标）
    HICON icon = LoadIconW(hi, MAKEINTRESOURCEW(IDI_QLENS));
    if (icon) { SendMessageW(g_mainHwnd, WM_SETICON, ICON_BIG, (LPARAM)icon); SendMessageW(g_mainHwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon); }

    // 加载插件（exe 旁 plugins/ 目录）
    QLensPlugins_LoadFromExeDir();

    // 注册系统默认看图器（每次启动刷新，HKCU 无需管理员）
    RegisterFileAssociations();

    // 命令行参数：图片路径 + --monitor N（指定启动显示器）
    int monitorIdx = -1;
    wchar_t imgPath[MAX_PATH] = {};
    if (pCmdLine && *pCmdLine) {
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(pCmdLine, &argc);
        if (argv) {
            for (int i = 0; i < argc; ++i) {
                if (wcscmp(argv[i], L"--monitor") == 0 && i + 1 < argc)
                    monitorIdx = _wtoi(argv[i + 1]);
                else if (!imgPath[0] && argv[i][0] != L'-')
                    wcsncpy_s(imgPath, argv[i], MAX_PATH - 1);
            }
            LocalFree(argv);
        }
    }
    if (monitorIdx >= 0) SetStartMonitorIndex(monitorIdx);
    // 加载语言（qlens_config.ini 的 language → language/<Lang>.po；默认中文）
    I18n::LoadFromConfig();
    // 首次启动：无 config 时现场写一份默认配置（默认 language=系统解析结果）
    I18n::EnsureDefaultConfig();

    if (imgPath[0]) LoadFileByPath(g_mainHwnd, imgPath);

    MSG m; while (GetMessageW(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
