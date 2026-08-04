#include "TagPanel.h"
#include "i18n.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QCompleter>
#include <QStringListModel>
#include <QKeyEvent>
#include <QMenu>
#include <QColorDialog>
#include <QAction>

// 翻译辅助：msgid=中文，默认中文；.po 覆盖为目标语言
static QString T(const wchar_t *id) { return QString::fromWCharArray(I18n::Get(id)); }
#include <QMenu>
#include <QAction>
#include <QSplitter>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QTimer>
#include <QSet>

TagPanel::TagPanel(TagStore *store, QWidget *parent)
    : QWidget(parent), m_store(store), m_refreshPending(false) {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    // 可拖动分割：上=已分配标签（2/3），下=输入栏（1/3，默认）
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName("tagPanelSplitter");   // 布局持久化定位
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
        QAction *colorAct = menu.addAction(T(L"设置颜色..."));
        QAction *sel2 = menu.exec(m_assignedTags->mapToGlobal(pos));
        if (sel2 == remove) {
            m_store->removeImageTag(QFileInfo(m_currentImage).fileName(), item->text());
            refresh();
        } else if (sel2 == colorAct) {
            QColor c = QColorDialog::getColor(
                QColor(m_store->tagColor(item->text())), this, T(L"标签颜色"));
            if (c.isValid()) {
                m_store->setTagColor(item->text(), c.name());
                refresh();
            }
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

    m_tagInput = new QPlainTextEdit(lower);
    m_tagInput->setPlaceholderText(T(L"输入标签（用 , 分隔）+ 回车..."));
    m_tagInput->setStyleSheet("background:#222; color:#ccc; border:1px solid #333; padding:4px;");
    m_tagInput->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);  // 占满剩余高度
    ll->addWidget(m_tagInput, 1);
    m_tagInput->installEventFilter(this);

    // 补全候选（QPlainTextEdit 无 setCompleter——Tab 补全手动）
    m_completer = new QCompleter(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    connect(m_tagInput, &QPlainTextEdit::textChanged, [this]() {
        QString text = m_tagInput->toPlainText();
        QString prefix = text.mid(text.lastIndexOf(',') + 1).trimmed();
        m_suggestions = m_store->searchTags(prefix);
        if (m_suggestions.isEmpty() && prefix.isEmpty())
            m_suggestions = m_store->allTagNames();
    });

    splitter->addWidget(upper);
    splitter->addWidget(lower);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({200, 100});  // 输入栏默认占 1/3
    l->addWidget(splitter, 1);

    setMinimumWidth(200);
}

// 回车提交（Ctrl+Enter 换行）、Tab 补全（插入第一个候选）
bool TagPanel::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_tagInput && ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (ke->modifiers() & Qt::ControlModifier)
                return QWidget::eventFilter(obj, ev);  // Ctrl+Enter 换行
            addTagFromInput();
            return true;
        }
        if (ke->key() == Qt::Key_Tab && !m_suggestions.isEmpty()) {
            QString text = m_tagInput->toPlainText();
            int comma = text.lastIndexOf(',');
            QString prefix = text.mid(comma + 1).trimmed();
            QString cand = m_suggestions.first();
            if (cand.compare(prefix, Qt::CaseInsensitive) != 0) {
                m_tagInput->setPlainText(text.left(comma + 1) + " " + cand + ", ");
                m_tagInput->moveCursor(QTextCursor::End);
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void TagPanel::setCurrentImage(const QString &imagePath) {
    m_currentImage = imagePath;
    m_tagInput->setEnabled(true);
    m_tagInput->setPlaceholderText(T(L"输入标签（用 , 分隔）+ 回车..."));
    scheduleRefresh();   // 异步刷新：不阻塞单击/双击事件流
}

// 防抖异步刷新：快速连点合并成一次；事件循环先处理鼠标第二击（双击）再刷新
void TagPanel::scheduleRefresh() {
    if (m_refreshPending) return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_refreshPending = false;
        refresh();
    });
}

// 多选状态：右侧显示所有选中图的非重复标签（并集），禁用单图打标输入
void TagPanel::showMultiSelection(const QStringList &paths) {
    m_currentImage.clear();
    m_assignedTags->clear();
    QSet<QString> all;
    for (const QString &p : paths) {
        if (p.isEmpty()) continue;
        const QString fn = QFileInfo(p).fileName();
        const QStringList t = m_store->tagsForImage(fn);
        for (const QString &x : t) all.insert(x);
    }
    // 标签列表（按名排序，稳定显示）
    QStringList sorted = all.values();
    sorted.sort();
    for (const QString &t : sorted) {
        auto *item = new QListWidgetItem(t);
        QString col = m_store->tagColor(t);
        QPixmap sw(14, 14);
        sw.fill(QColor(col.isEmpty() ? QStringLiteral("#444") : col));
        item->setIcon(QIcon(sw));
        m_assignedTags->addItem(item);
    }
    m_tagInput->setEnabled(false);
    m_tagInput->setPlaceholderText(T(L"多选 %1 张——右键批量添加/移除标签").arg(paths.size()));
}

void TagPanel::addTagFromInput() {
    if (m_currentImage.isEmpty()) return;
    // 逗号分隔批量添加，自动去前后空格
    QStringList parts = m_tagInput->toPlainText().split(',', Qt::SkipEmptyParts);
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
    for (const QString &t : tags) {
        auto *item = new QListWidgetItem(t);
        // 色点：tag 有 color 字段则显示色块图标（未设置 = 灰）
        QString col = m_store->tagColor(t);
        QPixmap sw(14, 14);
        sw.fill(QColor(col.isEmpty() ? QStringLiteral("#444") : col));
        item->setIcon(QIcon(sw));
        m_assignedTags->addItem(item);
    }
    emit tagsChanged(m_currentImage, tags);
}
