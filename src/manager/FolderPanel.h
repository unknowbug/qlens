#pragma once
#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QListWidget>

// 文件夹导航面板 —— 文件夹树 + 标签搜索
// 通过信号与 Manager 通信，不直接依赖其他模块

class FolderPanel : public QWidget {
    Q_OBJECT
public:
    explicit FolderPanel(QWidget *parent = nullptr);

signals:
    void folderSelected(const QString &path);

private:
    QTreeView        *m_folderTree = nullptr;
    QFileSystemModel *m_fsModel    = nullptr;
    QLineEdit        *m_tagSearch  = nullptr;
    QListWidget      *m_tagSuggestions = nullptr;
};
