#include <QApplication>
#include <QIcon>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include "ManagerWindow.h"
#include "decode_api.h"
#include "i18n.h"

// 插件加载入口（qlens_decode 提供，C 链接）
extern "C" bool QLensPlugins_LoadFromExeDir();

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

// ── 崩溃诊断日志：记录所有 qWarning/qFatal（含 Qt 断言），崩溃前后可查最后活动 ──
static QFile g_logFile;
static QMutex g_logMutex;

static void logHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    // 用 WinAPI 直接写文件，避免 QString/QTextStream 在内存损坏时二次崩溃
#ifdef Q_OS_WIN
    QByteArray utf8 = msg.toUtf8();
    utf8.append('\n');
    HANDLE h = CreateFileW((LPCWSTR)QString(g_logFile.fileName()).utf16(),
                           FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(h, utf8.constData(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(h);
    }
    if (type == QtFatalMsg) {
        void *stack[32];
        USHORT frames = CaptureStackBackTrace(0, 32, stack, nullptr);
        QByteArray hdr = QByteArray("--- call stack (") + QByteArray::number(frames) + QByteArray(" frames) ---\n");
        QByteArray dump;
        for (USHORT i = 0; i < frames; ++i)
            dump += QByteArray("  0x") + QByteArray::number((quintptr)stack[i], 16) + '\n';
        HANDLE h2 = CreateFileW((LPCWSTR)QString(g_logFile.fileName()).utf16(),
                                FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h2 != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h2, hdr.constData(), (DWORD)hdr.size(), &w, nullptr);
            WriteFile(h2, dump.constData(), (DWORD)dump.size(), &w, nullptr);
            CloseHandle(h2);
        }
    }
#endif
}

int main(int argc, char *argv[]) {
#ifdef Q_OS_WIN
    // 抓 SEH 硬崩溃（访问违规等不走 qFatal），打印调用栈到日志
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *ep) -> LONG {
        QMutexLocker lock(&g_logMutex);
        if (g_logFile.isOpen()) {
            QTextStream ts(&g_logFile);
            ts << "--- SEH exception code=0x"
               << QString::number((quint32)ep->ExceptionRecord->ExceptionCode, 16)
               << " ---\n";
            void *stack[32];
            USHORT frames = CaptureStackBackTrace(0, 32, stack, nullptr);
            ts << "--- call stack (" << frames << " frames) ---\n";
            for (USHORT i = 0; i < frames; ++i)
                ts << "  " << stack[i] << "\n";
            ts.flush();
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });
#endif

    QApplication app(argc, argv);
    app.setApplicationName("QLens Manager");
    app.setWindowIcon(QIcon(":/app.ico"));  // 窗口/任务栏图标

    // 打开崩溃日志（%APPDATA%/QLens Manager/qlens_crash.log）
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logPath);
    g_logFile.setFileName(logPath + "/qlens_crash.log");
    g_logFile.open(QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(logHandler);
    qInfo() << "=== Manager started ===";

    // 加载解码插件（exe 旁 plugins/，SVG 等格式）
    QLensPlugins_LoadFromExeDir();
    // 加载语言（qlens_config.ini language → language/<lang>/qlens_manager.po；默认中文）
    I18n::LoadForApp(L"qlens_manager");

    ManagerWindow w;
    // 布局自愈：上次正常关闭（trusted=1）才恢复布局；恢复前先降级（防恢复本身崩溃死循环）
    {
        std::wstring ini = I18n::ConfigIniPath(false);
        wchar_t trusted[8] = {};
        GetPrivateProfileStringW(L"Layout", L"trusted", L"", trusted, 8, ini.c_str());
        if (trusted[0] == L'1') {
            std::wstring iniW = I18n::ConfigIniPath(true);
            WritePrivateProfileStringW(L"Layout", L"trusted", L"0", iniW.c_str());
            w.restoreLayout();
        } else {
            w.showMaximized();   // 首次启动 / 上次异常退出 → 默认最大化
        }
    }
    // 命令行参数：目录 → 启动后打开（语言切换自动重启保留路径）
    if (argc > 1) {
        const QString p = QString::fromLocal8Bit(argv[1]);
        if (QFileInfo(p).isDir())
            w.openFolder(QDir::cleanPath(p));
    }
    int rc = app.exec();
    qInstallMessageHandler(nullptr);
    return rc;
}
