#ifndef NOMINMAX
#define NOMINMAX   // 防 Windows min/max 宏与 QtConcurrent 冲突
#endif
#include "ManagerWindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include "i18n.h"
#include "decode_api.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QStatusBar>
#include <QImageReader>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QTimer>
#include <QCloseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QByteArray>
#include <QDebug>
#include <QProcess>
#include <cstdio>
#include <QScreen>
#include <QApplication>
#include <shlobj.h>
#include <shellapi.h>
#include <objbase.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QImage>
#include <QVector>

// 翻译辅助：msgid=中文，默认中文；en.po 覆盖为英文
static QString T(const wchar_t *id) { return QString::fromWCharArray(I18n::Get(id)); }

// ── 本地 QC 检测（固定标 = 非 AI 能靠谱检测的）──
// 过曝：高光(>0.9 亮度)占比；色偏：RGB 通道均值偏移；模糊：灰度拉普拉斯方差
static QStringList detectQc(const QImage &src) {
    QStringList hits;
    QImage img = (src.width() > 256 || src.height() > 256)
        ? src.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
               .convertToFormat(QImage::Format_RGB32)
        : src.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    const qint64 total = (qint64)w * h;
    if (total < 16) return hits;

    qint64 sumR = 0, sumG = 0, sumB = 0, over = 0;
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            sumR += r; sumG += g; sumB += b;
            if (qGray(r, g, b) > 229) ++over;   // 亮度 > 0.9
        }
    }
    if ((double)over / total > 0.15) hits << QStringLiteral("曝光过度");

    double mr = sumR / (double)total, mg = sumG / (double)total, mb = sumB / (double)total;
    double dev = qMax(qAbs(mr - mg), qMax(qAbs(mg - mb), qAbs(mr - mb)));
    if (dev > 20.0) hits << QStringLiteral("色偏");

    // 拉普拉斯方差（灰度 3x3 卷积）——低方差 = 模糊
    if (w >= 8 && h >= 8) {
        QVector<int> lap((w - 2) * (h - 2));
        int idx = 0;
        for (int y = 1; y < h - 1; ++y) {
            const QRgb *p0 = reinterpret_cast<const QRgb *>(img.constScanLine(y - 1));
            const QRgb *p1 = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            const QRgb *p2 = reinterpret_cast<const QRgb *>(img.constScanLine(y + 1));
            for (int x = 1; x < w - 1; ++x) {
                int center = qGray(p1[x]);
                int neigh = qGray(p0[x-1]) + qGray(p0[x]) + qGray(p0[x+1])
                          + qGray(p1[x-1]) + qGray(p1[x+1])
                          + qGray(p2[x-1]) + qGray(p2[x]) + qGray(p2[x+1]);
                lap[idx++] = neigh - 8 * center;
            }
        }
        double mean = 0;
        for (int v : lap) mean += v;
        mean /= lap.size();
        double var = 0;
        for (int v : lap) { double d = v - mean; var += d * d; }
        var /= lap.size();
        if (var < 60.0) hits << QStringLiteral("模糊");
    }
    return hits;
}
// 定位默认图片文件夹（前向声明——定义在下方）
static QString findDefaultFolder();
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
#include <QSlider>
#include <QDir>
#include "thumbnail.h"

