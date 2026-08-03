#pragma once
#include <QWidget>
#include <QStringList>
#include <QLineEdit>

// Windows 资源管理器风格面包屑路径栏：
// - 路径按 \ 分段显示为按钮，点击跳转到该段
// - 点击 \ 分隔符 → 下拉该段目录下的子文件夹
// - 点击空白区域 → 切换输入框（可输入路径，Enter 跳转，Esc/失焦切回）
class PathBar : public QWidget {
    Q_OBJECT
public:
    explicit PathBar(QWidget *parent = nullptr);
    void setPath(const QString &path);
    QString path() const { return m_path; }

signals:
    void pathActivated(const QString &path);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    struct Segment { QString text; QString path; QRect rect; };
    struct Sep { QRect rect; QString parentPath; };
    void rebuildSegments();
    void enterEditMode();
    void leaveEditMode();
    void applyEdit();

    QString m_path;
    QList<Segment> m_segments;
    QList<Sep>     m_seps;
    int m_hoverSeg = -1;
    int m_hoverSep = -1;
    QLineEdit *m_edit = nullptr;
    bool m_editMode = false;
};
