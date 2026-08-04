#include "PathBar.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QFontMetrics>
#include <QDebug>

// 分隔符点击宽度（\ 按钮）——紧凑
static const int kSepW = 10;
static const int kHMargin = 4;
static const int kVMargin = 4;

PathBar::PathBar(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(28);
    setMouseTracking(true);  // 悬停跟踪（无需按下）
}

void PathBar::setPath(const QString &path)
{
    m_path = path;
    rebuildSegments();
    update();
}

// 路径按 / 分段：E:/PYTHON/qlens → [E:/, PYTHON, qlens]
void PathBar::rebuildSegments()
{
    m_segments.clear();
    m_seps.clear();
    QString p = QDir::fromNativeSeparators(m_path);
    QStringList parts = p.split('/', Qt::SkipEmptyParts);
    QString running;
    for (int i = 0; i < parts.size(); ++i) {
        const QString &part = parts[i];
        if (i == 0 && part.endsWith(':')) {
            running = part + "/";                       // 盘符 E:（显示不带 /）
            m_segments.append({part, running});
        } else {
            running += part + "/";
            m_segments.append({part, running});
        }
    }
    m_hoverSeg = -1;
    m_hoverSep = -1;
    if (m_editMode) leaveEditMode();
}

void PathBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    p.setFont(f);
    QFontMetrics fm(f);

    int x = kHMargin;
    int y = kVMargin;
    int h = std::max(height() - kVMargin * 2, 18);

    m_seps.clear();  // 每次绘制重建分隔符区域（与当前布局一致）
    for (int i = 0; i < m_segments.size(); ++i) {
        int w = fm.horizontalAdvance(m_segments[i].text) + 6;
        QRect r(x, y, w, h);
        m_segments[i].rect = r;

        // 段按钮（悬停高亮）
        if (i == m_hoverSeg) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x3a, 0x5a, 0x9a, 0x99));
            p.drawRoundedRect(r.adjusted(0, 1, 0, -1), 4, 4);
        }
        p.setPen(QColor(0xdd, 0xdd, 0xdd));
        p.drawText(r, Qt::AlignCenter, m_segments[i].text);
        x += w;

        // \ 分隔符（点击 → 子文件夹下拉）
        QRect sr(x, y, kSepW, h);
        m_seps.append({sr, m_segments[i].path});
        if (i == m_hoverSep) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x55, 0x55, 0x55, 0x88));
            p.drawRoundedRect(sr.adjusted(0, 1, 0, -1), 4, 4);
        }
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(sr, Qt::AlignCenter, "\\");
        x += kSepW;
    }
}

void PathBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_editMode) return;
    int hSeg = -1, hSep = -1;
    for (int i = 0; i < m_segments.size(); ++i)
        if (m_segments[i].rect.contains(e->pos())) { hSeg = i; break; }
    for (int i = 0; i < m_seps.size(); ++i)
        if (m_seps[i].rect.contains(e->pos())) { hSep = i; break; }
    if (hSeg != m_hoverSeg || hSep != m_hoverSep) {
        m_hoverSeg = hSeg;
        m_hoverSep = hSep;
        update();
    }
}

void PathBar::leaveEvent(QEvent *)
{
    m_hoverSeg = -1;
    m_hoverSep = -1;
    update();
}

void PathBar::mousePressEvent(QMouseEvent *e)
{
    if (m_editMode) return;
    QPoint pos = e->pos();

    // 分隔符 → 子文件夹下拉
    for (int i = 0; i < m_seps.size(); ++i) {
        if (m_seps[i].rect.contains(pos)) {
            QMenu menu(this);
            menu.setStyleSheet(
                "QMenu{background:#222; color:#eee; border:1px solid #444;}"
                "QMenu::item{padding:4px 18px;}"
                "QMenu::item:selected{background:#335; color:#fff;}");
            QDir d(m_seps[i].parentPath);
            QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            if (subs.isEmpty()) { menu.addAction(QStringLiteral("…")); }
            else for (const QString &name : subs)
                menu.addAction(name);
            QAction *sel = menu.exec(e->globalPos());
            if (sel && !sel->text().startsWith("…"))
                emit pathActivated(QDir(m_seps[i].parentPath).filePath(sel->text()));
            return;
        }
    }

    // 段 → 跳转
    for (int i = 0; i < m_segments.size(); ++i) {
        if (m_segments[i].rect.contains(pos)) {
            emit pathActivated(m_segments[i].path);
            return;
        }
    }

    // 空白 → 编辑模式
    enterEditMode();
}

void PathBar::enterEditMode()
{
    if (m_editMode) return;
    m_editMode = true;
    if (!m_edit) {
        m_edit = new QLineEdit(this);
        m_edit->setStyleSheet("QLineEdit{background:#1c1c1c; color:#eee; border:1px solid #3a5a9a; selection-background-color:#3a5a9a;}");
        connect(m_edit, &QLineEdit::returnPressed, this, &PathBar::applyEdit);
    }
    m_edit->setText(QDir::toNativeSeparators(m_path));  // Windows 标准 \ 分隔符
    m_edit->setGeometry(0, 0, width(), height());
    m_edit->show();
    m_edit->setFocus();
    m_edit->selectAll();
    update();
}

void PathBar::leaveEditMode()
{
    m_editMode = false;
    if (m_edit) m_edit->hide();
    update();
}

void PathBar::applyEdit()
{
    QString t = QDir::fromNativeSeparators(m_edit->text().trimmed());
    if (!t.isEmpty() && QDir(t).exists())
        emit pathActivated(t);
    leaveEditMode();
}

void PathBar::keyPressEvent(QKeyEvent *e)
{
    if (m_editMode && e->key() == Qt::Key_Escape)
        leaveEditMode();
    else
        QWidget::keyPressEvent(e);
}