ManagerWindow::ManagerWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QString::fromWCharArray(I18n::Get(L"QLens 管理器")));
    // 默认尺寸：屏幕可用区 80%（分发兼容小屏/高 DPI），最大化为常态
    {
        QScreen *scr = screen();
        if (scr) {
            QSize avail = scr->availableGeometry().size();
            resize(avail.width() * 4 / 5, avail.height() * 4 / 5);
        } else {
            resize(1280, 800);
        }
    }
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
    tl->setSpacing(4);

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

    m_pathBar = new PathBar(toolbar);
    m_pathBar->setStyleSheet("background:#141414; border:1px solid #333; border-radius:3px;");
    m_pathBar->setMinimumWidth(200);
    connect(m_pathBar, &PathBar::pathActivated, [this](const QString &p) {
        openFolder(p);
    });
    tl->addWidget(m_pathBar, 1);

    // 固定标（QC）筛选：独立于普通标签——工具栏最左侧（与 tag 筛选 AND 叠加）
    auto *qcLabel = new QLabel(T(L"QC:"), toolbar);
    qcLabel->setStyleSheet("color:#888;");
    qcLabel->setToolTip(T(L"固定标筛选（过曝/模糊/色偏等 CV 可检测标签）"));
    tl->addWidget(qcLabel);
    m_qcCombo = new QComboBox(toolbar);
    m_qcCombo->setInsertPolicy(QComboBox::NoInsert);
    m_qcCombo->setMinimumWidth(110);
    m_qcCombo->setStyleSheet(
        "QComboBox{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:3px;}"
        "QComboBox QAbstractItemView{background:#222; color:#eee; selection-background-color:#335;}");
    tl->addWidget(m_qcCombo);
    // 首项"全部"（空 = 不过滤固定标）
    m_qcCombo->addItem(T(L"全部"), QString());
    connect(m_qcCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int) {
                if (m_updatingCombo) return;   // 刷新 toolbar 时跳过——否则单击后 reset 清 selection
                m_grid->setFilterQc(m_qcCombo->currentData().toString());
            });

    // 筛选：只显示命中标签的图片（候选排除 QC 固定标——但手动输入仍可组合）
    auto *filterLabel = new QLabel(T(L"筛选:"), toolbar);
    filterLabel->setStyleSheet("color:#888;");
    tl->addWidget(filterLabel);
    m_filterCombo = new QComboBox(toolbar);
    m_filterCombo->setEditable(true);
    m_filterCombo->setInsertPolicy(QComboBox::NoInsert);
    m_filterCombo->setPlaceholderText(T(L"按标签过滤(逗号=多标签AND)..."));
    m_filterCombo->setMinimumWidth(140);
    m_filterCombo->setStyleSheet(
        "QComboBox{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:3px;}"
        "QComboBox QAbstractItemView{background:#222; color:#eee; selection-background-color:#335;}");
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
    m_highlightCombo->setStyleSheet(
        "QComboBox{background:#2a2a2a; color:#ccc; border:1px solid #444; padding:3px;}"
        "QComboBox QAbstractItemView{background:#222; color:#eee; selection-background-color:#335;}");
    tl->addWidget(m_highlightCombo);

    auto *clearBtn = new QPushButton(tr("\u2715"), toolbar);  // ✕
    clearBtn->setToolTip(T(L"清除过滤与高亮"));
    clearBtn->setFixedSize(26, 24);
    clearBtn->setStyleSheet("QPushButton{background:#3a2a2a; color:#d88; border:1px solid #555;}"
                            "QPushButton:hover{background:#4a3a3a;}");
    connect(clearBtn, &QPushButton::clicked, [this]() {
        m_filterCombo->setCurrentText("");
        m_highlightCombo->setCurrentText("");
        m_qcCombo->setCurrentIndex(0);   // 固定标回"全部"
        m_grid->setFilterTag("");
        m_grid->setHighlightTag("");
        m_grid->setFilterQc("");
    });
    tl->addWidget(clearBtn);

    // QC 批量检测：本地 CV（曝光过度/模糊/色偏）→ 写固定标
    auto *qcBtn = new QPushButton(T(L"QC 检测"), toolbar);
    qcBtn->setToolTip(T(L"批量检测当前文件夹（曝光过度/模糊/色偏）并打固定标"));
    qcBtn->setStyleSheet("QPushButton{background:#2a3a2a; color:#8d8; border:1px solid #454;}"
                         "QPushButton:hover{background:#3a4a3a;}"
                         "QPushButton:disabled{background:#222; color:#555; border:1px solid #333;}");
    connect(qcBtn, &QPushButton::clicked, this, &ManagerWindow::runQcDetection);
    tl->addWidget(qcBtn);

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
    m_leftDock = leftDock;

    // ── 右侧 Dock：标签面板 ──
    m_tagPanel = new TagPanel(m_store, this);
    auto *rightDock = new QDockWidget(T(L"标签"), this);
    rightDock->setObjectName("tags");
    rightDock->setWidget(m_tagPanel);
    addDockWidget(Qt::RightDockWidgetArea, rightDock);
    m_rightDock = rightDock;

    // ── 状态栏 ──
    m_statusLabel = new QLabel(T(L"就绪"), this);
    m_statusLabel->setStyleSheet("color:#888; padding:2px 8px;");
    statusBar()->addPermanentWidget(m_statusLabel);

    // 状态栏左侧：缩略图大小 / 查看器缩放 滑块（view 态自动挂钩图片缩放）
    m_sizeSlider = new QSlider(Qt::Horizontal, this);
    m_sizeSlider->setFixedWidth(160);
    m_sizeSlider->setRange(32, 480);
    m_sizeSlider->setValue(m_grid->thumbSize());
    m_sizeLabel = new QLabel(QString("%1: %2").arg(T(L"缩略图")).arg(m_grid->thumbSize()), this);
    m_sizeLabel->setStyleSheet("color:#888; padding:0 6px;");
    m_sizeLabel->setMinimumWidth(130);   // 固定标签宽度：文本长度变化不影响滑块位置
    m_sizeLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    statusBar()->addWidget(m_sizeLabel);
    statusBar()->addWidget(m_sizeSlider);
    connect(m_sizeSlider, &QSlider::valueChanged, [this](int v) {
        if (m_stack->currentIndex() == 0) {
            m_grid->setThumbSize(v);
            m_sizeLabel->setText(QString("%1: %2").arg(T(L"缩略图")).arg(m_grid->thumbSize()));
        } else {
            m_viewer->setZoomPercent(v);
            m_sizeLabel->setText(QString("%1: %2%").arg(T(L"缩放")).arg(v));
        }
    });
    // 查看器缩放变化（F/S/右键/滚轮）→ 滑块同步
    connect(m_viewer, &ViewerWidget::zoomChanged, [this](double pct) {
        if (m_stack->currentIndex() != 1) return;
        const int v = (int)qRound(pct);
        m_sizeSlider->blockSignals(true);
        m_sizeSlider->setValue(v);
        m_sizeSlider->blockSignals(false);
        m_sizeLabel->setText(QString("%1: %2%").arg(T(L"缩放")).arg(v));
    });
    // 网格 Ctrl+滚轮缩放 → 滑块同步
    connect(m_grid, &ThumbnailGrid::thumbSizeChanged, [this](int v) {
        if (m_stack->currentIndex() != 0) return;
        m_sizeSlider->blockSignals(true);
        m_sizeSlider->setValue(v);
        m_sizeSlider->blockSignals(false);
        m_sizeLabel->setText(QString("%1: %2").arg(T(L"缩略图")).arg(v));
        // 持久化缩略图大小（重启恢复）
        wchar_t buf[16];
        swprintf_s(buf, 16, L"%d", v);
        std::wstring ini = I18n::ConfigIniPath(true);
        WritePrivateProfileStringW(L"View", L"thumb_size", buf, ini.c_str());
    });

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
    // 标签导出/导入（协议生态：CSV/JSON 交换，走 qlens_lib CLI）
    fm->addAction(T(L"导出标签(&E)..."), [this]() {
        QString folder = m_grid->currentFolder();
        if (folder.isEmpty()) return;
        QString out = QFileDialog::getSaveFileName(this, T(L"导出标签"),
            folder + "/tags.csv", T(L"CSV (*.csv);;JSON (*.json)"));
        if (out.isEmpty()) return;
        QString fmt = out.endsWith(".json", Qt::CaseInsensitive) ? "json" : "csv";
        QString lib = QCoreApplication::applicationDirPath() + "/../src/mcp/qlens_lib.py";
        QProcess::startDetached("python", {lib, "export", folder, out, fmt});
        m_statusLabel->setText(T(L"导出标签：") + out);
    });
    fm->addAction(T(L"导入标签(&I)..."), [this]() {
        QString folder = m_grid->currentFolder();
        if (folder.isEmpty()) return;
        QString in = QFileDialog::getOpenFileName(this, T(L"导入标签"),
            folder, T(L"CSV (*.csv);;JSON (*.json)"));
        if (in.isEmpty()) return;
        QString fmt = in.endsWith(".json", Qt::CaseInsensitive) ? "json" : "csv";
        QString lib = QCoreApplication::applicationDirPath() + "/../src/mcp/qlens_lib.py";
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this, [this, proc, in]() {
            m_statusLabel->setText(T(L"导入标签完成：") + in);
            m_grid->refreshCurrentFolder();   // 刷新缩略图角标/候选
            proc->deleteLater();
        });
        proc->start("python", {lib, "import", folder, in, fmt});
    });
    fm->addSeparator();
    fm->addAction(T(L"退出(&X)"), qApp, &QApplication::quit);

    // ── Settings 菜单（语言切换 + 注册默认看图器）──
    auto *sm = menuBar()->addMenu(T(L"设置(&S)"));
    auto *langMenu = sm->addMenu(T(L"语言(&L)"));
    QAction *zhAct = langMenu->addAction(tr("中文"));
    QAction *enAct = langMenu->addAction(tr("English"));
    zhAct->setCheckable(true); enAct->setCheckable(true);
    // 当前语言勾选标识（读 qlens_config.ini——分发兼容路径）
    {
        std::wstring iniPath = I18n::ConfigIniPath(false);
        wchar_t lang[16] = {};
        GetPrivateProfileStringW(L"General", L"language", L"", lang, 16, iniPath.c_str());
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
        // 定位协议文档所在目录（发行版 docs/ 或源码树 docs/），打开目录让用户自选阅读
        QString base = QCoreApplication::applicationDirPath();
        QStringList candidates = {
            base + "/docs/QLENS_TAG_PROTOCOL.md",     // 发行版：exe 旁 docs/
            base + "/QLENS_TAG_PROTOCOL.md",          // 发行版：协议文档直接 exe 旁
            base + "/../docs/QLENS_TAG_PROTOCOL.md",   // 源码树：build-qv/Release/../..
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
        // 不直接打开 MD（避免语言/编辑器关联问题），打开存放目录让用户自选
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(doc).absolutePath()));
    });
    helpMenu->addSeparator();
    helpMenu->addAction(T(L"关于 QLens(&A)"), [this]() {
        QMessageBox::about(this, T(L"关于 QLens"),
            QStringLiteral("<h3>QLens 0.2.1</h3>"
                "<p>") + T(L"轻量图片查看器 + 管理器，围绕一套开放的图片标签协议（qltag.db）构建。") +
                QStringLiteral("</p><p>") + T(L"协议文档：docs/QLENS_TAG_PROTOCOL.md") +
                QStringLiteral("</p><p>© 2026 N.T.Black (unknowbug)</p>"));
    });

    // ── 初始加载延迟到窗口显示后（UI 先出，缩略图/数据库后台载入）──
    QTimer::singleShot(0, [this]() { openFolder(findDefaultFolder()); });

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
    // 选中变化 → 状态栏 + 右侧标签面板（单选=当前图；多选=批量提示）
    connect(m_grid->selectionModel(), &QItemSelectionModel::selectionChanged,
            [this]() {
        updateGridStatus();
        auto sels = m_grid->selectionModel()->selectedIndexes();
        if (sels.size() == 1) {
            const QString p = sels.first().data(ThumbModel::PathRole).toString();
            if (!p.isEmpty()) m_tagPanel->setCurrentImage(p);
        } else if (sels.size() > 1) {
            QStringList paths;
            for (const auto &s : sels)
                paths << s.data(ThumbModel::PathRole).toString();
            m_tagPanel->showMultiSelection(paths);
        }
    });
    // 打标后刷新筛选/高亮候选
    connect(m_tagPanel, &TagPanel::tagsChanged,
            [this](const QString &, const QStringList &) {
        refreshToolbar(m_grid->currentFolder());
    });

    // 批量打标/删标（网格右键）→ 刷新候选 + 缩略图角标
    connect(m_grid, &ThumbnailGrid::tagsChanged, [this]() {
        refreshToolbar(m_grid->currentFolder());
        m_grid->refreshCurrentFolder();
    });

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

    // 程序被剪切/移动后自动重注册关联（静默——防右键菜单出现无效应用）
    checkAssociationsOnStart();

    // 恢复上次的缩略图大小（[View] thumb_size；默认 160）
    {
        std::wstring ini = I18n::ConfigIniPath(false);
        wchar_t ts[16] = {};
        GetPrivateProfileStringW(L"View", L"thumb_size", L"", ts, 16, ini.c_str());
        if (ts[0]) m_grid->setThumbSize(_wtoi(ts));
    }

    // ── 界面布局持久化：dock 拖动/可见性/窗口移动缩放 → 防抖自动保存 ──
    m_layoutTimer = new QTimer(this);
    m_layoutTimer->setSingleShot(true);
    m_layoutTimer->setInterval(600);
    connect(m_layoutTimer, &QTimer::timeout, this, &ManagerWindow::saveLayout);

    const auto docks = findChildren<QDockWidget *>();
    for (QDockWidget *dw : docks) {
        connect(dw, &QDockWidget::topLevelChanged, this, [this](bool) { scheduleLayoutSave(); });
        connect(dw, &QDockWidget::visibilityChanged, this, [this](bool) { scheduleLayoutSave(); });
    }
    // dock 分隔条/内部 splitter 在布局激活后才有（构造时 QMainWindow 内部 splitter 未创建）
    // → showEvent 里统一连接（见 showEvent）
}

