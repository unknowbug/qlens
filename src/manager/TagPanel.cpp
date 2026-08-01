#include "TagPanel.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QCompleter>
#include <QStringListModel>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QFileInfo>

TagPanel::TagPanel(TagStore *store, QWidget *parent)
    : QWidget(parent), m_store(store) {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    auto *assignedLabel = new QLabel(tr("Selected Image Tags"), this);
    assignedLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
    l->addWidget(assignedLabel);

    m_assignedTags = new QListWidget(this);
    m_assignedTags->setStyleSheet("background:#1a1a1a; color:#ccc; border:1px solid #333;");
    m_assignedTags->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_assignedTags, &QListWidget::customContextMenuRequested,
            [this](const QPoint &pos) {
        auto *item = m_assignedTags->itemAt(pos);
        if (!item) return;
        QMenu menu(this);
        QAction *remove = menu.addAction(tr("Remove Tag"));
        if (menu.exec(m_assignedTags->mapToGlobal(pos)) == remove) {
            m_store->removeImageTag(QFileInfo(m_currentImage).fileName(), item->text());
            refresh();
        }
    });
    l->addWidget(m_assignedTags, 2);

    auto *addLabel = new QLabel(tr("Add Tag"), this);
    addLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
    l->addWidget(addLabel);

    m_tagInput = new QLineEdit(this);
    m_tagInput->setPlaceholderText(tr("Type tag + Enter..."));
    m_tagInput->setStyleSheet("background:#222; color:#ccc; border:1px solid #333; padding:4px;");
    l->addWidget(m_tagInput);

    // 前缀补全
    m_completer = new QCompleter(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setMaxVisibleItems(12);
    m_tagInput->setCompleter(m_completer);

    connect(m_tagInput, &QLineEdit::returnPressed, this, &TagPanel::addTagFromInput);
    // 输入时动态更新补全列表
    connect(m_tagInput, &QLineEdit::textEdited, [this](const QString &text) {
        QStringList suggestions = m_store->searchTags(text);
        // 附加所有已存在标签供浏览
        if (suggestions.isEmpty() && text.trimmed().isEmpty())
            suggestions = m_store->allTagNames();
        m_completer->setModel(new QStringListModel(suggestions, m_completer));
        m_completer->setCompletionPrefix(text);
    });

    setMinimumWidth(200);
}

void TagPanel::setCurrentImage(const QString &imagePath) {
    m_currentImage = imagePath;
    refresh();
}

void TagPanel::addTagFromInput() {
    QString tag = m_tagInput->text().trimmed();
    if (tag.isEmpty() || m_currentImage.isEmpty()) return;
    m_store->addImageTag(QFileInfo(m_currentImage).fileName(), tag);
    m_tagInput->clear();
    refresh();
}

void TagPanel::refresh() {
    m_assignedTags->clear();
    if (m_currentImage.isEmpty()) return;
    QStringList tags = m_store->tagsForImage(QFileInfo(m_currentImage).fileName());
    for (const QString &t : tags)
        m_assignedTags->addItem(t);
    emit tagsChanged(m_currentImage, tags);
}
