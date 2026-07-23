#pragma once
#include <QMainWindow>
#include <QDockWidget>
#include "ThumbnailGrid.h"
#include "FolderPanel.h"
#include "TagPanel.h"
#include "ViewerWidget.h"

class ManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ManagerWindow(QWidget *parent = nullptr);

private:
    void openInViewer(const QString &path);

    ThumbnailGrid *m_grid        = nullptr;
    FolderPanel   *m_folderPanel = nullptr;
    ViewerWidget  *m_viewer      = nullptr;
};
