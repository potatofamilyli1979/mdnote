#include "ThemePicker.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsEffect>
#include <QApplication>
#include <QScreen>
#include <QTimer>

namespace
{
constexpr int kSwatchDiameter = 32;
constexpr int kSwatchMargin = 4; // room for the hover/selected ring outside the circle
// The design spec calls for 30px matching a hypothetical 30px toolbar
// button row; this app's existing toolbar buttons are 28px (see
// EditorArea's kButtonDiameter), so match that instead for a uniform row.
constexpr int kEntryDiameter = 28;
constexpr int kEntryInnerDiameter = 14;

const Theme &themeFor(const Theme *theme)
{
    return theme ? *theme : defaultFlatTheme();
}

void paintBicolorCircle(QPainter &painter, const QRectF &r, const QColor &accent, const QColor &content, const QColor &borderColor)
{
    QPainterPath circle;
    circle.addEllipse(r);
    painter.save();
    painter.setClipPath(circle);
    // Split along the diagonal from top-right to bottom-left: upper-left
    // triangle = accent ("toolbarColor"), lower-right triangle = content
    // ("paperColor").
    QPainterPath upperLeft;
    upperLeft.moveTo(r.topLeft());
    upperLeft.lineTo(r.topRight());
    upperLeft.lineTo(r.bottomLeft());
    upperLeft.closeSubpath();
    painter.fillPath(upperLeft, accent);

    QPainterPath lowerRight;
    lowerRight.moveTo(r.topRight());
    lowerRight.lineTo(r.bottomRight());
    lowerRight.lineTo(r.bottomLeft());
    lowerRight.closeSubpath();
    painter.fillPath(lowerRight, content);
    painter.restore();

    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(r);
}

constexpr int kPopupRadius = 12;
// Room around the visible card for QGraphicsDropShadowEffect to render
// into -- see RoundedCornersEffect's doc comment for why the shadow
// effect and the corner-rounding effect live on two different widgets.
constexpr int kPopupShadowMargin = 20;

// Clips the card's rendered content (all four corners, unlike
// SlideWindow.cpp's left-only-rounded card) through an antialiased
// QPainterPath clip instead of QWidget::setMask()'s binary QRegion --
// see SlideWindow.cpp's own RoundedCornersEffect doc comment for why a
// mask can never look smooth regardless of how finely its polygon
// approximates a circle. This card used to paint its own white
// rounded-rect background *and* set a mask shaped by a second,
// independently-computed rounded-rect polygon -- the two used slightly
// different rectangles (the paint path was inset by 1px, the mask
// wasn't), so the mask edge sat a hair outside the painted edge and
// exposed a sliver of fully transparent gap all along the right and
// bottom edges. Clipping and drawing from the exact same QPainterPath
// here removes the two-different-shapes mismatch entirely, not just the
// aliasing.
class RoundedCornersEffect : public QGraphicsEffect
{
public:
    explicit RoundedCornersEffect(qreal radius, QObject *parent = nullptr)
        : QGraphicsEffect(parent)
        , m_radius(radius)
    {
    }

protected:
    void draw(QPainter *painter) override
    {
        QPoint offset;
        const QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset);
        if (pixmap.isNull()) {
            return;
        }

        const QRectF r(0, 0, pixmap.width() / pixmap.devicePixelRatio(), pixmap.height() / pixmap.devicePixelRatio());
        QPainterPath path;
        path.addRoundedRect(r, m_radius, m_radius);
        const QPainterPath translatedPath = path.translated(offset);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setClipPath(translatedPath);
        painter->drawPixmap(offset, pixmap);
        painter->setClipping(false);
        // Same path used for the clip and the border, so the border
        // traces exactly where the clip actually cut -- the previous
        // two-different-rectangles mismatch above is what made a
        // visible gap possible in the first place.
        painter->setPen(QPen(QColor(0, 0, 0, 31), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(translatedPath);
        painter->restore();
    }

private:
    qreal m_radius;
};

// QLabel's usual palette-based text color is unreliable for this one
// specific label -- for some (not all) themes the "Preview: xxx"
// /"Current: xxx" status line renders invisible against the popup's own
// fixed white card, even though the palette is set to a fixed black
// regardless of theme and nothing here should vary with it. Paints the
// text directly with an explicit color instead, sidestepping the
// style/palette cascade entirely -- guaranteed to render the same way
// regardless of theme.
class FixedColorLabel : public QLabel
{
public:
    using QLabel::QLabel;

