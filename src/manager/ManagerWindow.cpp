#include "ManagerWindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QStatusBar>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QDir>
#include "thumbnail.h"

ManagerWindow::ManagerWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("QLens Manager");
    resize(1400, 900);
    setDockNestingEnabled(true);

    // 数据层（文件夹绑定在 loadFolder 时切换）
    m_store = new TagStore(this);
    ThumbnailCache::init();  // 缩略图 SQLite 缓存

    // ── 中央：QStackedWidget（页0=网格，页1=查看器）──
    m_stack = new QStackedWidget(this);

    m_grid = new ThumbnailGrid(m_store, this);
    m_stack->addWidget(m_grid);  // page 0

    auto *viewerPage = new QWidget(this);
    auto *vl = new QVBoxLayout(viewerPage);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // 查看页顶部栏：返回按钮 + 文件名
    auto *topBar = new QWidget(viewerPage);
    topBar->setStyleSheet("background:#1e1e1e;");
    auto *topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(8, 4, 8, 4);
    auto *backBtn = new QPushButton(tr("\u2190 Back"), topBar);
    backBtn->setStyleSheet("QPushButton{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:4px 12px;}"
                           "QPushButton:hover{background:#3a3a3a; color:#fff;}");
    connect(backBtn, &QPushButton::clicked, this, &ManagerWindow::backToGrid);
    m_viewTitle = new QLabel(tr("Viewer"), topBar);
    m_viewTitle->setStyleSheet("color:#aaa; font-size:13px; padding-left:8px;");
    topLay->addWidget(backBtn);
    topLay->addWidget(m_viewTitle, 1);
    vl->addWidget(topBar);

    m_viewer = new ViewerWidget(viewerPage);
    m_viewer->installEventFilter(this);  // 双击查看器 → 返回
    vl->addWidget(m_viewer, 1);
    m_stack->addWidget(viewerPage);  // page 1

    connect(m_viewer, &ViewerWidget::imageChanged, [this](const QString &p, int) {
        // 右侧 TagPanel 跟随切图显示标签
        m_tagPanel->setCurrentImage(p);
        m_viewTitle->setText(QFileInfo(p).fileName());
        setWindowTitle(QFileInfo(p).fileName() + " — QLens");
    });

    // ── 中央容器：网格顶部工具条 + QStackedWidget ──
    auto *central = new QWidget(this);
    auto *cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    // 工具条：路径 + 过滤 + 着色 + 清除
    auto *toolbar = new QWidget(central);
    toolbar->setStyleSheet("background:#1e1e1e;");
    auto *tl = new QHBoxLayout(toolbar);
    tl->setContentsMargins(8, 4, 8, 4);
    tl->setSpacing(6);

    m_pathLabel = new QLabel(tr("(no folder)"), toolbar);
    m_pathLabel->setStyleSheet("color:#aaa; font-size:12px;");
    m_pathLabel->setMinimumWidth(160);
    tl->addWidget(m_pathLabel, 1);

    m_filterCombo = new QComboBox(toolbar);
    m_filterCombo->setEditable(true);
    m_filterCombo->setInsertPolicy(QComboBox::NoInsert);
    m_filterCombo->setPlaceholderText(tr("Filter by tag..."));
    m_filterCombo->setMinimumWidth(140);
    m_filterCombo->setStyleSheet("QComboBox{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:3px;}");
    tl->addWidget(m_filterCombo);

    m_highlightCombo = new QComboBox(toolbar);
    m_highlightCombo->setEditable(true);
    m_highlightCombo->setInsertPolicy(QComboBox::NoInsert);
    m_highlightCombo->setPlaceholderText(tr("Highlight by tag..."));
    m_highlightCombo->setMinimumWidth(140);
    m_highlightCombo->setStyleSheet("QComboBox{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:3px;}");
    tl->addWidget(m_highlightCombo);

    auto *clearBtn = new QPushButton(tr("\u2715"), toolbar);  // ✕
    clearBtn->setToolTip(tr("Clear filter & highlight"));
    clearBtn->setFixedSize(26, 24);
    clearBtn->setStyleSheet("QPushButton{background:#3a2a2a; color:#d88; border:1px solid #555;}"
                            "QPushButton:hover{background:#4a3a3a;}");
    connect(clearBtn, &QPushButton::clicked, [this]() {
        m_filterCombo->setCurrentText("");
        m_highlightCombo->setCurrentText("");
        m_grid->setFilterTag("");
        m_grid->setHighlightTag("");
    });
    tl->addWidget(clearBtn);

    cl->addWidget(toolbar);
    cl->addWidget(m_stack, 1);
    setCentralWidget(central);

    // 过滤/着色信号
    connect(m_filterCombo, &QComboBox::currentTextChanged, [this](const QString &t) {
        if (m_updatingCombo) return;
        m_grid->setFilterTag(t);
    });
    connect(m_highlightCombo, &QComboBox::currentTextChanged, [this](const QString &t) {
        if (m_updatingCombo) return;
        m_grid->setHighlightTag(t);
    });

    // ── 左侧 Dock：文件夹树 + 标签搜索 ──
    m_folderPanel = new FolderPanel(this);
    auto *leftDock = new QDockWidget(tr("Browse"), this);
    leftDock->setObjectName("browse");
    leftDock->setWidget(m_folderPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    // ── 右侧 Dock：标签面板 ──
    m_tagPanel = new TagPanel(m_store, this);
    auto *rightDock = new QDockWidget(tr("Tags"), this);
    rightDock->setObjectName("tags");
    rightDock->setWidget(m_tagPanel);
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    // ── 状态栏 ──
    statusBar()->showMessage(tr("Ready"));

    // ── 菜单 ──
    auto *fm = menuBar()->addMenu(tr("&File"));
    fm->addAction(tr("&Open Folder..."), [this]() {
        QString d = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
        if (!d.isEmpty()) openFolder(d);
    });
    fm->addAction(tr("&Open Image..."), [this]() {
        QString f = QFileDialog::getOpenFileName(this, tr("Open Image"));
        if (!f.isEmpty()) openInViewer(f);
    });
    fm->addSeparator();
    fm->addAction(tr("E&xit"), qApp, &QApplication::quit);

    // ── 信号路由 ──
    connect(m_folderPanel, &FolderPanel::folderSelected, [this](const QString &path) {
        m_grid->loadFolder(path);
        refreshToolbar(path);
    });

    connect(m_grid, &ThumbnailGrid::folderDoubleClicked, [this](const QString &path) {
        openFolder(path);
    });

    connect(m_grid, &ThumbnailGrid::imageDoubleClicked, [this](const QString &path) {
        m_tagPanel->setCurrentImage(path);
        openInViewer(path);
    });

    connect(m_grid, &ThumbnailGrid::imageClicked, [this](const QString &path) {
        m_tagPanel->setCurrentImage(path);
    });

    // ── 启动参数：图片路径 → 打开所在文件夹并查看 ──
    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) {
        QString target = args[1];
        if (QFileInfo::exists(target)) {
            QFileInfo fi(target);
            openFolder(fi.absolutePath());
            openInViewer(target);
        }
    }
}

