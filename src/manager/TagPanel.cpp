#include "TagPanel.h"
#include <QVBoxLayout>
#include <QLabel>

TagPanel::TagPanel(QWidget *parent) : QWidget(parent) {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    auto *assignedLabel = new QLabel(tr("Selected Image Tags"), this);
    assignedLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
    l->addWidget(assignedLabel);

    m_assignedTags = new QListWidget(this);
    m_assignedTags->setStyleSheet("background:#1a1a1a; color:#ccc; border:1px solid #333;");
    l->addWidget(m_assignedTags, 2);

    auto *addLabel = new QLabel(tr("Add Tag"), this);
    addLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
    l->addWidget(addLabel);

    m_tagInput = new QLineEdit(this);
    m_tagInput->setPlaceholderText(tr("Type tag + Enter..."));
    m_tagInput->setStyleSheet("background:#222; color:#ccc; border:1px solid #333; padding:4px;");
    l->addWidget(m_tagInput);

    setMinimumWidth(200);
}
