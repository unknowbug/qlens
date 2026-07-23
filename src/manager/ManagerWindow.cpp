#include "ManagerWindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QStatusBar>
#include <QApplication>

ManagerWindow::ManagerWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("QLens Manager");
    resize(1400, 900);
    setDockNestingEnabled(true);

    // ── 中央：缩略图网格 ──
    m_grid = new ThumbnailGrid(this);
    setCentralWidget(m_grid);

    // ── 左侧 Dock：文件夹树 + 标签搜索 ──
    m_folderPanel = new FolderPanel(this);
    auto *leftDock = new QDockWidget(tr("Browse"), this);
    leftDock->setObjectName("browse");
    leftDock->setWidget(m_folderPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    // ── 右侧 Dock：标签面板 ──
    auto *tagPanel = new TagPanel(this);
    auto *rightDock = new QDockWidget(tr("Tags"), this);
    rightDock->setObjectName("tags");
    rightDock->setWidget(tagPanel);
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    // ── 状态栏 ──
    statusBar()->showMessage(tr("Ready"));

    // ── 菜单 ──
    auto *fm = menuBar()->addMenu(tr("&File"));
    fm->addAction(tr("&Open Folder..."), [this]() {
        QString d = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
        if (!d.isEmpty()) m_grid->loadFolder(d);
    });
    fm->addAction(tr("&Open Image..."), [this]() {
        QString f = QFileDialog::getOpenFileName(this, tr("Open Image"));
        if (!f.isEmpty()) openInViewer(f);
    });
    fm->addSeparator();
    fm->addAction(tr("E&xit"), qApp, &QApplication::quit);

    // ── 信号路由 ──
    connect(m_folderPanel, &FolderPanel::folderSelected,
            m_grid, &ThumbnailGrid::loadFolder);

    connect(m_grid, &ThumbnailGrid::folderDoubleClicked, [this](const QString &path) {
        m_grid->loadFolder(path);
        statusBar()->showMessage(path);
    });

    connect(m_grid, &ThumbnailGrid::imageDoubleClicked, [this](const QString &path) {
        openInViewer(path);
    });
}

void ManagerWindow::openInViewer(const QString &path) {
    if (!m_viewer) {
        m_viewer = new ViewerWidget(this);
        m_viewer->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_viewer, &ViewerWidget::imageChanged, [this](const QString &p, int) {
            if (m_viewer) m_viewer->setWindowTitle(QFileInfo(p).fileName() + " — QLens");
        });
    }
    m_viewer->openFile(path);
    m_viewer->resize(1200, 800);
    m_viewer->show();
    m_viewer->raise();
}