// ── 界面布局持久化：qlens_config.ini [Layout]（geometry/state base64）──

// 防抖：任意布局变化后 600ms 保存一次（拖动过程不频繁写盘）
void ManagerWindow::scheduleLayoutSave() {
    if (m_layoutTimer) m_layoutTimer->start();
}

// 立即保存（状态与上次一致则跳过写盘）
void ManagerWindow::saveLayout() {
    QByteArray st = saveState();
    if (st == m_lastLayoutState) return;
    m_lastLayoutState = st;
    std::wstring ini = I18n::ConfigIniPath(true);
    WritePrivateProfileStringW(L"Layout", L"state",
        QString::fromLatin1(st.toBase64()).toStdWString().c_str(), ini.c_str());
}

// 启动恢复：只读 dock 布局（窗口永远最大化，不恢复 geometry）
bool ManagerWindow::restoreLayout() {
    std::wstring ini = I18n::ConfigIniPath(false);
    wchar_t stB64[16384] = {};
    GetPrivateProfileStringW(L"Layout", L"state", L"", stB64, 16384, ini.c_str());
    // 清理旧版键（geometry/splitter_sizes/splitter_ratios 不再使用）
    WritePrivateProfileStringW(L"Layout", L"geometry", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"Layout", L"splitter_sizes", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"Layout", L"splitter_ratios", nullptr, ini.c_str());
    bool ok = stB64[0] != 0;
    if (ok) {
        // dock 布局 → 显示后恢复（不阻塞快速启动）
        QTimer::singleShot(0, this, [this, stS = QString::fromWCharArray(stB64)] {
            restoreDockState(stS);
        });
    }
    return ok;
}

