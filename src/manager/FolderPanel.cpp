#include "FolderPanel.h"
#include <QVBoxLayout>
#include <QLabel>
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
    l->addWidget(m_folderTree, 3);

    auto *tagLabel = new QLabel(tr("Tag Filter"), this);
    tagLabel->setStyleSheet("color:#888; font-size:11px; padding:4px 4px 0 4px;");
    l->addWidget(tagLabel);

    m_tagSearch = new QLineEdit(this);
    m_tagSearch->setPlaceholderText(tr("Search tags..."));
    m_tagSearch->setStyleSheet("background:#222; color:#ccc; border:1px solid #333; padding:4px;");
    l->addWidget(m_tagSearch);

    m_tagSuggestions = new QListWidget(this);
    m_tagSuggestions->setMaximumHeight(120);
    m_tagSuggestions->setStyleSheet("background:#1a1a1a; color:#888; border:1px solid #333;");
    l->addWidget(m_tagSuggestions);

    setMinimumWidth(200);

    connect(m_folderTree->selectionModel(), &QItemSelectionModel::currentChanged,
            [this](const QModelIndex &idx) {
        emit folderSelected(m_fsModel->filePath(idx));
    });
}
