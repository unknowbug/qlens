#include <QApplication>
#include <QCommandLineParser>
#include <QTranslator>
#include <QLocale>
#include <QDir>
#include "MainWindow.h"
#include "thumbnail.h"
#include "color.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("QLens");
    app.setApplicationVersion("0.1.0");

    // ── 国际化 ──
    QTranslator trans;
    QString tsPath = QCoreApplication::applicationDirPath() + "/translations/qlens_zh_CN";
    QLocale sysLocale = QLocale::system();
    if (sysLocale.language() == QLocale::Chinese) {
        trans.load(tsPath);
        app.installTranslator(&trans);
    }

    // 初始化共享引擎
    ThumbnailCache::init();
    ColorManager::init(true);

    // 命令行解析
    QCommandLineParser parser;
    parser.setApplicationDescription("QLens — Image Viewer");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "Image file to open");

    // HDR 选项
    QCommandLineOption hdrOpt("hdr", "Force HDR mode");
    parser.addOption(hdrOpt);

    parser.process(app);

    MainWindow w;
    w.setWindowTitle("QLens");

    // 最大化启动（全屏对第一次使用不友好）
    w.showMaximized();

    // 如果命令行有文件，直接打开
    if (!parser.positionalArguments().isEmpty())
        w.openFile(parser.positionalArguments().first());

    return app.exec();
}