    void setTextColor(const QColor &color)
    {
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(m_color);
        painter.setFont(font());
        painter.drawText(rect(), alignment(), text());
    }

private:
    QColor m_color{0, 0, 0, 166};
};
}

// ---------------------------------------------------------------- ThemeSwatch

ThemeSwatch::ThemeSwatch(const Theme *theme, const QString &key, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
    , m_key(key)
{
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

QString ThemeSwatch::label() const
{
    return translatedLabel(themeFor(m_theme));
}

void ThemeSwatch::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    update();
}

QSize ThemeSwatch::sizeHint() const
{
    const qreal scale = uiChromeScale(this);
    const int d = qRound((kSwatchDiameter + kSwatchMargin * 2) * scale);
    return QSize(d, d);
}

void ThemeSwatch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const Theme &t = themeFor(m_theme);
    // Derived from this widget's actual (already-scaled, via
    // sizeHint()/setFixedSize() in the constructor) size, keeping the
    // same margin-to-diameter proportion rather than recomputing
    // uiChromeScale() a second time here.
    const qreal margin = width() * (qreal(kSwatchMargin) / (kSwatchDiameter + kSwatchMargin * 2));
    const qreal diameter = width() - 2 * margin;
    const QRectF circleRect(margin, margin, diameter, diameter);

    const double luminanceAccent = (0.299 * t.accent.red() + 0.587 * t.accent.green() + 0.114 * t.accent.blue()) / 255.0;
    const double luminanceContent = (0.299 * t.content.red() + 0.587 * t.content.green() + 0.114 * t.content.blue()) / 255.0;
    const bool bothLight = luminanceAccent > 0.75 && luminanceContent > 0.75;
    const QColor borderColor = bothLight ? QColor(0, 0, 0, 46) : QColor(0, 0, 0, 31);

    if (m_selected) {
        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(circleRect.adjusted(-2, -2, 2, 2));
        painter.setPen(QPen(t.accent, 2));
        painter.drawEllipse(circleRect.adjusted(-4, -4, 4, 4));
    } else if (m_hovered || m_keyboardHighlighted) {
        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(circleRect.adjusted(-2, -2, 2, 2));
        painter.setPen(QPen(QColor(0, 0, 0, 89), 1));
        painter.drawEllipse(circleRect.adjusted(-3, -3, 3, 3));
    }

    paintBicolorCircle(painter, circleRect, t.accent, t.content, borderColor);

    if (m_selected) {
        constexpr qreal badgeD = 14;
        const QRectF badgeRect(circleRect.right() - badgeD * 0.7, circleRect.bottom() - badgeD * 0.7, badgeD, badgeD);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(t.accent);
        painter.drawEllipse(badgeRect);
        QPen checkPen(Qt::white, 1.6);
        checkPen.setCapStyle(Qt::RoundCap);
        checkPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(checkPen);
        const QPointF c1(badgeRect.left() + badgeRect.width() * 0.28, badgeRect.top() + badgeRect.height() * 0.52);
        const QPointF c2(badgeRect.left() + badgeRect.width() * 0.44, badgeRect.top() + badgeRect.height() * 0.70);
        const QPointF c3(badgeRect.left() + badgeRect.width() * 0.76, badgeRect.top() + badgeRect.height() * 0.32);
        painter.drawLine(c1, c2);
        painter.drawLine(c2, c3);
    }
}

void ThemeSwatch::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    update();
    Q_EMIT previewed(m_key);
}

void ThemeSwatch::leaveEvent(QEvent *)
{
    m_hovered = false;
    update();
    Q_EMIT previewCanceled();
}

void ThemeSwatch::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        Q_EMIT committed(m_key);
    }
}

void ThemeSwatch::setKeyboardHighlighted(bool highlighted)
{
    if (m_keyboardHighlighted == highlighted) {
        return;
    }
    m_keyboardHighlighted = highlighted;
    update();
    if (highlighted) {
        Q_EMIT previewed(m_key);
    }
}

// ------------------------------------------------------------ ThemePickerPopup

