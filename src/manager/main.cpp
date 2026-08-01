#include <QApplication>
#include <QIcon>
#include "ManagerWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("QLens Manager");
    app.setWindowIcon(QIcon(":/app.ico"));  // 窗口/任务栏图标
    ManagerWindow w;
    w.showMaximized();
    return app.exec();
}
