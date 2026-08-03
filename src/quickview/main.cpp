#include <windows.h>
#include <stdio.h>
#include "i18n.h"

#define IDI_QLENS 101  // qlens.rc 图标资源

extern "C" bool QLensPlugins_LoadFromExeDir();

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
bool CreateMainWindow(HINSTANCE);
extern HWND g_mainHwnd;
void LoadFileByPath(HWND, const wchar_t*);
void SetStartMonitorIndex(int);
void RegisterFileAssociations();

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep)
{
    // 崩溃日志写到 %APPDATA%/QLens/（exe 目录可能只读——分发兼容）
    wchar_t logPath[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", logPath, MAX_PATH) && logPath[0]) {
        wcscat_s(logPath, MAX_PATH, L"\\QLens");
        CreateDirectoryW(logPath, nullptr);
        wcscat_s(logPath, MAX_PATH, L"\\crash.log");
    } else {
        GetModuleFileNameW(nullptr, logPath, MAX_PATH);
        wchar_t *sl = wcsrchr(logPath, L'\\');
        if (sl) wcscpy_s(sl + 1, MAX_PATH - (sl - logPath), L"crash.log");
    }
    FILE *f = nullptr;
    _wfopen_s(&f, logPath, L"a");
    if (f) {
        fprintf(f, "CRASH code=0x%08lX at=%p\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
        fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR pCmdLine, int)
{
    // 高 DPI 感知（Win10 1703+ Per-Monitor V2）——分发到高分屏不模糊/不错位
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL(WINAPI *SetDpiCtxFn)(DPI_AWARENESS_CONTEXT);
        auto fn = (SetDpiCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    SetUnhandledExceptionFilter(CrashFilter);
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

    if (imgPath[0]) LoadFileByPath(g_mainHwnd, imgPath);

    MSG m; while (GetMessageW(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