// show 后：恢复 dock 布局 + 连接 dock splitter（内部面板高度不持久化——用户决定不做）
void ManagerWindow::restoreDockState(const QString &stB64) {
    std::wstring ini = I18n::ConfigIniPath(false);
    if (!stB64.isEmpty()) {
        QByteArray st = QByteArray::fromBase64(stB64.toLatin1());
        qInfo() << "[layout] restoring state bytes=" << st.size();
        if (restoreState(st)) {
            m_lastLayoutState = st;
        } else {
            // 坏 state：清除并重建默认 dock 布局（防反复启动失败）
            WritePrivateProfileStringW(L"Layout", L"state", nullptr, ini.c_str());
            if (m_leftDock)  addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);
            if (m_rightDock) addDockWidget(Qt::RightDockWidgetArea, m_rightDock);
        }
    }
    // 布局激活后才有 dock splitter：现在连接，拖动分隔条才触发保存
    connectSplitters();
}

// 连接所有 QSplitter（dock 分隔条 + 内部可调栏），拖动触发防抖保存
void ManagerWindow::connectSplitters() {
    const auto splitters = findChildren<QSplitter *>();
    for (QSplitter *sp : splitters) {
        if (sp->property("qlenSplitterConnected").toBool()) continue;
        connect(sp, &QSplitter::splitterMoved, this, [this](int, int) { scheduleLayoutSave(); });
        sp->setProperty("qlenSplitterConnected", true);
    }
}

