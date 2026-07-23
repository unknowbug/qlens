#include <QApplication>
#include "ManagerWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("QLens Manager");
    ManagerWindow w;
    w.showMaximized();
    return app.exec();
}
