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
#include <QMenu>
#include <QAction>
#include <QSplitter>
#include <QContextMenuEvent>
#include <QFileInfo>

TagPanel::TagPanel(TagStore *store, QWidget *parent)
    : QWidget(parent), m_store(store) {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    // 可拖动分割：上=已分配标签（2/3），下=输入栏（1/3，默认）
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);

    // ── 上半：已分配标签 ──
    auto *upper = new QWidget(splitter);
    auto *ul = new QVBoxLayout(upper);
    ul->setContentsMargins(0, 0, 0, 0);
    ul->setSpacing(0);
    auto *assignedLabel = new QLabel(T(L"选中图片的标签"), upper);
    assignedLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
    ul->addWidget(assignedLabel);

    m_assignedTags = new QListWidget(upper);
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
    ul->addWidget(m_assignedTags, 1);

    // ── 下半：输入栏（占 1/3，可拖动调整）──
    auto *lower = new QWidget(splitter);
    auto *ll = new QVBoxLayout(lower);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
    auto *addLabel = new QLabel(T(L"添加标签"), lower);
    addLabel->setStyleSheet("color:#888; font-size:11px; padding:4px;");
    addLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);  // 标题不拉伸
    ll->addWidget(addLabel);

    m_tagInput = new QLineEdit(lower);
    m_tagInput->setPlaceholderText(T(L"输入标签（用 , 分隔）+ 回车..."));
    m_tagInput->setStyleSheet("background:#222; color:#ccc; border:1px solid #333; padding:4px;");
    m_tagInput->setAlignment(Qt::AlignLeft);  // 提示文字左上角
    m_tagInput->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);  // 占满剩余高度
    ll->addWidget(m_tagInput, 1);

    splitter->addWidget(upper);
    splitter->addWidget(lower);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({200, 100});  // 输入栏默认占 1/3
    l->addWidget(splitter, 1);
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
