// 插件实现：注册表 + DLL 加载
#include "plugins.h"
#include <shlwapi.h>
#include <cstring>
#include <cstdio>

namespace QLensPlugins {

namespace {
std::vector<DecodePlugin> g_decoders;
std::vector<RenderPlugin> g_renders;
std::vector<HMODULE> g_modules;

struct RegApi {
    void (*regDecoder)(const DecodePlugin *);
    void (*regRender)(const RenderPlugin *);
};
// 注册回调适配（引用→指针转换）
static void regDecoderPtr(const DecodePlugin *p) { if (p) RegisterDecoder(*p); }
static void regRenderPtr(const RenderPlugin *p) { if (p) RegisterRender(*p); }
typedef void (*PluginEntryFn)(RegApi *);
}  // namespace

void RegisterDecoder(const DecodePlugin &p) { g_decoders.push_back(p); }
const DecodePlugin *FindDecoder(const std::wstring &extLower)
{
    for (const auto &d : g_decoders) {
        // char* -> wchar_t* 转换比较
        int len = MultiByteToWideChar(CP_UTF8, 0, d.ext, -1, nullptr, 0);
        std::wstring wext(len > 0 ? len - 1 : 0, L'\0');
        if (len > 1) MultiByteToWideChar(CP_UTF8, 0, d.ext, -1, &wext[0], len);
        if (extLower == wext) return &d;
    }
    return nullptr;
}

void RegisterRender(const RenderPlugin &p) { g_renders.push_back(p); }
const RenderPlugin *FindRender(const char *name)
{
    for (const auto &r : g_renders)
        if (strcmp(r.name, name) == 0) return &r;
    return nullptr;
}

bool ApplyRender(const char *name, const ImageBuffer *src, ImageBuffer *dst)
{
    const RenderPlugin *rp = FindRender(name);
    if (!rp || !rp->process) return false;
    return rp->process(src, dst);
}

bool LoadPluginsFromDir(const std::wstring &dir)
{
    std::wstring search = dir + L"\\*.dll";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        HMODULE h = LoadLibraryW(full.c_str());
        if (!h) continue;
        PluginEntryFn entry = (PluginEntryFn)GetProcAddress(h, "qlens_plugin_entry");
        if (!entry) { FreeLibrary(h); continue; }
        RegApi api = { regDecoderPtr, regRenderPtr };
        entry(&api);
        g_modules.push_back(h);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return !g_modules.empty();
}

}  // namespace QLensPlugins

// 全局入口：从 exe 同目录 plugins/ 加载
extern "C" __declspec(dllexport)
bool QLensPlugins_LoadFromExeDir()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, exe);
    PathRemoveFileSpecW(dir);
    std::wstring pdir = std::wstring(dir) + L"\\plugins";
    return QLensPlugins::LoadPluginsFromDir(pdir);
}
