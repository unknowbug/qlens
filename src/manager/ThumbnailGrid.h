#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QStringList>
#include <QPushButton>

// 缩略图网格 —— 显示文件夹 + 图片缩略图，支持 Ctrl+滚轮缩放
// 与 Manager 其他模块通过信号通信，不直接依赖外部

class ThumbnailGrid : public QScrollArea {
    Q_OBJECT
public:
    explicit ThumbnailGrid(QWidget *parent = nullptr);

    void loadFolder(const QString &path);

signals:
    void folderDoubleClicked(const QString &path);
    void imageDoubleClicked(const QString &path);

protected:
    void wheelEvent(QWheelEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QWidget     *m_gridWidget = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    int          m_thumbSize  = 200;
    int          m_thumbCols  = 4;
    QStringList  m_imageFiles;
    QStringList  m_subDirs;
    QString      m_currentFolder;
};
