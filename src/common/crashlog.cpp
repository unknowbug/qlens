#include "crashlog.h"
#include <windows.h>
#include <stdio.h>

static wchar_t g_logPath[MAX_PATH];
static wchar_t g_appName[64];
static wchar_t g_version[32];

// Windows 版本（RtlGetVersion——GetVersionEx 在 Win8.1+ 被行为阉割）
static void WriteOsVersion(FILE *f)
{
    typedef LONG(WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn) {
            RTL_OSVERSIONINFOW ovi;
            ovi.dwOSVersionInfoSize = sizeof(ovi);
            if (fn(&ovi) == 0)
                fprintf(f, "OS: Windows %lu.%lu.%lu (build %lu)\n",
                        ovi.dwMajorVersion, ovi.dwMinorVersion, ovi.dwBuildNumber, ovi.dwBuildNumber);
        }
    }
}

// 地址 → "模块名+偏移"（可定位到具体 DLL 内函数）
static void WriteFrame(FILE *f, void *addr)
{
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)addr, &mod) && mod) {
        wchar_t modName[MAX_PATH];
        if (GetModuleFileNameW(mod, modName, MAX_PATH)) {
            const wchar_t *base = wcsrchr(modName, L'\\');
            fprintf(f, "  0x%p  %ls+0x%llX\n", addr,
                    base ? base + 1 : modName,
                    (unsigned long long)((char *)addr - (char *)mod));
            return;
        }
    }
    fprintf(f, "  0x%p\n", addr);
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep)
{
    FILE *f = nullptr;
    _wfopen_s(&f, g_logPath, L"a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "\n===== %ls v%ls CRASH =====\n", g_appName, g_version);
        fprintf(f, "Time: %04u-%02u-%02u %02u:%02u:%02u\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        WriteOsVersion(f);
        fprintf(f, "Exception: code=0x%08lX\n", (unsigned long)ep->ExceptionRecord->ExceptionCode);
        WriteFrame(f, ep->ExceptionRecord->ExceptionAddress);
        void *stack[32];
        USHORT frames = RtlCaptureStackBackTrace(0, 32, stack, nullptr);
        fprintf(f, "Call stack (%u frames):\n", (unsigned)frames);
        for (USHORT i = 0; i < frames; ++i)
            WriteFrame(f, stack[i]);
        fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;   // 继续默认处理（崩溃弹窗）
}

void CrashLog_Init(const wchar_t *appName, const wchar_t *version)
{
    wcsncpy_s(g_appName, appName, 63);
    wcsncpy_s(g_version, version, 31);
    wchar_t logPath[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", logPath, MAX_PATH) && logPath[0]) {
        wcscat_s(logPath, MAX_PATH, L"\\");
        wcscat_s(logPath, MAX_PATH, appName);
        CreateDirectoryW(logPath, nullptr);
        wcscat_s(logPath, MAX_PATH, L"\\crash.log");
    } else {
        GetModuleFileNameW(nullptr, logPath, MAX_PATH);
        wchar_t *sl = wcsrchr(logPath, L'\\');
        if (sl) wcscpy_s(sl + 1, MAX_PATH - (sl - logPath), L"crash.log");
    }
    wcsncpy_s(g_logPath, logPath, MAX_PATH - 1);
    SetUnhandledExceptionFilter(CrashFilter);
}

int CrashLog_HasRecentCrash(void)
{
    if (!g_logPath[0]) return 0;
    FILE *f = nullptr;
    _wfopen_s(&f, g_logPath, L"r");
    if (!f) return 0;
    int found = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "CRASH")) { found = 1; break; }
    }
    fclose(f);
    return found;
}

void CrashLog_Path(wchar_t *buf, int bufLen)
{
    if (buf && bufLen > 0)
        wcsncpy_s(buf, bufLen, g_logPath, _TRUNCATE);
}
