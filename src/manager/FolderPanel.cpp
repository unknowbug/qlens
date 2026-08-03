#include "FolderPanel.h"
#include <QVBoxLayout>
#include <QDir>

FolderPanel::FolderPanel(QWidget *parent) : QWidget(parent) {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    m_folderTree = new QTreeView(this);
    m_fsModel = new QFileSystemModel(this);
    m_folderTree->setStyleSheet(
        "QTreeView{background:#111; color:#ddd; border:none;}"
        "QTreeView::item:selected{background:#335; color:#fff;}");
    // 根 = 所有驱动器（Windows: C:、D:...），任意路径都能在树中导航
    m_fsModel->setRootPath("");
    m_fsModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    m_folderTree->setModel(m_fsModel);
    m_folderTree->setHeaderHidden(true);
    for (int i = 1; i < m_fsModel->columnCount(); ++i)
        m_folderTree->hideColumn(i);
    l->addWidget(m_folderTree);

    setMinimumWidth(180);

    // 初始定位到 Pictures（存在时）
    QString pics = QDir::homePath() + "/Pictures";
    if (QDir(pics).exists()) {
        QModelIndex idx = m_fsModel->index(pics);
        if (idx.isValid()) {
            m_folderTree->setCurrentIndex(idx);
            m_folderTree->scrollTo(idx);
        }
    }

    connect(m_folderTree->selectionModel(), &QItemSelectionModel::currentChanged,
            [this](const QModelIndex &idx) {
        if (m_syncing) return;  // 程序同步时忽略
        emit folderSelected(m_fsModel->filePath(idx));
    });
}

void FolderPanel::setCurrentPath(const QString &path) {
    QModelIndex idx = m_fsModel->index(path);
    if (!idx.isValid()) return;
    m_syncing = true;
    // QFileSystemModel 异步懒加载：逐级展开祖先节点，深层路径才能显示
    QModelIndex parent = idx.parent();
    while (parent.isValid()) {
        m_folderTree->expand(parent);
        parent = parent.parent();
    }
    m_folderTree->setCurrentIndex(idx);
    m_folderTree->scrollTo(idx);
    m_syncing = false;
}
