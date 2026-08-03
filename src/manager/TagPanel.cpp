#include "TagPanel.h"
#include "i18n.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QCompleter>
#include <QStringListModel>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>

// 翻译辅助：msgid=中文，默认中文；.po 覆盖为目标语言
static QString T(const wchar_t *id) { return QString::fromWCharArray(I18n::Get(id)); }
#include <QContextMenuEvent>
#include <QFileInfo>

TagPanel::TagPanel(TagStore *store, QWidget *parent)
    : QWidget(parent), m_store(store) {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    auto *assignedLabel = new QLabel(T(L"选中图片的标签"), this);
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
        QAction *remove = menu.addAction(T(L"移除标签"));
        if (menu.exec(m_assignedTags->mapToGlobal(pos)) == remove) {
            m_store->removeImageTag(QFileInfo(m_currentImage).fileName(), item->text());
            refresh();
        }
    });
    l->addWidget(m_assignedTags, 2);

    auto *addLabel = new QLabel(T(L"添加标签"), this);
    addLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
    l->addWidget(addLabel);

    m_tagInput = new QLineEdit(this);
    m_tagInput->setPlaceholderText(T(L"输入标签（用 , 分隔）+ 回车..."));
    m_tagInput->setStyleSheet("background:#222; color:#ccc; border:1px solid #333; padding:4px;");
    m_tagInput->setMinimumHeight(60);  // 扩大输入栏（视觉占高约 1/3）
    l->addWidget(m_tagInput, 1);
    m_tagInput->setCompleter(m_completer);

    connect(m_tagInput, &QLineEdit::returnPressed, this, &TagPanel::addTagFromInput);
    // 输入时动态更新补全列表（取最后一个逗号后的片段做前缀）
    connect(m_tagInput, &QLineEdit::textEdited, [this](const QString &text) {
        QString prefix = text.mid(text.lastIndexOf(',') + 1).trimmed();
        QStringList suggestions = m_store->searchTags(prefix);
        if (suggestions.isEmpty() && prefix.isEmpty())
            suggestions = m_store->allTagNames();
        m_completer->setModel(new QStringListModel(suggestions, m_completer));
        m_completer->setCompletionPrefix(prefix);
    });

    setMinimumWidth(200);
}

void TagPanel::setCurrentImage(const QString &imagePath) {
    m_currentImage = imagePath;
    refresh();
}

void TagPanel::addTagFromInput() {
    if (m_currentImage.isEmpty()) return;
    // 逗号分隔批量添加，自动去前后空格
    QStringList parts = m_tagInput->text().split(',', Qt::SkipEmptyParts);
    QString fname = QFileInfo(m_currentImage).fileName();
    bool any = false;
    for (QString &t : parts) {
        t = t.trimmed();
        if (t.isEmpty()) continue;
        m_store->addImageTag(fname, t);
        any = true;
    }
    if (any) {
        m_tagInput->clear();
        refresh();
    }
}

void TagPanel::refresh() {
    m_assignedTags->clear();
    if (m_currentImage.isEmpty()) return;
    QStringList tags = m_store->tagsForImage(QFileInfo(m_currentImage).fileName());
    for (const QString &t : tags)
        m_assignedTags->addItem(t);
    emit tagsChanged(m_currentImage, tags);
}
