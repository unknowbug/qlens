#pragma once
#include <QMainWindow>
#include <QDockWidget>
#include <QStackedWidget>
#include <QKeyEvent>
#include <QComboBox>
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
    void backToGrid();
    void refreshToolbar(const QString &path);

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
