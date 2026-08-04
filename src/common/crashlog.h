#pragma once
// 共享崩溃日志模块（纯 Win32——QuickView/Manager 通用）
// 安装 SEH 过滤器：崩溃时写 时间/版本/系统信息/异常码+模块偏移/调用栈（带模块偏移）
// 日志路径：%APPDATA%\<appName>\crash.log（exe 目录可能只读——分发兼容）

#ifdef __cplusplus
extern "C" {
#endif

// 安装崩溃捕获。appName = 日志文件夹名（如 L"QLens Manager"），version = 版本字符串
void CrashLog_Init(const wchar_t *appName, const wchar_t *version);

// 检查 crash.log 是否有崩溃记录（供启动时提示用户发送给开发者）
// 返回 1 = 有崩溃记录；0 = 无
int CrashLog_HasRecentCrash(void);

// 当前崩溃日志完整路径（写入 g_logPath；供 UI 提示用户）
void CrashLog_Path(wchar_t *buf, int bufLen);

#ifdef __cplusplus
}
#endif
