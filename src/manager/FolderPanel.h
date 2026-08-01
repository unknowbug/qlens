#pragma once
#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>

// 文件夹导航面板 —— 只留文件夹树
// 标签搜索已移到网格顶部工具条

class FolderPanel : public QWidget {
    Q_OBJECT
public:
    explicit FolderPanel(QWidget *parent = nullptr);

    // 同步树到指定路径（展开 + 高亮），不触发 folderSelected 信号循环
    void setCurrentPath(const QString &path);

signals:
    void folderSelected(const QString &path);

private:
    QTreeView        *m_folderTree = nullptr;
    QFileSystemModel *m_fsModel    = nullptr;
    bool              m_syncing = false;
};