void ManagerWindow::showEvent(QShowEvent *e) {
    QMainWindow::showEvent(e);
    connectSplitters();   // 首次显示后连接所有 splitter（构造时 dock splitter 未创建）
}

void ManagerWindow::closeEvent(QCloseEvent *e) {
    saveLayout();
    // 正常关闭标记：下次启动才恢复布局（崩溃/异常退出 → 自愈跳过恢复）
    std::wstring ini = I18n::ConfigIniPath(true);
    WritePrivateProfileStringW(L"Layout", L"trusted", L"1", ini.c_str());
    QMainWindow::closeEvent(e);
}
void ManagerWindow::moveEvent(QMoveEvent *e)   { QMainWindow::moveEvent(e); scheduleLayoutSave(); }
void ManagerWindow::resizeEvent(QResizeEvent *e) { QMainWindow::resizeEvent(e); scheduleLayoutSave(); }

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
    // 用传入 path 直接查 tag（不依赖 m_grid->m_currentFolder 时序）
    QStringList tags = TagStore::queryFolderTags(path);
    // 普通筛选/高亮候选排除 QC 固定标（独立成 QC 下拉——不互斥，手动输入仍可组合）
    QStringList qcs = m_store->qcTagNames();
    QStringList normal;
    for (const QString &t : tags)
        if (!qcs.contains(t)) normal << t;
    m_updatingCombo = true;
    m_filterCombo->clear();
    m_highlightCombo->clear();
    m_filterCombo->addItems(normal);
    m_highlightCombo->addItems(normal);
    // 固定标列表（category='qc'）——保留首项"全部"
    QString curQc = m_qcCombo->currentData().toString();
    m_qcCombo->clear();
    m_qcCombo->addItem(T(L"全部"), QString());
    if (m_store && m_store->isOpen()) {
        QStringList qcs = m_store->qcTagNames();
        for (const QString &q : qcs) m_qcCombo->addItem(q, q);
        int restore = m_qcCombo->findData(curQc);
        m_qcCombo->setCurrentIndex(restore >= 0 ? restore : 0);
    }
    m_updatingCombo = false;
}

static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

// 定位用户图片文件夹（分发/移动兼容）：
// 1) 系统注册位置（SHGetKnownFolderPath——用户通过"位置"设置移动过 → 正确）
// 2) 默认 %USERPROFILE%/Pictures
// 3) OneDrive Pictures（部分系统默认在 OneDrive 下）
// 4) 兜底用户目录
static QString findDefaultFolder()
{
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path)) && path) {
        QString p = QString::fromWCharArray(path);
        CoTaskMemFree(path);
        if (QDir(p).exists()) return p;
    }
    QString homePics = QDir::homePath() + "/Pictures";
    if (QDir(homePics).exists()) return homePics;
    QString odPics = QDir::homePath() + "/OneDrive/Pictures";
    if (QDir(odPics).exists()) return odPics;
    return QDir::homePath();
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
    // 滑块切到"图片缩放"模式（5%=缩小下限，400% 上限；初始=当前实际缩放——适配结果）
    // blockSignals：setRange 会把旧值 clamp 到新范围并误发 valueChanged（改网格缩略图）
    const int initZoom = (int)qRound(m_viewer->zoomPercent());
    m_sizeSlider->blockSignals(true);
    m_sizeSlider->setRange(5, 400);
    m_sizeSlider->setValue(initZoom);
    m_sizeSlider->blockSignals(false);
    m_sizeLabel->setText(QString("%1: %2%").arg(T(L"缩放")).arg(initZoom));
}

