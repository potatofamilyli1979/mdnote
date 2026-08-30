#pragma once

#include <QScrollBar>

class QAbstractScrollArea;
class QTimer;

// A thin capsule scrollbar that floats over a QAbstractScrollArea's
// viewport instead of occupying layout width -- the target's own
// scrollbar is turned off and this one is bound to it bidirectionally.
// No arrow buttons, colors derived from the theme's text/paper contrast
// color at a few fixed alpha steps, hidden by default and shown only
// while scrolling or while the pointer is near the right edge.
// Deliberately a plain show()/hide() rather than an opacity fade: QSS
// can't animate a scrollbar's opacity or width, and a real fade needs a
// QGraphicsOpacityEffect + QPropertyAnimation rig for marginal benefit.
class OverlayScrollBar : public QScrollBar
{
    Q_OBJECT

public:
    // Disables area's built-in vertical scrollbar and creates/binds one
    // of these over its viewport instead. Returns the bar so the caller
    // can restyle it (applyColor()) on theme changes.
    static OverlayScrollBar *attach(QAbstractScrollArea *area);

    // dark = the color this scrollbar's chrome is derived from (the
    // theme's paper-contrast text color -- already flips to a light
    // color on a dark background and vice versa).
    void applyColor(const QColor &dark);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit OverlayScrollBar(QAbstractScrollArea *area);
    void reposition();

    QAbstractScrollArea *m_area;
    QTimer *m_hideTimer;
    bool m_hovering = false;
    // Set true by a wheel/scroll-key event on the viewport, consumed (and
    // cleared) by the very next valueChanged this causes -- distinguishes
    // an actual user scroll gesture from a value change that just happens
    // to ride along with one (setCurrentIndex()/setTextCursor() calling
    // their own ensureVisible-style auto-scroll when a file is opened or
    // the sidebar's selection follows it), which should resync silently
    // rather than flash the bar on for 1.2s at moments the user never
    // touched a scrollbar.
    bool m_genuineScrollPending = false;
};
