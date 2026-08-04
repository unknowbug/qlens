#pragma once
#include <QWidget>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QCompleter>
#include "TagStore.h"

// 标签操作面板 —— 当前图片标签列表 + 前缀补全输入
// 通过信号与 Manager 通信

class TagPanel : public QWidget {
    Q_OBJECT
public:
    explicit TagPanel(TagStore *store, QWidget *parent = nullptr);

    // 切换当前图片，加载其标签
    void setCurrentImage(const QString &imagePath);
    // 多选：右侧显示所有选中图的非重复标签（并集），禁用打标输入
    void showMultiSelection(const QStringList &paths);

signals:
    void tagsChanged(const QString &imagePath, const QStringList &tags);

private:
    void addTagFromInput();
    void refresh();
    void scheduleRefresh();   // 防抖异步刷新（不阻塞单击/双击事件流）
    bool eventFilter(QObject *obj, QEvent *ev) override;

    TagStore        *m_store = nullptr;
    QString          m_currentImage;
    QListWidget     *m_assignedTags = nullptr;
    QPlainTextEdit  *m_tagInput     = nullptr;
    QCompleter      *m_completer    = nullptr;
    QStringList      m_suggestions;   // 当前补全候选（Tab 补全用）
    bool             m_refreshPending = false;
};