ThemePickerPopup::ThemePickerPopup(QWidget *parent)
    // Qt::NoDropShadowWindowHint: Windows normally gives Qt::Popup
    // windows a native CS_DROPSHADOW edge -- a plain rectangular shadow
    // drawn by DWM directly against the raw HWND bounds, with no
    // awareness of this popup's own rounded/translucent content. Left
    // enabled, it showed as a crisp (unblurred, so clearly distinct from
    // this popup's own soft QGraphicsDropShadowEffect below) rectangular
    // line right at kPopupShadowMargin's edge, well outside the actual
    // rounded card.
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground, true);

    // Shadow lives on a plain wrapper, not on the card that paints/clips
    // itself -- see RoundedCornersEffect's doc comment: a graphics effect
    // applied to the same widget that also does its own custom paint
    // only composites correctly on some edges.
    auto *shadowWrapper = new QWidget(this);
    auto *shadow = new QGraphicsDropShadowEffect(shadowWrapper);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 61));
    shadowWrapper->setGraphicsEffect(shadow);

    // See EditorArea's toolbarScale/uiChromeScale() doc comment: this
    // popup's own literal pixel sizes don't track a screen's size the
    // way the swatches' theme-name status text and other point-sized
    // content elsewhere already does.
    const qreal scale = uiChromeScale(this);

    auto *card = new QWidget(shadowWrapper);
    // autoFillBackground()+palette rather than a stylesheet -- see the
    // divider widget further down for why (Fusion's WA_StyledBackground
    // side effect from any stylesheet at all, even a scoped/transparent
    // one).
    card->setAutoFillBackground(true);
    QPalette cardPalette = card->palette();
    cardPalette.setColor(QPalette::Window, Qt::white);
    card->setPalette(cardPalette);
    card->setGraphicsEffect(new RoundedCornersEffect(kPopupRadius * scale, card));
    auto *shadowLayout = new QVBoxLayout(shadowWrapper);
    shadowLayout->setContentsMargins(0, 0, 0, 0);
    shadowLayout->addWidget(card);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(kPopupShadowMargin, kPopupShadowMargin, kPopupShadowMargin, kPopupShadowMargin);
    outerLayout->addWidget(shadowWrapper);

    auto *grid = new QGridLayout;
    grid->setSpacing(qRound(10 * scale));

    int index = 0;
    // "System Default" first, matching the previous dropdown's order,
    // then the 9 presets in their existing display order.
    auto addSwatch = [&](const Theme *theme, const QString &key) {
        auto *swatch = new ThemeSwatch(theme, key, card);
        connect(swatch, &ThemeSwatch::previewed, this, &ThemePickerPopup::previewRequested);
        connect(swatch, &ThemeSwatch::previewed, this, [this](const QString &key) { showPreviewStatus(key); });
        connect(swatch, &ThemeSwatch::previewCanceled, this, &ThemePickerPopup::previewCanceled);
        connect(swatch, &ThemeSwatch::previewCanceled, this, [this] { showAppliedStatus(); });
        connect(swatch, &ThemeSwatch::committed, this, [this](const QString &key) { commitTheme(key); });
        grid->addWidget(swatch, index / 5, index % 5);
        m_swatches.push_back(swatch);
        ++index;
    };

    addSwatch(nullptr, QString());
    for (const Theme &theme : availableThemes()) {
        addSwatch(&theme, theme.key);
    }

    auto *statusLabel = new FixedColorLabel(card);
    statusLabel->setAlignment(Qt::AlignCenter);
    QFont statusFont = statusLabel->font();
    statusFont.setPixelSize(qRound(11 * scale));
    statusLabel->setFont(statusFont);
    statusLabel->setTextColor(QColor(0, 0, 0, 115));
    m_statusLabel = statusLabel;

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(qRound(12 * scale), qRound(12 * scale), qRound(12 * scale), qRound(10 * scale));
    layout->setSpacing(0);
    layout->addLayout(grid);

    // Deliberately no setStyleSheet() anywhere in this popup, on itself
    // or any child (the divider below included): Qt's style sheet
    // machinery implies WA_StyledBackground for a widget carrying any
    // stylesheet at all, even one scoped to a child selector or set to
    // "background: transparent", which under the Fusion style paints an
    // opaque rect for that widget's own background before anything else
    // runs.

    auto *divider = new QWidget(card);
    divider->setFixedHeight(1);
    divider->setAutoFillBackground(true);
    QPalette dividerPalette = divider->palette();
    dividerPalette.setColor(QPalette::Window, QColor(0, 0, 0, 20));
    divider->setPalette(dividerPalette);
    layout->addSpacing(qRound(10 * scale));
    layout->addWidget(divider);
    layout->addSpacing(qRound(9 * scale));
    layout->addWidget(m_statusLabel);

    // StrongFocus (not NoFocus) so showEvent()'s setFocus() actually
    // lands here, letting keyPressEvent() drive arrow-key navigation.
    setFocusPolicy(Qt::StrongFocus);
}

