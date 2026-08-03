#include "ManagerWindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include "i18n.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QStatusBar>
#include <QImageReader>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QTimer>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>

// 翻译辅助：msgid=中文，默认中文；en.po 覆盖为英文
static QString T(const wchar_t *id) { return QString::fromWCharArray(I18n::Get(id)); }
#include <QLabel>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

// 获取系统"图片"文件夹真实路径（兼容 Known Folder 重定向到其他盘）
static QString picturesFolder()
{
#ifdef Q_OS_WIN
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path))) {
        QString result = QString::fromWCharArray(path);
        CoTaskMemFree(path);
        if (!result.isEmpty() && QDir(result).exists())
            return result;
    }
#endif
    QString fallback = QDir::homePath() + "/Pictures";
    return QDir(fallback).exists() ? fallback : QDir::homePath();
}
#include <QPushButton>
#include <QComboBox>
#include <QDir>
#include "thumbnail.h"

ManagerWindow::ManagerWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QString::fromWCharArray(I18n::Get(L"QLens 管理器")));
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
    auto *backBtn = new QPushButton(T(L"← 返回"), topBar);
    backBtn->setStyleSheet("QPushButton{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:4px 12px;}"
                           "QPushButton:hover{background:#3a3a3a; color:#fff;}");
    connect(backBtn, &QPushButton::clicked, this, &ManagerWindow::backToGrid);
    m_viewTitle = new QLabel(T(L"查看器"), topBar);
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
        m_tagPanel->setCurrentImage(p);        m_viewTitle->setText(p);  // 完整路径（不是文件名）
        setWindowTitle(QFileInfo(p).fileName() + " — QLens");
        updateViewerStatus(p);
    });
    // 右键菜单「返回网格」
    connect(m_viewer, &ViewerWidget::backRequested, this, &ManagerWindow::backToGrid);

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

    // 导航按钮：后退 / 前进 / 上级目录
    auto mkNavBtn = [&](const QString &text, const QString &tip) -> QPushButton* {
        auto *b = new QPushButton(text, toolbar);
        b->setToolTip(tip);
        b->setFixedSize(28, 26);
        b->setStyleSheet("QPushButton{background:#2a2a2a; color:#ccc; border:1px solid #444; border-radius:3px;}"
                         "QPushButton:hover{background:#3a3a3a; color:#fff;}"
                         "QPushButton:disabled{color:#555; background:#222;}");
        return b;
    };
    m_backBtn = mkNavBtn("\u2190", T(L"返回 (Alt+←)"));
    m_forwardBtn = mkNavBtn("\u2192", T(L"前进 (Alt+→)"));
    m_upBtn = mkNavBtn("\u2191", T(L"上级目录 (Backspace)"));
    connect(m_backBtn, &QPushButton::clicked, this, &ManagerWindow::goBack);
    connect(m_forwardBtn, &QPushButton::clicked, this, &ManagerWindow::goForward);
    connect(m_upBtn, &QPushButton::clicked, this, &ManagerWindow::goUp);
    tl->addWidget(m_backBtn);
    tl->addWidget(m_forwardBtn);
    tl->addWidget(m_upBtn);
    tl->addSpacing(4);

    m_pathBar = new PathBar(toolbar);
    m_pathBar->setStyleSheet("background:#141414; border:1px solid #333; border-radius:3px;");
    m_pathBar->setMinimumWidth(200);
    connect(m_pathBar, &PathBar::pathActivated, [this](const QString &p) {
        openFolder(p);
    });
    tl->addWidget(m_pathBar, 1);

    // 筛选：只显示命中标签的图片
    auto *filterLabel = new QLabel(T(L"筛选:"), toolbar);
    filterLabel->setStyleSheet("color:#888;");
    tl->addWidget(filterLabel);
    m_filterCombo = new QComboBox(toolbar);
    m_filterCombo->setEditable(true);
    m_filterCombo->setInsertPolicy(QComboBox::NoInsert);
    m_filterCombo->setPlaceholderText(T(L"按标签过滤..."));
    m_filterCombo->setMinimumWidth(140);
    m_filterCombo->setStyleSheet("QComboBox{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:3px;}");
    tl->addWidget(m_filterCombo);

    // 高亮：给网格项加颜色标记（按标签）
    auto *hlLabel = new QLabel(T(L"高亮:"), toolbar);
    hlLabel->setStyleSheet("color:#888;");
    tl->addWidget(hlLabel);
    m_highlightCombo = new QComboBox(toolbar);
    m_highlightCombo->setEditable(true);
    m_highlightCombo->setInsertPolicy(QComboBox::NoInsert);
    m_highlightCombo->setPlaceholderText(T(L"按标签高亮..."));
    m_highlightCombo->setMinimumWidth(140);
    m_highlightCombo->setStyleSheet("QComboBox{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:3px;}");
    tl->addWidget(m_highlightCombo);

    auto *clearBtn = new QPushButton(tr("\u2715"), toolbar);  // ✕
    clearBtn->setToolTip(T(L"清除过滤与高亮"));
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
    auto *leftDock = new QDockWidget(T(L"浏览"), this);
    leftDock->setObjectName("browse");
    leftDock->setWidget(m_folderPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    // ── 右侧 Dock：标签面板 ──
    m_tagPanel = new TagPanel(m_store, this);
    auto *rightDock = new QDockWidget(T(L"标签"), this);
    rightDock->setObjectName("tags");
    rightDock->setWidget(m_tagPanel);
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    // ── 状态栏 ──
    m_statusLabel = new QLabel(T(L"就绪"), this);
    m_statusLabel->setStyleSheet("color:#888; padding:2px 8px;");
    statusBar()->addPermanentWidget(m_statusLabel);

    // ── 菜单 ──
    auto *fm = menuBar()->addMenu(T(L"文件(&F)"));
    fm->addAction(T(L"打开文件夹(&O)..."), [this]() {
        QString d = QFileDialog::getExistingDirectory(this, T(L"打开文件夹"));
        if (!d.isEmpty()) openFolder(d);
    });
    fm->addAction(T(L"打开图片(&O)..."), [this]() {
        QString f = QFileDialog::getOpenFileName(this, T(L"打开图片"));
        if (!f.isEmpty()) openInViewer(f);
    });
    fm->addSeparator();
    fm->addAction(T(L"退出(&X)"), qApp, &QApplication::quit);

    // ── Settings 菜单（语言切换 + 注册默认看图器）──
    auto *sm = menuBar()->addMenu(T(L"设置(&S)"));
    auto *langMenu = sm->addMenu(T(L"语言(&L)"));
    QAction *zhAct = langMenu->addAction(tr("中文"));
    QAction *enAct = langMenu->addAction(tr("English"));
    zhAct->setCheckable(true); enAct->setCheckable(true);
    // 当前语言勾选标识（读 qlens_config.ini）
    {
        QString iniPath = QCoreApplication::applicationDirPath() + "/qlens_config.ini";
        wchar_t lang[16] = {};
        GetPrivateProfileStringW(L"General", L"language", L"", lang, 16, (LPCWSTR)iniPath.utf16());
        bool en = (_wcsicmp(lang, L"en") == 0 || _wcsicmp(lang, L"English") == 0);
        zhAct->setChecked(!en);
        enAct->setChecked(en);
    }
    connect(zhAct, &QAction::triggered, [this, zhAct, enAct]() {
        zhAct->setChecked(true); enAct->setChecked(false);
        setLanguage(QStringLiteral("zh"));
    });
    connect(enAct, &QAction::triggered, [this, zhAct, enAct]() {
        enAct->setChecked(true); zhAct->setChecked(false);
        setLanguage(QStringLiteral("en"));
    });
    sm->addSeparator();
    sm->addAction(T(L"注册为默认看图器(&R)"), [this]() { registerFileAssociations(); });

    // ── MCP 菜单（帮助介绍 MCP 功能）──
    auto *mcpMenu = menuBar()->addMenu(tr("&MCP"));
    mcpMenu->addAction(T(L"关于 MCP(&A)"), [this]() { showMcpHelp(); });

    // ── 帮助菜单（协议文档 + 关于）──
    auto *helpMenu = menuBar()->addMenu(T(L"帮助(&H)"));
    helpMenu->addAction(T(L"关于标签协议(&T)"), [this]() {
        // 打开协议文档（优先 exe 相对 docs/，回退源码目录）
        QString base = QCoreApplication::applicationDirPath();
        QStringList candidates = {
            base + "/docs/QLENS_TAG_PROTOCOL.md",
            base + "/../docs/QLENS_TAG_PROTOCOL.md",
            base + "/../src/mcp/QLENS_TAG_PROTOCOL.md",
        };
        QString doc;
        for (const QString &c : candidates)
            if (QFile::exists(c)) { doc = c; break; }
        if (doc.isEmpty()) {
            QMessageBox::information(this, T(L"关于标签协议"),
                T(L"协议文档未找到：") + candidates.first());
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(doc));
    });
    helpMenu->addSeparator();
    helpMenu->addAction(T(L"关于 QLens(&A)"), [this]() {
        QMessageBox::about(this, T(L"关于 QLens"),
            QStringLiteral("<h3>QLens 0.2.0</h3>"
                "<p>轻量图片查看器 + 管理器，围绕一套开放的图片标签协议（qltag.db）构建。</p>"
                "<p>协议文档：docs/QLENS_TAG_PROTOCOL.md</p>"
                "<p>© 2026 NDark (unknowbug)</p>"));
    });

    // ── 初始加载延迟到窗口显示后（UI 先出，缩略图/数据库后台载入）──
    QTimer::singleShot(0, [this]() {
        QString pics = QDir::homePath() + "/Pictures";
        openFolder(QDir(pics).exists() ? pics : QDir::homePath());
    });

    // ── 信号路由 ──
    connect(m_folderPanel, &FolderPanel::folderSelected, [this](const QString &path) {
        m_grid->loadFolder(path);
        refreshToolbar(path);
        updateGridStatus();
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
    // 选中变化 → 状态栏（选中数/当前文件大小）
    connect(m_grid->selectionModel(), &QItemSelectionModel::selectionChanged,
            [this]() { updateGridStatus(); });

    // ── 启动参数：图片路径 → 打开所在文件夹并查看 ──
    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) {
        QString target = args[1];
        if (QFileInfo::exists(target)) {
            QFileInfo fi(target);
            openFolder(fi.absolutePath());   // 文件管理器模式（网格）
            m_grid->selectImage(target);     // 焦点/选中该图片
        }
    } else {
        // 无参数：默认打开系统"图片"文件夹（兼容重定向到其他盘）
        openFolder(picturesFolder());
    }
}

// 统一文件夹入口：网格加载 + 左侧树同步 + 工具条刷新 + 历史记录
void ManagerWindow::openFolder(const QString &path) {
    m_grid->loadFolder(path);
    m_folderPanel->setCurrentPath(path);
    refreshToolbar(path);
    pushHistory(path);
}

// 记录浏览历史（后退/前进用）
void ManagerWindow::pushHistory(const QString &path) {
    // 若在历史中间（后退过），清掉前进分支
    while (m_historyPos < m_history.size() - 1)
        m_history.removeLast();
    // 避免重复记录同一目录
    if (m_historyPos >= 0 && m_history[m_historyPos] == path)
        return;
    m_history.append(path);
    m_historyPos = m_history.size() - 1;
    updateNavButtons();
}

void ManagerWindow::goUp() {
    QString cur = m_grid->currentFolder();
    if (cur.isEmpty()) return;
    QDir d(cur);
    d.cdUp();
    QString parent = d.absolutePath();
    if (parent != cur && QDir(parent).exists())
        openFolder(parent);
}

void ManagerWindow::goBack() {
    if (m_historyPos > 0) {
        --m_historyPos;
        QString p = m_history[m_historyPos];
        m_grid->loadFolder(p);
        m_folderPanel->setCurrentPath(p);
        refreshToolbar(p);
        updateNavButtons();
    }
}

void ManagerWindow::goForward() {
    if (m_historyPos < m_history.size() - 1) {
        ++m_historyPos;
        QString p = m_history[m_historyPos];
        m_grid->loadFolder(p);
        m_folderPanel->setCurrentPath(p);
        refreshToolbar(p);
        updateNavButtons();
    }
}

void ManagerWindow::updateNavButtons() {
    m_backBtn->setEnabled(m_historyPos > 0);
    m_forwardBtn->setEnabled(m_historyPos < m_history.size() - 1);
    QString cur = m_grid->currentFolder();
    QDir d(cur);
    m_upBtn->setEnabled(!cur.isEmpty() && d.cdUp());
}

// 文件夹变化：更新路径标签 + 刷新过滤/着色候选标签
void ManagerWindow::refreshToolbar(const QString &path) {
    m_pathBar->setPath(path);
    QStringList tags = m_grid->folderTags();
    m_updatingCombo = true;
    m_filterCombo->clear();
    m_highlightCombo->clear();
    m_filterCombo->addItems(tags);
    m_highlightCombo->addItems(tags);
    m_updatingCombo = false;
}

static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

// 状态栏：网格视图（图片数 / 选中数 / 当前选中文件大小）
void ManagerWindow::updateGridStatus()
{
    int total = m_grid->imageCount();
    QString curPath;
    int selCount = 0;
    auto sels = m_grid->selectionModel()->selectedIndexes();
    selCount = sels.size();
    if (selCount > 0)
        curPath = sels.first().data(ThumbModel::PathRole).toString();
    QString msg = QString("%1 %2  |  %3 %4")
        .arg(total).arg(T(L"张图片")).arg(selCount).arg(T(L"张选中"));
    if (!curPath.isEmpty()) {
        QFileInfo fi(curPath);
        msg += QString("  |  %1（%2）").arg(fi.fileName()).arg(formatFileSize(fi.size()));
    }
    m_statusLabel->setText(msg);
}

// 状态栏：查看器（当前图片转述信息：文件名/尺寸/格式/大小）
void ManagerWindow::updateViewerStatus(const QString &path)
{
    QImageReader r(path);
    QSize sz = r.size();
    QFileInfo fi(path);
    QString msg = QString("%1  |  %2×%3  |  %4  |  %5")
        .arg(fi.fileName())
        .arg(sz.isValid() ? sz.width() : 0)
        .arg(sz.isValid() ? sz.height() : 0)
        .arg(fi.suffix().toUpper())
        .arg(formatFileSize(fi.size()));
    m_statusLabel->setText(msg);
}

void ManagerWindow::openInViewer(const QString &path) {
    m_viewer->openFile(path);
    m_stack->setCurrentIndex(1);  // 切到查看页
}

void ManagerWindow::backToGrid() {
    m_stack->setCurrentIndex(0);
    setWindowTitle(QString::fromWCharArray(I18n::Get(L"QLens 管理器")));
    updateGridStatus();
}

void ManagerWindow::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape && m_stack->currentIndex() == 1) {
        backToGrid();
        return;
    }
    // 文件管理快捷键
    if (e->key() == Qt::Key_Backspace) { goUp(); return; }
    if (e->modifiers() & Qt::AltModifier) {
        if (e->key() == Qt::Key_Left)  { goBack(); return; }
        if (e->key() == Qt::Key_Right) { goForward(); return; }
        if (e->key() == Qt::Key_Up)    { goUp(); return; }
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

// ── Settings ──
// 切换界面语言（写 qlens_config.ini；重启生效——i18n 启动时加载）
void ManagerWindow::setLanguage(const QString &lang)
{
    QString iniPath = QCoreApplication::applicationDirPath() + "/qlens_config.ini";
    WritePrivateProfileStringW(L"General", L"language",
        (LPCWSTR)lang.utf16(), (LPCWSTR)iniPath.utf16());
    QMessageBox::information(this, T(L"设置"), T(L"语言已更改，重启后生效。"));
}

// ── MCP 帮助 ──
// 弹出帮助窗口介绍 QLens MCP Server 功能
void ManagerWindow::showMcpHelp()
{
    const QString help =
        QStringLiteral(
        "QLens MCP Server 让 AI 客户端（Claude / CherryStudio / Cursor 等）操作你的图片库。\n\n"
        "可用工具：\n"
        "  qlens_list_folder   列出文件夹图片\n"
        "  qlens_search_tag    按标签搜索\n"
        "  qlens_get_tags      读图片标签\n"
        "  qlens_set_tags      设图片标签\n"
        "  qlens_add_tags      追加标签\n"
        "  qlens_folder_tags   文件夹内所有标签\n"
        "  qlens_analyze       批量 QC 打标（自动预压缩，省 token）\n\n"
        "配置（MCP 客户端添加 stdio server）：\n"
        "  python <QLens>/src/mcp/server.py\n\n"
        "注意：请保持 Manager 运行——图片分析前自动预压缩，防止超大图直接上传烧爆 token。\n\n"
        "协议文档：docs/QLENS_TAG_PROTOCOL.md");
    QMessageBox::information(this, T(L"QLens MCP"), help);
}

// 注册系统默认看图器（图片类型 → QLens QuickView 的"打开方式"）
void ManagerWindow::registerFileAssociations()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    // ProgID 命令：exe "%1"
    std::wstring cmd = std::wstring(L"\"") + exe + L"\" \"%1\"";
    const wchar_t *exts[] = { L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp",
        L".heic", L".heif", L".avif", L".jxr", L".svg", L".tif", L".tiff" };
    HKEY k;
    // ProgID shell open command（注册到 HKCU）
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\QLensQuickView\\shell\\open\\command", 0, nullptr,
        0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"", 0, REG_SZ, (const BYTE*)cmd.c_str(),
            (DWORD)(cmd.size() * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    // 各扩展名 OpenWithProgIDs
    for (const wchar_t *ext : exts) {
        std::wstring key = std::wstring(L"Software\\Classes\\") + ext + L"\\OpenWithProgIDs";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0,
            KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, L"QLensQuickView", 0, REG_SZ, nullptr, 0);
            RegCloseKey(k);
        }
    }
    QMessageBox::information(this, T(L"设置"),
        tr("QLens QuickView registered. Right-click an image → Open with → QLens QuickView."));
}
