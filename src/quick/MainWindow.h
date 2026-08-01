#pragma once
#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QString>
#include <QDir>
#include <QStringList>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QHBoxLayout>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFile(const QString &filePath);

protected:
    void keyPressEvent(QKeyEvent *event)      override;
    void wheelEvent(QWheelEvent *event)       override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event)         override;
    void resizeEvent(QResizeEvent *event)     override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event)   override;

private:
    void loadImage(const QString &filePath);
    void decodeAsync(const QString &filePath, int idx);
    void navigate(int direction);
    void updateZoom();
    void updateTitle();
    void showUI(bool visible);
    void launchManager();
    void buildThumbnails();
    void scrollThumbToCenter();
    void thumbUpdateByName(const QString &name, const QPixmap &icon);
    void thumbAddBtn(int idx, const QPixmap &pix);

    QGraphicsView  *m_view   = nullptr;
    QGraphicsScene *m_scene  = nullptr;
    QPixmap         m_original;
    double          m_zoomFactor   = 0.0;
    QString         m_currentFile;
    QDir            m_currentDir;
    QStringList     m_siblings;
    int             m_currentIndex = -1;
    QPushButton    *m_btnPrev = nullptr, *m_btnNext = nullptr, *m_btnManager = nullptr;
    QLabel         *m_lblFile = nullptr;
    QTimer         *m_hideTimer = nullptr;
    QScrollArea    *m_thumbStrip = nullptr;
    QWidget        *m_thumbInner = nullptr;
    QHBoxLayout    *m_thumbLayout = nullptr;
    int             m_thumbSize = 64;
    int             m_thumbCount = 0;
};
