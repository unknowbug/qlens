#pragma once
#include <QMainWindow>
#include <QDockWidget>
#include <QStackedWidget>
#include <QKeyEvent>
#include <QComboBox>
#include <QPushButton>
#include <QStringList>
#include <QLabel>
#include <QTimer>
#include <QByteArray>
class QSplitter;
#include "ThumbnailGrid.h"
#include "FolderPanel.h"
#include "TagPanel.h"
#include "TagStore.h"
#include "ViewerWidget.h"
#include "PathBar.h"

class ManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ManagerWindow(QWidget *parent = nullptr);
    void openFolder(const QString &path);   // 公开：命令行参数/语言切换重启恢复路径
    bool restoreLayout();                   // 启动恢复布局；返回是否有保存的布局

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    // 界面布局持久化（dock/几何 → qlens_config.ini [Layout]；动了界面就自动保存）
    void restoreDockState(const QString &stB64);  // show 后：恢复 dock 布局（内部面板高度不持久化）
    void connectSplitters();       // 连接所有 QSplitter（dock 分隔条/内部栏），拖动触发防抖保存
    void saveLayout();             // 立即保存（状态有变化才写盘）
    void scheduleLayoutSave();     // 防抖：600ms 后保存（拖动/移动过程不频繁写盘）
    QTimer     *m_layoutTimer = nullptr;
    QByteArray  m_lastLayoutState;
    void openInViewer(const QString &path);
    void backToGrid();
    void refreshToolbar(const QString &path);
    // 状态栏
    void updateGridStatus();
    void updateViewerStatus(const QString &path);
    // Settings
    void setLanguage(const QString &lang);
    void registerFileAssociations();
    void checkAssociationsOnStart();   // 启动检测：程序移动后自动静默重注册（防右键菜单无效关联）
    void doRegisterAssociations(bool notify);   // 实际注册（notify=false 静默——启动检测用）
    // MCP 帮助
    void showMcpHelp();

    // 文件管理导航
    void goUp();       // 上级目录
    void goBack();     // 历史后退
    void goForward();  // 历史前进
    void pushHistory(const QString &path);  // 记录浏览历史
    void updateNavButtons();               // 更新按钮可用状态

    // QC 批量检测（本地 CV：曝光过度/模糊/色偏 → 固定标）
    void runQcDetection();
    bool m_qcRunning = false;

    QPushButton *m_backBtn = nullptr;
    QPushButton *m_forwardBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QStringList  m_history;
    int          m_historyPos = -1;
    // 状态栏左侧：缩略图大小 / 查看器缩放 滑块（view 态挂钩图片缩放）
    QSlider *m_sizeSlider = nullptr;
    QLabel  *m_sizeLabel  = nullptr;

    TagStore      *m_store   = nullptr;
    QStackedWidget *m_stack  = nullptr;
    QDockWidget *m_leftDock  = nullptr;   // 左侧浏览 dock（restoreState 失败时重建默认布局用）
    QDockWidget *m_rightDock = nullptr;   // 右侧标签 dock
    ThumbnailGrid *m_grid    = nullptr;
    FolderPanel   *m_folderPanel = nullptr;
    TagPanel      *m_tagPanel = nullptr;
    ViewerWidget  *m_viewer  = nullptr;
    QLabel        *m_viewTitle = nullptr;
    PathBar       *m_pathBar   = nullptr;
    QLabel        *m_statusLabel = nullptr;
    QComboBox     *m_filterCombo = nullptr;
    QComboBox     *m_qcCombo = nullptr;      // 固定标(QC)筛选
    QComboBox     *m_highlightCombo = nullptr;
    bool           m_updatingCombo = false;
};
