#include <windows.h>
#include <stdio.h>

#define IDI_QLENS 101  // qlens.rc 图标资源

extern "C" bool QLensPlugins_LoadFromExeDir();

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
bool CreateMainWindow(HINSTANCE);
extern HWND g_mainHwnd;
void LoadFileByPath(HWND, const wchar_t*);
void RegisterFileAssociations();

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep)
{
    FILE *f = fopen("E:/PYTHON/qlens/build-qv/crash.log", "a");
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
    SetUnhandledExceptionFilter(CrashFilter);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc = {};
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

    // 命令行传图则打开
    if (pCmdLine && *pCmdLine) LoadFileByPath(g_mainHwnd, pCmdLine);

    MSG m; while (GetMessageW(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
