#pragma once
#include <QMainWindow>
#include <QDockWidget>
#include <QStackedWidget>
#include <QKeyEvent>
#include <QComboBox>
#include <QPushButton>
#include <QStringList>
#include <QLabel>
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

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
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

    TagStore      *m_store   = nullptr;
    QStackedWidget *m_stack  = nullptr;
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
