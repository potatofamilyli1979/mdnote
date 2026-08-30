#include "OverlayScrollBar.h"

#include <QAbstractScrollArea>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEnterEvent>
#include <QSignalBlocker>

namespace
{
constexpr int kHitWidth = 16;   // command hit region; visual width is narrower, drawn via QSS margin
constexpr int kEndInset = 8;    // top/bottom inset from the viewport edges
constexpr int kIdleMs = 1200;   // hides this long after the last scroll/hover
}

OverlayScrollBar::OverlayScrollBar(QAbstractScrollArea *area)
    : QScrollBar(Qt::Vertical, area)
    , m_area(area)
    , m_hideTimer(new QTimer(this))
{
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(kIdleMs);
    connect(m_hideTimer, &QTimer::timeout, this, [this] {
        if (!m_hovering && !isSliderDown()) {
            hide();
        }
    });
    hide();
}

OverlayScrollBar *OverlayScrollBar::attach(QAbstractScrollArea *area)
{
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *bar = new OverlayScrollBar(area);
    QScrollBar *real = area->verticalScrollBar();

    auto syncFromReal = [bar, real] {
        const QSignalBlocker blocker(bar);
        bar->setRange(real->minimum(), real->maximum());
        bar->setPageStep(real->pageStep());
        bar->setValue(real->value());
    };
    syncFromReal();
    QObject::connect(real, &QScrollBar::rangeChanged, bar, [syncFromReal](int, int) { syncFromReal(); });
    QObject::connect(real, &QScrollBar::valueChanged, bar, [bar, syncFromReal](int) {
        syncFromReal();
        // Only reveal for a value change the user actually caused by
        // scrolling -- see m_genuineScrollPending's comment. A value
        // change from opening a file or the sidebar following the
        // current selection resyncs the bar's position silently instead.
        if (bar->m_genuineScrollPending) {
            bar->m_genuineScrollPending = false;
            if (bar->maximum() > bar->minimum()) {
                bar->show();
                bar->raise();
            }
            bar->m_hideTimer->start();
        }
    });
    QObject::connect(bar, &QScrollBar::valueChanged, real, [real](int value) {
        if (real->value() != value) {
            real->setValue(value);
        }
    });

    // Watching the viewport's own resize/show, not the scroll area's --
    // QAbstractScrollArea resizes its viewport *from inside* its own
    // resizeEvent(), and Qt delivers event-filter notifications before
    // the watched object's handler runs. Filtering area's resize event
    // would read viewport()->width()/height() before that resize had
    // actually happened, catching a stale (often just-constructed,
    // near-zero) size -- which is exactly what produced a scrollbar
    // frozen at the wrong position/height after the window was later
    // resized for real.
    area->viewport()->installEventFilter(bar);
    bar->reposition();
    return bar;
}

void OverlayScrollBar::reposition()
{
    if (!m_area) {
        return;
    }
    // Deliberately m_area's own size, not viewport()'s: a container
    // that gives its scroll area QSS padding (the sidebar's document
    // list does, for its own layout reasons) shrinks the viewport rect
    // by that padding, which pulled this bar in from the true right
    // edge along with it. The area's own geometry is unaffected by
    // that and matches what "flush against the container's edge" means
    // here.
    const int w = m_area->width();
    const int h = m_area->height();
    setGeometry(w - kHitWidth, kEndInset, kHitWidth, qMax(0, h - 2 * kEndInset));
}

bool OverlayScrollBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_area->viewport()) {
        return false;
    }
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        reposition();
        raise();
    } else if (event->type() == QEvent::Wheel) {
        m_genuineScrollPending = true;
    } else if (event->type() == QEvent::KeyPress) {
        switch (static_cast<QKeyEvent *>(event)->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            m_genuineScrollPending = true;
            break;
        default:
            break;
        }
    }
    return false;
}

void OverlayScrollBar::enterEvent(QEnterEvent *event)
{
    m_hovering = true;
    m_hideTimer->stop();
    if (maximum() > minimum()) {
        show();
        raise();
    }
    QScrollBar::enterEvent(event);
}

void OverlayScrollBar::leaveEvent(QEvent *event)
{
    m_hovering = false;
    if (!isSliderDown()) {
        m_hideTimer->start();
    }
    QScrollBar::leaveEvent(event);
}

void OverlayScrollBar::mousePressEvent(QMouseEvent *event)
{
    m_hideTimer->stop();
    QScrollBar::mousePressEvent(event);
}

void OverlayScrollBar::mouseReleaseEvent(QMouseEvent *event)
{
    QScrollBar::mouseReleaseEvent(event);
    if (!m_hovering) {
        m_hideTimer->start();
    }
}

void OverlayScrollBar::applyColor(const QColor &dark)
{
    auto rgba = [&dark](int alphaPercent) {
        QColor c = dark;
        c.setAlpha(qRound(255.0 * alphaPercent / 100.0));
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };
    setStyleSheet(QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: %4px; margin: 0; border: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: %1; margin: 8px 4px 8px 4px; border-radius: 4px; }"
        "QScrollBar::handle:vertical {"
        "  background: %2; min-height: 32px; margin: 0 4px 0 4px; border-radius: 4px; border: none; }"
        "QScrollBar::handle:vertical:hover, QScrollBar::handle:vertical:pressed {"
        "  background: %3; margin: 0 2px 0 2px; border-radius: 6px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0; width: 0; background: none; border: none; }"
        "QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {"
        "  background: none; border: none; image: none; }")
        .arg(rgba(6), rgba(42), rgba(58)).arg(kHitWidth));
}
