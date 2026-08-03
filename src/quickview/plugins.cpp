// 插件实现：注册表 + DLL 加载 + 统一解码入口
#include "plugins.h"
#include <shlwapi.h>
#include <cstring>
#include <cstdio>

namespace QLensPlugins {

namespace {
std::vector<DecodePlugin> g_decoders;
std::vector<HMODULE> g_modules;

struct RegApi {
    void (*regDecoder)(const DecodePlugin *);
};
static void regDecoderPtr(const DecodePlugin *p) { if (p) RegisterDecoder(*p); }
typedef void (*PluginEntryFn)(RegApi *);
}  // namespace

void RegisterDecoder(const DecodePlugin &p) { g_decoders.push_back(p); }

// 扩展名匹配：支持 "svg|svgz" 多扩展名
static bool extMatch(const DecodePlugin &d, const std::wstring &extLower)
{
    // char* -> wchar_t*（UTF-8）
    int len = MultiByteToWideChar(CP_UTF8, 0, d.ext, -1, nullptr, 0);
    if (len <= 1) return false;
    std::wstring all(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, d.ext, -1, &all[0], len);
    // 按 | 分割比较
    size_t start = 0;
    while (start <= all.size()) {
        size_t bar = all.find(L'|', start);
        std::wstring one = all.substr(start, bar == std::wstring::npos ? std::wstring::npos : bar - start);
        if (!one.empty() && one == extLower) return true;
        if (bar == std::wstring::npos) break;
        start = bar + 1;
    }
    return false;
}

const DecodePlugin *FindDecoder(const std::wstring &extLower)
{
    for (const auto &d : g_decoders)
        if (extMatch(d, extLower)) return &d;
    return nullptr;
}

// 签名匹配：插件的 signature 是十六进制字节表字符串（如 "89504E47"）
const DecodePlugin *FindDecoderBySignature(const unsigned char *magic, int len)
{
    for (const auto &d : g_decoders) {
        if (!d.signature || !d.signature[0]) continue;
        size_t sigLen = strlen(d.signature);
        if (sigLen < 2 || (sigLen & 1)) continue;
        size_t bytes = sigLen / 2;
        if (bytes > (size_t)len) continue;
        bool match = true;
        for (size_t i = 0; i < bytes; ++i) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(d.signature[i * 2]);
            int lo = hex(d.signature[i * 2 + 1]);
            if (hi < 0 || lo < 0 || (unsigned char)((hi << 4) | lo) != magic[i]) { match = false; break; }
        }
        if (match) return &d;
    }
    return nullptr;
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
        RegApi api = { regDecoderPtr };
        entry(&api);
        g_modules.push_back(h);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return !g_modules.empty();
}

// 统一解码入口：插件优先（扩展名 + 签名），调用者负责 out.freeFn 释放
bool DecodeAny(const std::wstring &path, int frame, int targetW, int targetH, ImageBuffer *out)
{
    if (!out) return false;
    out->pixels = nullptr;

    // 1. 扩展名匹配
    size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = path.substr(dot + 1);
        for (auto &ch : ext) if (ch >= L'A' && ch <= L'Z') ch += 32;
        const DecodePlugin *dp = FindDecoder(ext);
        if (dp && dp->decode) {
            if (dp->decode(path.c_str(), frame, targetW, targetH, out)) return true;
        }
    }

    // 2. 签名匹配（插件能识别改名文件）
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (f) {
        unsigned char magic[16] = {};
        int got = (int)fread(magic, 1, 16, f);
        fclose(f);
        const DecodePlugin *dp = FindDecoderBySignature(magic, got);
        if (dp && dp->decode) {
            if (dp->decode(path.c_str(), frame, targetW, targetH, out)) return true;
        }
    }
    return false;
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
