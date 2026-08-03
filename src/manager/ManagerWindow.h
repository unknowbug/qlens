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

class ManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ManagerWindow(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void openInViewer(const QString &path);
    void openFolder(const QString &path);
    void backToGrid();
    void refreshToolbar(const QString &path);
    // Settings
    void setLanguage(const QString &lang);
    void registerFileAssociations();
    // MCP 帮助
    void showMcpHelp();

    // 文件管理导航
    void goUp();       // 上级目录
    void goBack();     // 历史后退
    void goForward();  // 历史前进
    void pushHistory(const QString &path);  // 记录浏览历史
    void updateNavButtons();               // 更新按钮可用状态

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
    QLabel        *m_pathLabel = nullptr;
    QComboBox     *m_filterCombo = nullptr;
    QComboBox     *m_highlightCombo = nullptr;
    bool           m_updatingCombo = false;
};
