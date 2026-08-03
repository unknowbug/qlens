// i18n.cpp —— .po 解析（GNU gettext 风格，UTF-8）
#include "i18n.h"
#include <stdio.h>
#include <vector>

namespace I18n {

static std::unordered_map<std::wstring, std::wstring> g_map;
static std::wstring g_lang;  // 当前语言（空=默认中文）

// UTF-8 → wstring
static std::wstring Utf8ToW(const char *s)
{
    if (!s || !*s) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n - 1);
    return w;
}

// 提取引号内的内容（"..." 或 "..." 跨行续接）
static bool ParseQuoted(const char *line, std::wstring &out)
{
    const char *q = strchr(line, '"');
    if (!q) return false;
    const char *end = strrchr(q + 1, '"');
    if (!end) return false;
    std::string inner(q + 1, end - q - 1);
    // 处理转义 \" 和 \n
    std::string unescaped;
    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            if (inner[i+1] == '"') { unescaped += '"'; ++i; }
            else if (inner[i+1] == 'n') { unescaped += '\n'; ++i; }
            else if (inner[i+1] == '\\') { unescaped += '\\'; ++i; }
            else { unescaped += inner[i]; }
        } else unescaped += inner[i];
    }
    out = Utf8ToW(unescaped.c_str());
    return true;
}

bool Load(const wchar_t *poPath)
{
    g_map.clear();    // 读文件（二进制，UTF-8）
    FILE *f = nullptr;
    if (_wfopen_s(&f, poPath, L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) { fclose(f); return false; }
    std::vector<char> buf(sz + 1, 0);
    fread(buf.data(), 1, sz, f);
    fclose(f);

    // 逐行解析 msgid/msgstr
    std::wstring curId;
    bool inId = false, inStr = false;
    char *line = buf.data();
    char *p = line;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        // 跳过 BOM
        if (p == line && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
        if (strncmp(p, "msgid ", 6) == 0) {
            std::wstring id;
            if (ParseQuoted(p + 6, id)) { curId = id; inId = true; inStr = false; }
        } else if (strncmp(p, "msgstr ", 7) == 0) {
            std::wstring s;
            if (inId && ParseQuoted(p + 7, s)) {
                g_map[curId] = s;
                inId = false; inStr = true;
            }
        } else if (p[0] == '"' && (inId || inStr)) {
            // 续接行（多行字符串）——简化：追加到当前
            std::wstring part;
            if (ParseQuoted(p, part)) {
                if (inId) curId += part;
                else if (inStr) { /* msgstr 续接——当前实现只支持单行，跳过 */ }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    // 提取语言（Header 的 Language: 字段——简化：文件名后缀）
    return true;
}

const wchar_t *Get(const wchar_t *zh)
{
    if (!zh) return L"";
    auto it = g_map.find(zh);
    if (it != g_map.end() && !it->second.empty()) return it->second.c_str();
    return zh;  // 默认中文
}

void Clear() { g_map.clear(); }

// 从配置/系统语言加载对应 .po（language/<code>.po）
// 优先级：qlens_config.ini 的 language（Manager Settings 写）→ 系统 UI 语言
// 代码：zh=中文(默认) en=English；系统语言无匹配 → 中文
bool LoadFromConfig()
{
    g_map.clear();
    wchar_t exeDir[MAX_PATH];
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    wchar_t *sl = wcsrchr(exeDir, L'\\');
    if (sl) *sl = 0;

    // 1) 配置优先（Manager Settings 写）
    wchar_t lang[64] = {};
    {
        wchar_t iniPath[MAX_PATH];
        swprintf_s(iniPath, MAX_PATH, L"%s\\qlens_config.ini", exeDir);
        GetPrivateProfileStringW(L"General", L"language", L"", lang, 64, iniPath);
    }
    // 2) 无配置 → 系统 UI 语言
    if (!lang[0]) {
        LANGID lid = GetUserDefaultUILanguage();
        // 中文（简体/繁体/zh）→ zh；英文 → en；其他 → 默认中文
        switch (PRIMARYLANGID(lid)) {
            case LANG_CHINESE: wcscpy_s(lang, L"zh"); break;
            case LANG_ENGLISH: wcscpy_s(lang, L"en"); break;
            default: wcscpy_s(lang, L"zh"); break;
        }
    }
    // 归一化：zh/Chinese → zh；en/English → en
    if (_wcsicmp(lang, L"Chinese") == 0) wcscpy_s(lang, L"zh");
    else if (_wcsicmp(lang, L"English") == 0) wcscpy_s(lang, L"en");

    // zh → 默认中文（无需 .po）
    if (_wcsicmp(lang, L"zh") == 0) return true;

    // 加载 language/<code>/qlens_quickview.po
    wchar_t poPath[MAX_PATH];
    swprintf_s(poPath, MAX_PATH, L"%s\\language\\%s\\qlens_quickview.po", exeDir, lang);
    return Load(poPath);
}

}
