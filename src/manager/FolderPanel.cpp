#include "FolderPanel.h"
#include <QVBoxLayout>
#include <QDir>

FolderPanel::FolderPanel(QWidget *parent) : QWidget(parent) {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    m_folderTree = new QTreeView(this);
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setRootPath(QDir::homePath());
    m_fsModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    m_folderTree->setModel(m_fsModel);
    QString pics = QDir::homePath() + "/Pictures";
    if (!QDir(pics).exists()) pics = QDir::homePath();
    m_folderTree->setRootIndex(m_fsModel->index(pics));
    m_folderTree->setCurrentIndex(m_fsModel->index(pics));
    m_folderTree->setHeaderHidden(true);
    for (int i = 1; i < m_fsModel->columnCount(); ++i)
        m_folderTree->hideColumn(i);
    l->addWidget(m_folderTree);

    setMinimumWidth(180);

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
    m_folderTree->setCurrentIndex(idx);
    m_folderTree->scrollTo(idx);
    m_syncing = false;
}
