// 最小 Win32 窗口 —— 验证环境是否支持基本窗口
#include <windows.h>
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = L"MinWin";
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, L"MinWin", L"Min", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        300, 300, 400, 300, 0, 0, hi, 0);
    MSG m; while (GetMessageW(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
