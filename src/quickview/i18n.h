// 轻量 i18n（GNU gettext .po 风格）——UI 字符串可翻译
#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

namespace I18n {
// 加载 .po 文件（UTF-8）：msgid(中文) → msgstr(当前语言)。失败时用内置中文。
bool Load(const wchar_t *poPath);
// 从配置（qlens_config.ini language=）加载 language/<lang>/<app>.po；无配置 → 系统语言/中文
bool LoadFromConfig();
// 指定程序名加载（Manager 用：qlens_manager.po）
bool LoadForApp(const wchar_t *appName);
// 查表：返回当前语言翻译；无匹配时返回中文（msgid 本身）
const wchar_t *Get(const wchar_t *zh);
// 卸载/清空（释放）
void Clear();
}
