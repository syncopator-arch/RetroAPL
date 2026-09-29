#include "graphwindow.h"

#include <QPainter>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QWindow>

GraphWindow::GraphWindow(int handle, const QString &title, int w, int h,
                          QWidget *parent)
    : QWidget(parent)
    , m_handle(handle)
    , m_image(qMax(w, 1), qMax(h, 1), QImage::Format_RGB32)
    , m_penColor(Qt::black)
    , m_penWidth(1)
    , m_penPos(0, 0)
{
    m_image.fill(Qt::white);
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(m_image.size());
}

GraphWindow::GraphWindow(int handle, const QString &title,
                          const QImage &image, QWidget *parent)
    : QWidget(parent)
    , m_handle(handle)
    , m_image(image.convertToFormat(QImage::Format_RGB32))
    , m_penColor(Qt::black)
    , m_penWidth(1)
    , m_penPos(0, 0)
{
    // No m_image.fill() here -- unlike the blank-window constructor
    // above, the whole point is that m_image already *is* the window's
    // real content the moment it's set.
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(m_image.size());
}

void GraphWindow::setPen(const QColor &color, int width)
{
    m_penColor = color;
    // Clamp to >=1: width feeds both QPen (0 would mean Qt's cosmetic
    // 1px hairline, which is a different thing) and WPOINTS' fillRect
    // sizing, where 0 would plot nothing at all.
    m_penWidth = qMax(1, width);
}

void GraphWindow::recreateNativeWindow()
{
    // Content-area geometry to re-apply: the one recorded before the
    // hide if there is one (see hideRememberingGeometry() for why the
    // current geometry() is wrong at this point), otherwise the current
    // one (e.g. after a WCTRL 4 move while hidden). geometry() (content
    // area), not pos() (frame): after destroy() Qt no longer knows the
    // frame margins, so a frame position would land the content area
    // there instead -- one title bar too high (openbox test).
    QRect g = m_geometryBeforeHide.isNull() ? geometry() : m_geometryBeforeHide;
    m_geometryBeforeHide = QRect();
    m_minimizedAtHide = false;
    // QWidget::destroy() releases the native window without touching
    // the widget itself -- no closeEvent, so WA_DeleteOnClose doesn't
    // fire. The next show() re-creates the native window automatically.
    destroy();
    // With no native window left, these just update stored state;
    // nothing is sent to the window manager until show().
    setWindowState(windowState() & ~Qt::WindowMinimized);
    setGeometry(g);
}

void GraphWindow::hideRememberingGeometry()
{
    if (!isHidden()) {
        m_geometryBeforeHide = geometry();
        // Frame position from the QWindow, same source (and same reason)
        // as GraphManager::size(): QWidget::pos() can be stale after a
        // recreateNativeWindow().
        m_framePosBeforeHide = windowHandle() ? windowHandle()->framePosition()
                                              : pos();
        m_hasFramePosBeforeHide = true;
        m_minimizedAtHide = isMinimized();   // see wasMinimizedWhenHidden()
    }
    hide();
}

void GraphWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.drawImage(0, 0, m_image);
}

void GraphWindow::closeEvent(QCloseEvent *event)
{
    emit closing(m_handle);
    QWidget::closeEvent(event);
}

void GraphWindow::flush()
{
    update();
}

void GraphWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // event->pos() rather than position().toPoint(): this codebase
        // hasn't adopted the newer QPointF-based event API anywhere else
        // (grep of the rest of the tree turned up nothing), so staying
        // with the older call keeps this consistent rather than being
        // the one spot that's different -- revisit if the build ever
        // flags it deprecated.
        QPoint pt = event->pos();
        emit clicked(m_handle, pt.x(), pt.y());
    }
    QWidget::mousePressEvent(event);
}