void ManagerWindow::backToGrid() {
    m_stack->setCurrentIndex(0);
    setWindowTitle(QString::fromWCharArray(I18n::Get(L"QLens 管理器")));
    updateGridStatus();
    // 滑块切回"缩略图大小"模式（blockSignals：setRange clamp 旧值会误触发 setThumbSize）
    m_sizeSlider->blockSignals(true);
    m_sizeSlider->setRange(32, 480);
    m_sizeSlider->setValue(m_grid->thumbSize());
    m_sizeSlider->blockSignals(false);
    m_sizeLabel->setText(QString("%1: %2").arg(T(L"缩略图")).arg(m_grid->thumbSize()));
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

// ── QC 批量检测（后台 + 进度条）：曝光过度/模糊/色偏 → 写固定标 ──
void ManagerWindow::runQcDetection() {
    if (m_qcRunning) return;
    QString folder = m_grid->currentFolder();
    QStringList paths = m_grid->allImagePaths();
    if (folder.isEmpty() || paths.isEmpty()) {
        m_statusLabel->setText(T(L"当前文件夹没有图片"));
        return;
    }
    m_qcRunning = true;

    // 进度对话框（防止反复点 QC 按钮；可取消）
    auto *dlg = new QProgressDialog(T(L"QC 检测中..."), T(L"取消"), 0, paths.size(), this);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    dlg->setAutoClose(false);
    dlg->setAutoReset(false);
    dlg->setWindowTitle(T(L"QC 检测"));
    dlg->show();

    auto *watcher = new QFutureWatcher<QPair<QString, QStringList>>(this);
    connect(watcher, &QFutureWatcher<QPair<QString, QStringList>>::progressValueChanged,
            dlg, &QProgressDialog::setValue);
    connect(watcher, &QFutureWatcher<QPair<QString, QStringList>>::finished,
            this, [this, watcher, dlg, paths]() {
        int nOver = 0, nBlur = 0, nColor = 0;
        if (!watcher->future().isCanceled()) {
            const auto results = watcher->future().results();
            for (const auto &r : results) {
                for (const QString &t : r.second) {
                    m_store->addImageTag(QFileInfo(r.first).fileName(), t);
                    if (t == QStringLiteral("曝光过度")) ++nOver;
                    else if (t == QStringLiteral("模糊")) ++nBlur;
                    else if (t == QStringLiteral("色偏")) ++nColor;
                }
            }
        }
        m_qcRunning = false;
        dlg->close();
        dlg->deleteLater();
        if (watcher->future().isCanceled()) {
            m_statusLabel->setText(T(L"QC 检测已取消"));
        } else {
            m_statusLabel->setText(T(L"QC 检测完成：") +
                QStringLiteral("过曝 %1 | 模糊 %2 | 色偏 %3")
                    .arg(nOver).arg(nBlur).arg(nColor));
            // 刷新缩略图（emoji 角标出现）+ 维持当前 QC 筛选
            m_grid->refreshCurrentFolder();
            m_grid->setFilterQc(m_qcCombo->currentData().toString());
        }
        watcher->deleteLater();
    });
    connect(dlg, &QProgressDialog::canceled, [watcher]() {
        watcher->future().cancel();
    });
    watcher->setFuture(QtConcurrent::mapped(paths, [](const QString &p) {
        QImage img = QLensCore::decodeImage(p);
        if (img.isNull()) return QPair<QString, QStringList>(p, QStringList());
        return QPair<QString, QStringList>(p, detectQc(img));
    }));
}

// ── Settings ──
// 切换界面语言（写 qlens_config.ini；重启生效——i18n 启动时加载）
void ManagerWindow::setLanguage(const QString &lang)
{
    std::wstring iniPath = I18n::ConfigIniPath(true);  // 分发兼容：exe 旁可写 → AppData
    WritePrivateProfileStringW(L"General", L"language",
        (LPCWSTR)lang.utf16(), iniPath.c_str());
    QMessageBox::information(this, T(L"设置"), T(L"语言已切换，程序将自动重启。"));
    // 自动重启应用新语言（无需手动重启）——保留当前文件夹
    QStringList args;
    const QString cur = m_grid->currentFolder();
    if (!cur.isEmpty()) args << cur;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args,
                            QCoreApplication::applicationDirPath());
    QTimer::singleShot(300, qApp, &QApplication::quit);
}