// 统一文件夹入口：网格加载 + 左侧树同步 + 工具条刷新
void ManagerWindow::openFolder(const QString &path) {
    m_grid->loadFolder(path);
    m_folderPanel->setCurrentPath(path);
    refreshToolbar(path);
}

// 文件夹变化：更新路径标签 + 刷新过滤/着色候选标签
void ManagerWindow::refreshToolbar(const QString &path) {
    m_pathLabel->setText(QDir(path).dirName().isEmpty() ? path : QDir(path).dirName());
    m_pathLabel->setToolTip(path);
    QStringList tags = m_grid->folderTags();
    m_updatingCombo = true;
    m_filterCombo->clear();
    m_highlightCombo->clear();
    m_filterCombo->addItems(tags);
    m_highlightCombo->addItems(tags);
    m_updatingCombo = false;
}

void ManagerWindow::openInViewer(const QString &path) {
    m_viewer->openFile(path);
    m_stack->setCurrentIndex(1);  // 切到查看页
}

void ManagerWindow::backToGrid() {
    m_stack->setCurrentIndex(0);
    setWindowTitle("QLens Manager");
}

void ManagerWindow::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape && m_stack->currentIndex() == 1) {
        backToGrid();
        return;
    }
    QMainWindow::keyPressEvent(e);
}

bool ManagerWindow::eventFilter(QObject *obj, QEvent *event) {
    // 查看器中双击 → 返回文件夹视图
    if (obj == m_viewer && event->type() == QEvent::MouseButtonDblClick) {
        if (m_stack->currentIndex() == 1) {
            backToGrid();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}
