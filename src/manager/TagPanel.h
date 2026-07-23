#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>

// 标签操作面板 —— 已打标签列表 + 标签输入
// ponytail: 目前纯占位，等标签引擎到位后再实装

class TagPanel : public QWidget {
    Q_OBJECT
public:
    explicit TagPanel(QWidget *parent = nullptr);

private:
    QListWidget *m_assignedTags = nullptr;
    QLineEdit   *m_tagInput     = nullptr;
};
