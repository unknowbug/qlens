#include <QApplication>
#include <QIcon>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include "ManagerWindow.h"

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
    QMutexLocker lock(&g_logMutex);
    QString line = QStringLiteral("[%1] [%4] %2  %3")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
        .arg(msg)
        .arg(ctx.file ? QString::fromUtf8(ctx.file) : QStringLiteral("?"))
        .arg(QString::fromUtf8(ctx.category ? ctx.category : ""));
    if (g_logFile.isOpen()) {
        QTextStream ts(&g_logFile);
        ts << line << "\n";
        ts.flush();
    }
    if (type == QtFatalMsg) {
        // 断言/致命错误：打印调用栈辅助定位
#ifdef Q_OS_WIN
        void *stack[32];
        USHORT frames = CaptureStackBackTrace(0, 32, stack, nullptr);
        if (g_logFile.isOpen()) {
            QTextStream ts(&g_logFile);
            ts << "--- call stack (" << frames << " frames) ---\n";
            for (USHORT i = 0; i < frames; ++i)
                ts << "  " << stack[i] << "\n";
            ts.flush();
        }
#endif
        if (g_logFile.isOpen()) g_logFile.flush();
    }
}

int main(int argc, char *argv[]) {
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

    ManagerWindow w;
    w.showMaximized();
    int rc = app.exec();
    qInstallMessageHandler(nullptr);
    return rc;
}