void ThemePickerPopup::setAppliedThemeKey(const QString &key)
{
    m_appliedKey = key;
    for (ThemeSwatch *swatch : m_swatches) {
        swatch->setSelected(swatch->key() == key);
    }
    showAppliedStatus();
}

void ThemePickerPopup::showPreviewStatus(const QString &previewKey)
{
    const Theme *theme = findTheme(previewKey);
    m_statusLabel->setText(tr("Preview: %1").arg(translatedLabel(themeFor(theme))));
    static_cast<FixedColorLabel *>(m_statusLabel)->setTextColor(QColor(0, 0, 0, 166));
}

void ThemePickerPopup::showAppliedStatus()
{
    const Theme *theme = findTheme(m_appliedKey);
    m_statusLabel->setText(tr("Current: %1").arg(translatedLabel(themeFor(theme))));
    static_cast<FixedColorLabel *>(m_statusLabel)->setTextColor(QColor(0, 0, 0, 115));
}

void ThemePickerPopup::popupBelow(QWidget *anchor)
{
    adjustSize();
    // This window is kPopupShadowMargin bigger than the visible card on
    // every side (room for the drop shadow) -- shift the anchor math by
    // that margin so the *card*, not the invisible padded window, lands
    // at the intended visual position.
    const QPoint anchorBottomLeft = anchor->mapToGlobal(QPoint(0, anchor->height()));
    const QPoint anchorBottomRight = anchor->mapToGlobal(QPoint(anchor->width(), anchor->height()));
    int x = anchorBottomRight.x() + kPopupShadowMargin - width();
    const QScreen *screen = anchor->screen() ? anchor->screen() : QApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 4096, 4096);
    if (x < avail.left()) {
        x = anchorBottomLeft.x() - kPopupShadowMargin;
    }
    x = qBound(avail.left(), x, avail.right() - width());
    const int y = anchorBottomLeft.y() + 6 - kPopupShadowMargin;
    move(x, y);
    show();
}

void ThemePickerPopup::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // Any path that closes the popup while a preview is still active
    // (Esc, click outside, alt-tab away, ...) must roll back -- a
    // preview must never "stick" just because the popup went away.
    Q_EMIT previewCanceled();
    if (m_keyboardIndex >= 0 && m_keyboardIndex < int(m_swatches.size())) {
        m_swatches[m_keyboardIndex]->setKeyboardHighlighted(false);
    }
    m_keyboardIndex = -1;
}

void ThemePickerPopup::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setFocus(Qt::PopupFocusReason);
    m_keyboardIndex = -1;
}

void ThemePickerPopup::setKeyboardIndex(int index)
{
    if (index < 0 || index >= int(m_swatches.size()) || index == m_keyboardIndex) {
        return;
    }
    if (m_keyboardIndex >= 0) {
        m_swatches[m_keyboardIndex]->setKeyboardHighlighted(false);
    }
    m_keyboardIndex = index;
    m_swatches[m_keyboardIndex]->setKeyboardHighlighted(true);
}

