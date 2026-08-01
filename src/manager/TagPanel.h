#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include "TagStore.h"

// 标签操作面板 —— 当前图片标签列表 + 前缀补全输入
// 通过信号与 Manager 通信

class TagPanel : public QWidget {
    Q_OBJECT
public:
    explicit TagPanel(TagStore *store, QWidget *parent = nullptr);

    // 切换当前图片，加载其标签
    void setCurrentImage(const QString &imagePath);

signals:
    void tagsChanged(const QString &imagePath, const QStringList &tags);

private:
    void addTagFromInput();
    void refresh();

    TagStore    *m_store = nullptr;
    QString      m_currentImage;
    QListWidget *m_assignedTags = nullptr;
    QLineEdit   *m_tagInput     = nullptr;
    QCompleter  *m_completer    = nullptr;
};