// ── MCP 帮助 ──
// 弹出帮助窗口介绍 QLens MCP Server 功能
void ManagerWindow::showMcpHelp()
{
    const QString help =
        T(L"QLens MCP Server 让 AI 客户端（Claude / CherryStudio / Cursor 等）操作你的图片库。") + "\n\n" +
        T(L"可用工具（14）：") + "\n" +
        T(L"  查询：") + "\n" +
        T(L"    qlens_list_folder   列出文件夹图片+标签") + "\n" +
        T(L"    qlens_search_tag    按单个标签搜索") + "\n" +
        T(L"    qlens_combo_search  多标签组合搜索（AND/OR）") + "\n" +
        T(L"    qlens_get_tags      读图片标签") + "\n" +
        T(L"    qlens_folder_tags   文件夹内所有标签") + "\n" +
        T(L"    qlens_tag_stats     标签统计（每标签图片数）") + "\n" +
        T(L"  打标：") + "\n" +
        T(L"    qlens_set_tags      全量设置标签（替换/清空）") + "\n" +
        T(L"    qlens_add_tags      追加标签") + "\n" +
        T(L"    qlens_export_tags   导出标签（CSV/JSON）") + "\n" +
        T(L"    qlens_import_tags   导入标签（CSV/JSON）") + "\n" +
        T(L"  文件：") + "\n" +
        T(L"    qlens_move_files    移动/归档（标签迁移）") + "\n" +
        T(L"    qlens_rename_files  重命名（标签迁移）") + "\n" +
        T(L"    qlens_delete_files  永久删除（需客户端确认）") + "\n" +
        T(L"  分析：") + "\n" +
        T(L"    qlens_analyze       批量 QC 质检打标（本地 OpenCV，零 token）") + "\n\n" +
        T(L"配置（MCP 客户端添加 stdio server）：") + "\n" +
        T(L"  python <QLens安装目录>/mcp/server.py") + "\n\n" +
        T(L"协议文档：docs/QLENS_TAG_PROTOCOL.md");
    QMessageBox::information(this, T(L"QLens MCP"), help);
}

// 注册系统默认看图器（图片类型 → QLens QuickView 的"打开方式"）
void ManagerWindow::registerFileAssociations()
{
    doRegisterAssociations(true);
}

// 启动检测：若已注册但 exe 路径已变（程序被剪切/移动），静默重注册覆盖旧关联
void ManagerWindow::checkAssociationsOnStart()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t selfDir[MAX_PATH];
    wcscpy_s(selfDir, self);
    wchar_t *sl = wcsrchr(selfDir, L'\\');
    if (sl) *sl = 0;
    // 期望命令：当前目录的 qlens_quickview.exe
    std::wstring expect = L"\"" + std::wstring(selfDir) + L"\\qlens_quickview.exe\" \"%1\"";
    HKEY k;
    wchar_t cur[1024] = {};
    DWORD sz = sizeof(cur);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\QLensQuickView\\shell\\open\\command", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;  // 未注册——尊重用户选择，不自动注册
    RegQueryValueExW(k, L"", nullptr, nullptr, (LPBYTE)cur, &sz);
    RegCloseKey(k);
    if (_wcsicmp(cur, expect.c_str()) != 0) {
        // 已注册但指向旧路径 → 静默重注册覆盖
        doRegisterAssociations(false);
    }
}