void ThemePickerPopup::keyPressEvent(QKeyEvent *event)
{
    constexpr int kColumns = 5;
    const int count = int(m_swatches.size());
    switch (event->key()) {
    case Qt::Key_Escape:
        hide();
        return;
    case Qt::Key_Left:
        setKeyboardIndex(m_keyboardIndex <= 0 ? 0 : m_keyboardIndex - 1);
        return;
    case Qt::Key_Right:
        setKeyboardIndex(m_keyboardIndex < 0 ? 0 : qMin(m_keyboardIndex + 1, count - 1));
        return;
    case Qt::Key_Up:
        setKeyboardIndex(m_keyboardIndex - kColumns < 0 ? m_keyboardIndex : m_keyboardIndex - kColumns);
        return;
    case Qt::Key_Down:
        setKeyboardIndex(m_keyboardIndex < 0 ? 0
                          : (m_keyboardIndex + kColumns >= count ? m_keyboardIndex : m_keyboardIndex + kColumns));
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (m_keyboardIndex >= 0) {
            commitTheme(m_swatches[m_keyboardIndex]->key());
        }
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void ThemePickerPopup::commitTheme(const QString &key)
{
    setAppliedThemeKey(key);
    Q_EMIT themeCommitted(key);
    QTimer::singleShot(120, this, [this] { hide(); });
}

// ------------------------------------------------------------ ThemePickerButton

ThemePickerButton::ThemePickerButton(QWidget *parent)
    : QToolButton(parent)
{
    const int diameter = qRound(kEntryDiameter * uiChromeScale(this));
    setFixedSize(diameter, diameter);
    setCursor(Qt::PointingHandCursor);
    setToolTip(translatedLabel(themeFor(nullptr)));
    connect(this, &QToolButton::clicked, this, &ThemePickerButton::togglePopup);
}

void ThemePickerButton::setButtonBackground(const QColor &paperColor)
{
    m_backgroundColor = paperColor;
    update();
}

void ThemePickerButton::setCurrentThemeKey(const QString &key)
{
    m_currentKey = key;
    setToolTip(translatedLabel(themeFor(findTheme(key))));
    if (m_popup) {
        m_popup->setAppliedThemeKey(key);
    }
    update();
}

void ThemePickerButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // This button paints itself entirely (bicolor swatch preview has no
    // stock QToolButton equivalent), which means it never goes through
    // style()->drawControl() at all -- the flatCircleButtonCss() hover/
    // pressed rules the other toolbar buttons get from QSS have nothing
    // to attach to here, so hover has to be handled by hand the same
    // way flatCircleButtonCss() itself picks a shade (lighter on a dark
    // background, darker on a light one).
    QColor bg = m_backgroundColor.isValid() ? m_backgroundColor : QColor(240, 240, 240);
    if (underMouse()) {
        const double luminance = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
        bg = luminance > 0.55 ? bg.darker(112) : bg.lighter(125);
    }

    // Actual widget size, not the constexpr diameter directly -- the
    // constructor already scaled that into setFixedSize(), so reading
    // it back here keeps this in sync without needing its own copy of
    // uiChromeScale().
    const QRectF outer(0, 0, width(), height());
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawEllipse(outer);

    const Theme &t = themeFor(findTheme(m_currentKey));
    const qreal innerDiameter = width() * (qreal(kEntryInnerDiameter) / kEntryDiameter);
    const qreal inset = (width() - innerDiameter) / 2.0;
    const QRectF inner(inset, inset, innerDiameter, innerDiameter);
    paintBicolorCircle(painter, inner, t.accent, t.content, QColor(0, 0, 0, 51));
}

void ThemePickerButton::enterEvent(QEnterEvent *event)
{
    QToolButton::enterEvent(event);
    update();
}

void ThemePickerButton::leaveEvent(QEvent *event)
{
    QToolButton::leaveEvent(event);
    update();
}

void ThemePickerButton::togglePopup()
{
    if (m_popup && m_popup->isVisible()) {
        m_popup->hide();
        return;
    }
    if (!m_popup) {
        m_popup = new ThemePickerPopup(this);
        connect(m_popup, &ThemePickerPopup::previewRequested, this, &ThemePickerButton::themePreviewed);
        connect(m_popup, &ThemePickerPopup::previewCanceled, this, &ThemePickerButton::previewCanceled);
        connect(m_popup, &ThemePickerPopup::themeCommitted, this, [this](const QString &key) {
            m_currentKey = key;
            setToolTip(translatedLabel(themeFor(findTheme(key))));
            update();
            Q_EMIT themeCommitted(key);
        });
    }
    m_popup->setAppliedThemeKey(m_currentKey);
    m_popup->popupBelow(this);
}
