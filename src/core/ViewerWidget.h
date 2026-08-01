#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include <QDir>
#include <QStringList>
#include <QKeyEvent>
#include <QWheelEvent>

// 图片查看器核心控件 —— QGraphicsView + Scene + 缩放/翻页
// QuickView 和 Manager 共用

class Q_DECL_EXPORT ViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ViewerWidget(QWidget *parent = nullptr);

    void openFile(const QString &filePath);
    int  currentIndex() const { return m_currentIndex; }
    int  siblingCount() const { return (int)m_siblings.size(); }
    QString currentFile() const { return m_currentFile; }

signals:
    void imageChanged(const QString &filePath, int index);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void loadImage(const QString &filePath);
    void decodeAsync(const QString &filePath, int idx);
    void navigate(int direction);
    void updateZoom();

    QGraphicsView       *m_view   = nullptr;
    QGraphicsScene      *m_scene  = nullptr;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;
    QPixmap              m_original;
    double               m_zoomFactor  = 0.0;
    QString              m_currentFile;
    QDir                 m_currentDir;
    QStringList          m_siblings;
    int                  m_currentIndex = -1;
};