void ManagerWindow::doRegisterAssociations(bool notify)
{
    // 注册 QLens QuickView 为"可用看图器"（HKCU 用户级，无需管理员；微软文档化注册表路径 + Explorer 刷新通知）
    // 指向同目录 qlens_quickview.exe（正式发行版两 exe 同目录）
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t selfDir[MAX_PATH];
    wcscpy_s(selfDir, self);
    wchar_t *sl = wcsrchr(selfDir, L'\\');
    if (sl) *sl = 0;
    std::wstring qvExe = std::wstring(selfDir) + L"\\qlens_quickview.exe";
    if (GetFileAttributesW(qvExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        QMessageBox::warning(this, T(L"设置"),
            T(L"未找到 qlens_quickview.exe（需与 Manager 同目录部署）"));
        return;
    }
    const wchar_t *progId = L"QLensQuickView";
    const wchar_t *friendly = L"QLens QuickView";
    std::wstring cmd = L"\"" + qvExe + L"\" \"%1\"";
    std::wstring ico = L"\"" + qvExe + L"\",0";
    // 扩展名 → 独立 ProgID + 格式图标（icons/<ICO>.ico；空图标 = 无专属 → exe 主图标）
    static const struct { const wchar_t *ext; const wchar_t *prog; const wchar_t *ico; } kExtAssoc[] = {
        { L".jpg",  L"QLens.JPG",  L"JPG"  },
        { L".jpeg", L"QLens.JPG",  L"JPG"  },
        { L".png",  L"QLens.PNG",  L"PNG"  },
        { L".gif",  L"QLens.GIF",  L"GIF"  },
        { L".bmp",  L"QLens.BMP",  L"BMP"  },
        { L".webp", L"QLens.WEBP", L"WEBP" },
        { L".heic", L"QLens.HEIF", L"HEIF" },
        { L".heif", L"QLens.HEIF", L"HEIF" },
        { L".avif", L"QLens.AVIF", L"AVIF" },
        { L".jxr",  L"QLensQuickView", L""    },
        { L".wdp",  L"QLensQuickView", L""    },
        { L".svg",  L"QLens.SVG",  L"SVG"  },
        { L".tif",  L"QLens.TIFF", L"TIFF" },
        { L".tiff", L"QLens.TIFF", L"TIFF" },
    };
    HKEY k;

    // 1. ProgID：QLensQuickView（描述 / DefaultIcon / open command / FriendlyAppName）
    std::wstring base = std::wstring(L"Software\\Classes\\") + progId;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"", 0, REG_SZ, (const BYTE*)friendly, (DWORD)(wcslen(friendly) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    std::wstring sub = base + L"\\DefaultIcon";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"", 0, REG_SZ, (const BYTE*)ico.c_str(), (DWORD)(ico.size() * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    sub = base + L"\\shell\\open\\command";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.size() * sizeof(wchar_t)));
        RegCloseKey(k);
    }

    // 1.5 每扩展独立 ProgID：QLens.<EXT>（DefaultIcon 指向 icons/<EXT>.ico——文件类型图标）
    for (const auto &d : kExtAssoc) {
        if (wcscmp(d.prog, progId) == 0) continue;   // 主 ProgID 已建（无专属图标的扩展用它）
        std::wstring pBase = L"Software\\Classes\\" + std::wstring(d.prog);
        std::wstring icoPath;
        if (d.ico[0]) {
            std::wstring icof = std::wstring(selfDir) + L"\\icons\\" + d.ico + L".ico";
            if (GetFileAttributesW(icof.c_str()) != INVALID_FILE_ATTRIBUTES)
                icoPath = L"\"" + icof + L"\",0";
        }
        if (icoPath.empty()) icoPath = ico;   // 无图标文件 → exe 主图标
        sub = pBase + L"\\DefaultIcon";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
            KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, L"", 0, REG_SZ, (const BYTE*)icoPath.c_str(), (DWORD)(icoPath.size() * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        sub = pBase + L"\\shell\\open\\command";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
            KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, L"", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.size() * sizeof(wchar_t)));
            RegCloseKey(k);
        }
    }

    // 2. Applications\qlens_quickview.exe（让"打开方式"正确显示 QLens QuickView + 支持类型 + 图标）
    std::wstring appBase = L"Software\\Classes\\Applications\\qlens_quickview.exe";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, appBase.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"FriendlyAppName", 0, REG_SZ, (const BYTE*)friendly, (DWORD)(wcslen(friendly) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    sub = appBase + L"\\DefaultIcon";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"", 0, REG_SZ, (const BYTE*)ico.c_str(), (DWORD)(ico.size() * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    sub = appBase + L"\\shell\\open\\command";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.size() * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    // SupportedTypes：每个扩展一个命名值（Explorer"始终使用"依赖它）
    sub = appBase + L"\\SupportedTypes";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        for (const auto &d : kExtAssoc)
            RegSetValueExW(k, d.ext, 0, REG_SZ, (const BYTE*)L"", 1);
        RegCloseKey(k);
    }

    // 3. 各扩展 OpenWithProgids → 对应 ProgID（QLens.JPG/QLens.PNG...——"打开方式"候选）
    for (const auto &d : kExtAssoc) {
        std::wstring key = std::wstring(L"Software\\Classes\\") + d.ext + L"\\OpenWithProgids";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0,
            KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, d.prog, 0, REG_SZ, (const BYTE*)L"", 1);
            RegCloseKey(k);
        }
    }

    // 4. Capabilities + RegisteredApplications（Windows 10/11 设置 → 默认应用 识别 QLens 的标准机制）
    //    Capabilities\FileAssociations：扩展名 → 对应 ProgID（QLens.JPG 等——默认应用页按类型设置）
    std::wstring capBase = appBase + L"\\Capabilities\\FileAssociations";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, capBase.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        for (const auto &d : kExtAssoc)
            RegSetValueExW(k, d.ext, 0, REG_SZ, (const BYTE*)d.prog, (DWORD)(wcslen(d.prog) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    //    RegisteredApplications：应用名 → Capabilities 键路径（默认应用页入口）
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, nullptr, 0,
        KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        std::wstring capPath = L"Software\\Classes\\Applications\\qlens_quickview.exe\\Capabilities";
        RegSetValueExW(k, friendly, 0, REG_SZ, (const BYTE*)capPath.c_str(), (DWORD)(capPath.size() * sizeof(wchar_t)));
        RegCloseKey(k);
    }

    // 5. 通知资源管理器刷新关联（标准接口）
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_FLUSH | SHCNF_IDLIST, nullptr, nullptr);

    if (notify) {
        QMessageBox::information(this, T(L"设置"),
            T(L"QLens QuickView 已注册为可用看图器。\n右键图片 → 打开方式 → QLens QuickView 即可。\n"
              "Windows 10/11：设置 → 应用 → 默认应用 → 搜索 QLens QuickView 可设为默认。"));
    }
}
