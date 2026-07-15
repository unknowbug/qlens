#include <QApplication>
#include <QMainWindow>
#include <QDockWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QStackedWidget>
#include <QSplitter>
#include <QStatusBar>
#include "ViewerWidget.h"

// ──── 管理器主窗口 ────

class ManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ManagerWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("QLens Manager");
        resize(1400, 900);

        setDockNestingEnabled(true);

        // ── 中央：缩略图网格 ──
        m_grid = new QLabel(tr("Thumbnail Grid"), this);
        m_grid->setAlignment(Qt::AlignCenter);
        m_grid->setStyleSheet("background:#1a1a1a; color:#555; font-size:20px;");
        setCentralWidget(m_grid);

        // ── 左侧 Dock：文件夹树 + 标签搜索 ──
        auto *leftDock = new QDockWidget(tr("Browse"), this);
        leftDock->setObjectName("browse");
        auto *leftPanel = new QWidget(this);
        auto *ll = new QVBoxLayout(leftPanel);
        ll->setContentsMargins(0, 0, 0, 0);

        m_folderTree = new QTreeView(this);
        m_fsModel = new QFileSystemModel(this);
        m_fsModel->setRootPath(QDir::homePath());
        m_fsModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
        m_folderTree->setModel(m_fsModel);
        m_folderTree->setRootIndex(m_fsModel->index(QDir::homePath()));
        m_folderTree->setHeaderHidden(true);
        for (int i = 1; i < m_fsModel->columnCount(); ++i)
            m_folderTree->hideColumn(i);
        ll->addWidget(m_folderTree, 3);

        auto *tagSearchLabel = new QLabel(tr("Tag Filter"), this);
        tagSearchLabel->setStyleSheet("color:#888; font-size:11px; padding:4px 4px 0 4px;");
        ll->addWidget(tagSearchLabel);
        m_tagSearch = new QLineEdit(this);
        m_tagSearch->setPlaceholderText(tr("Search tags..."));
        m_tagSearch->setStyleSheet("background:#222; color:#ccc; border:1px solid #333; padding:4px;");
        ll->addWidget(m_tagSearch);

        m_tagSuggestions = new QListWidget(this);
        m_tagSuggestions->setMaximumHeight(120);
        m_tagSuggestions->setStyleSheet("background:#1a1a1a; color:#888; border:1px solid #333;");
        ll->addWidget(m_tagSuggestions);

        leftPanel->setMinimumWidth(200);
        leftDock->setWidget(leftPanel);
        addDockWidget(Qt::LeftDockWidgetArea, leftDock);

        // ── 右侧 Dock：标签操作面板 ──
        auto *rightDock = new QDockWidget(tr("Tags"), this);
        rightDock->setObjectName("tags");
        auto *rightPanel = new QWidget(this);
        auto *rl = new QVBoxLayout(rightPanel);
        rl->setContentsMargins(0, 0, 0, 0);

        auto *assignedLabel = new QLabel(tr("Selected Image Tags"), this);
        assignedLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
        rl->addWidget(assignedLabel);
        m_assignedTags = new QListWidget(this);
        m_assignedTags->setStyleSheet("background:#1a1a1a; color:#ccc; border:1px solid #333;");
        rl->addWidget(m_assignedTags, 2);

        auto *addLabel = new QLabel(tr("Add Tag"), this);
        addLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
        rl->addWidget(addLabel);
        m_tagInput = new QLineEdit(this);
        m_tagInput->setPlaceholderText(tr("Type tag + Enter..."));
        m_tagInput->setStyleSheet("background:#222; color:#ccc; border:1px solid #333; padding:4px;");
        rl->addWidget(m_tagInput);

        rightPanel->setMinimumWidth(200);
        rightDock->setWidget(rightPanel);
        addDockWidget(Qt::RightDockWidgetArea, rightDock);

        // ── 状态栏 ──
        statusBar()->showMessage(tr("Ready"));

        // ── 菜单 ──
        auto *fm = menuBar()->addMenu(tr("&File"));
        fm->addAction(tr("&Open Folder..."), [this]() {
            QString d = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
            if (!d.isEmpty()) loadFolder(d);
        });
        fm->addAction(tr("&Open Image..."), [this]() {
            QString f = QFileDialog::getOpenFileName(this, tr("Open Image"));
            if (!f.isEmpty()) openInViewer(f);
        });
        fm->addSeparator();
        fm->addAction(tr("E&xit"), qApp, &QApplication::quit);

        // 文件夹点击
        connect(m_folderTree->selectionModel(), &QItemSelectionModel::currentChanged,
                [this](const QModelIndex &idx) {
            loadFolder(m_fsModel->filePath(idx));
        });
    }

    void loadFolder(const QString &path) {
        m_currentFolder = path;
        statusBar()->showMessage(path);
        // ponytail: 缩略图网格以后再实装
        m_grid->setText(tr("Thumbnail Grid — %1\n(%2 images)")
            .arg(QDir(path).dirName())
            .arg(QDir(path).entryList(QDir::Files | QDir::Readable).size()));
    }

    void openInViewer(const QString &path) {
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

private:
    QLabel          *m_grid = nullptr;
    QTreeView       *m_folderTree = nullptr;
    QFileSystemModel *m_fsModel = nullptr;
    QLineEdit       *m_tagSearch = nullptr;
    QListWidget     *m_tagSuggestions = nullptr;
    QListWidget     *m_assignedTags = nullptr;
    QLineEdit       *m_tagInput = nullptr;
    QString          m_currentFolder;
    ViewerWidget    *m_viewer = nullptr;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("QLens Manager");
    ManagerWindow w;
    w.showMaximized();
    return app.exec();
}

#include "main.moc"
