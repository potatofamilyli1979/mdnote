#include "IconGlyphs.h"

#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QBrush>
#include <cmath>

namespace
{

void drawZoom(QPainter &painter, int size, int direction)
{
    // direction: -1 = minus (zoom out), 0 = empty lens (reset), +1 = plus (zoom in).
    const qreal lensDiameter = size * 0.62;
    const QRectF lensRect(size * 0.10, size * 0.10, lensDiameter, lensDiameter);
    painter.drawEllipse(lensRect);

    const QPointF center = lensRect.center();
    const QPointF dir(0.7071, 0.7071);
    const QPointF handleFrom = center + dir * (lensDiameter / 2.0);
    const QPointF handleTo = handleFrom + dir * (size * 0.24);
    painter.drawLine(handleFrom, handleTo);

    if (direction != 0) {
        const qreal half = lensDiameter * 0.22;
        painter.drawLine(QPointF(center.x() - half, center.y()), QPointF(center.x() + half, center.y()));
        if (direction > 0) {
            painter.drawLine(QPointF(center.x(), center.y() - half), QPointF(center.x(), center.y() + half));
        }
    }
}

void drawSidebarToggle(QPainter &painter, int size)
{
    const QRectF outer(size * 0.14, size * 0.16, size * 0.72, size * 0.68);
    painter.drawRoundedRect(outer, 2, 2);
    const qreal dividerX = outer.left() + outer.width() * 0.4;
    painter.drawLine(QPointF(dividerX, outer.top()), QPointF(dividerX, outer.bottom()));
}

void drawModePreview(QPainter &painter, int size)
{
    // A simple eye: a wide lens outline plus a filled pupil.
    const QRectF lens(size * 0.10, size * 0.32, size * 0.80, size * 0.40);
    painter.drawArc(lens, 0, 180 * 16);
    painter.drawArc(lens, 180 * 16, 180 * 16);
    const qreal pupilRadius = size * 0.09;
    QBrush savedBrush = painter.brush();
    painter.setBrush(painter.pen().color());
    painter.drawEllipse(lens.center(), pupilRadius, pupilRadius);
    painter.setBrush(savedBrush);
}

void drawModeSource(QPainter &painter, int size)
{
    // "</>": two angle brackets, the universal "this is raw code/markup"
    // mark -- distinct from ModePreview's eye so the button's icon
    // actually reflects which of the two modes is currently active
    // rather than staying static.
    QPolygonF left;
    left << QPointF(size * 0.42, size * 0.24) << QPointF(size * 0.14, size * 0.50) << QPointF(size * 0.42, size * 0.76);
    painter.drawPolyline(left);
    QPolygonF right;
    right << QPointF(size * 0.58, size * 0.24) << QPointF(size * 0.86, size * 0.50) << QPointF(size * 0.58, size * 0.76);
    painter.drawPolyline(right);
}

void drawSave(QPainter &painter, int size)
{
    // Stylized floppy disk: body, shutter notch, label.
    const QRectF body(size * 0.15, size * 0.12, size * 0.70, size * 0.76);
    painter.drawRoundedRect(body, 2, 2);
    const QRectF shutter(size * 0.32, size * 0.12, size * 0.36, size * 0.22);
    painter.drawRect(shutter);
    const QRectF label(size * 0.28, size * 0.55, size * 0.44, size * 0.26);
    painter.drawRect(label);
}

void drawFolderOpen(QPainter &painter, int size)
{
    const QRectF tab(size * 0.14, size * 0.22, size * 0.32, size * 0.14);
    painter.drawRoundedRect(tab, 1, 1);
    const QRectF body(size * 0.14, size * 0.34, size * 0.72, size * 0.48);
    painter.drawRoundedRect(body, 2, 2);
}

void drawNewNote(QPainter &painter, int size)
{
    const QRectF doc(size * 0.26, size * 0.10, size * 0.48, size * 0.78);
    painter.drawRoundedRect(doc, 2, 2);
    const qreal cx = doc.center().x();
    const qreal cy = doc.top() + doc.height() * 0.68;
    const qreal half = size * 0.13;
    painter.drawLine(QPointF(cx - half, cy), QPointF(cx + half, cy));
    painter.drawLine(QPointF(cx, cy - half), QPointF(cx, cy + half));
}

void drawTrash(QPainter &painter, int size)
{
    const QRectF lid(size * 0.22, size * 0.20, size * 0.56, 0);
    painter.drawLine(lid.topLeft(), lid.topRight());
    const QRectF handle(size * 0.38, size * 0.10, size * 0.24, size * 0.10);
    painter.drawRoundedRect(handle, 1, 1);
    const QRectF body(size * 0.26, size * 0.20, size * 0.48, size * 0.66);
    painter.drawRoundedRect(body, 2, 2);
    for (int i = 0; i < 3; ++i) {
        const qreal x = body.left() + body.width() * (i + 1) / 4.0;
        painter.drawLine(QPointF(x, body.top() + 6), QPointF(x, body.bottom() - 5));
    }
}

void drawRename(QPainter &painter, int size)
{
    // A pencil: shaft as a line, a small triangular nib at the writing
    // end, drawn diagonally like a classic "edit" glyph.
    const QPointF from(size * 0.22, size * 0.78);
    const QPointF to(size * 0.72, size * 0.28);
    painter.drawLine(from, to);
    const QPointF tipDir = (to - from);
    const qreal len = std::hypot(tipDir.x(), tipDir.y());
    const QPointF unit = len > 0 ? tipDir / len : QPointF(1, 0);
    const QPointF normal(-unit.y(), unit.x());
    const QPointF nibBase = to;
    const QPointF nibTip = nibBase + unit * (size * 0.14);
    const QPointF nibLeft = nibBase + normal * (size * 0.07);
    const QPointF nibRight = nibBase - normal * (size * 0.07);
    painter.drawLine(nibLeft, nibTip);
    painter.drawLine(nibRight, nibTip);
    painter.drawLine(nibLeft, nibRight);
}

void drawStar(QPainter &painter, int size, bool filled)
{
    QPolygonF star;
    const QPointF center(size / 2.0, size / 2.0);
    const qreal outerR = size * 0.42;
    const qreal innerR = outerR * 0.42;
    for (int i = 0; i < 10; ++i) {
        const qreal r = (i % 2 == 0) ? outerR : innerR;
        const qreal angle = -M_PI / 2 + i * M_PI / 5;
        star << QPointF(center.x() + r * std::cos(angle), center.y() + r * std::sin(angle));
    }
    if (filled) {
        QBrush savedBrush = painter.brush();
        painter.setBrush(painter.pen().color());
        painter.drawPolygon(star);
        painter.setBrush(savedBrush);
    } else {
        painter.drawPolygon(star);
    }
}

}

QIcon makeGlyphIcon(Glyph glyph, const QColor &color, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color);
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (glyph) {
    case Glyph::ZoomOut:
        drawZoom(painter, size, -1);
        break;
    case Glyph::ZoomReset:
        drawZoom(painter, size, 0);
        break;
    case Glyph::ZoomIn:
        drawZoom(painter, size, 1);
        break;
    case Glyph::SidebarToggle:
        drawSidebarToggle(painter, size);
        break;
    case Glyph::ModePreview:
        drawModePreview(painter, size);
        break;
    case Glyph::ModeSource:
        drawModeSource(painter, size);
        break;
    case Glyph::Save:
        drawSave(painter, size);
        break;
    case Glyph::FolderOpen:
        drawFolderOpen(painter, size);
        break;
    case Glyph::NewNote:
        drawNewNote(painter, size);
        break;
    case Glyph::Trash:
        drawTrash(painter, size);
        break;
    case Glyph::Rename:
        drawRename(painter, size);
        break;
    case Glyph::Star:
        drawStar(painter, size, true);
        break;
    }

    painter.end();
    return QIcon(pixmap);
}
