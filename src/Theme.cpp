#include "Theme.h"

#include <QMenu>
#include <QWidget>
#include <QScreen>
#include <QGuiApplication>
#include <QCoreApplication>

qreal uiChromeScale(const QWidget *widget)
{
    const QScreen *screen = (widget && widget->screen()) ? widget->screen() : QGuiApplication::primaryScreen();
    if (!screen) {
        return 1.0;
    }
    constexpr qreal kReferenceWidth = 1440.0;
    const qreal ratio = screen->availableGeometry().width() / kReferenceWidth;
    return qBound(1.0, 1.0 + (ratio - 1.0) * 0.5, 1.6);
}

namespace
{
const std::vector<Theme> kThemes = {
    {QStringLiteral("chunri"), QT_TRANSLATE_NOOP("Theme", "Spring Blossom"), QColor(0xff, 0xb3, 0xb3), QColor(0xff, 0xff, 0xff)},
    {QStringLiteral("jiangnan"), QT_TRANSLATE_NOOP("Theme", "Water Town"), QColor(0x2c, 0x31, 0x35), QColor(0x5d, 0x7b, 0x8b)},
    {QStringLiteral("zhulin"), QT_TRANSLATE_NOOP("Theme", "Bamboo Grove"), QColor(0x78, 0x92, 0x62), QColor(0xf0, 0xf5, 0xe5)},
    {QStringLiteral("meixue"), QT_TRANSLATE_NOOP("Theme", "Winter Plum"), QColor(0xd9, 0x53, 0x4f), QColor(0xff, 0xff, 0xff)},
    {QStringLiteral("songfeng"), QT_TRANSLATE_NOOP("Theme", "Pine Wind"), QColor(0x2f, 0x4f, 0x4f), QColor(0xf0, 0xf8, 0xff)},
    {QStringLiteral("chaxiang"), QT_TRANSLATE_NOOP("Theme", "Tea Aroma"), QColor(0x55, 0x6b, 0x2f), QColor(0xde, 0xb8, 0x87)},
    {QStringLiteral("zijin"), QT_TRANSLATE_NOOP("Theme", "Forbidden Palace"), QColor(0x8b, 0x00, 0x00), QColor(0xff, 0xd7, 0x00)},
    {QStringLiteral("qinghua"), QT_TRANSLATE_NOOP("Theme", "Blue Porcelain"), QColor(0x00, 0x00, 0xcd), QColor(0xf0, 0xf8, 0xff)},
    {QStringLiteral("gudao"), QT_TRANSLATE_NOOP("Theme", "Ancient Road"), QColor(0xda, 0xa5, 0x20), QColor(0x8b, 0x45, 0x13)},
};
}

const std::vector<Theme> &availableThemes()
{
    return kThemes;
}

const Theme *findTheme(const QString &key)
{
    if (key.isEmpty()) {
        return nullptr;
    }
    for (const Theme &theme : kThemes) {
        if (theme.key == key) {
            return &theme;
        }
    }
    return nullptr;
}

const Theme &defaultFlatTheme()
{
    static const Theme kDefault{QString(), QT_TRANSLATE_NOOP("Theme", "System Default"), QColor(0x2e, 0x2e, 0x32), QColor(0xff, 0xff, 0xff)};
    return kDefault;
}

QString translatedLabel(const Theme &theme)
{
    return QCoreApplication::translate("Theme", theme.label);
}

QColor contrastTextColor(const QColor &background)
{
    // Perceptual (not simple-average) luminance, so e.g. pure blue
    // (dark-looking despite a bright-ish average) still gets light text.
    const double luminance = (0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue()) / 255.0;
    return luminance > 0.55 ? QColor(0x20, 0x20, 0x20) : QColor(0xf0, 0xf0, 0xf0);
}

QString contextMenuCss(bool darkBackground)
{
    const QColor bg = darkBackground ? QColor(0x1f, 0x1f, 0x1f) : QColor(0xff, 0xff, 0xff);
    const QColor text = darkBackground ? QColor(0xf2, 0xf2, 0xf2) : QColor(0x20, 0x20, 0x20);
    const QString border = darkBackground ? QStringLiteral("rgba(255,255,255,36)") : QStringLiteral("rgba(0,0,0,36)");
    const QString hover = darkBackground ? QStringLiteral("rgba(255,255,255,22)") : QStringLiteral("rgba(0,0,0,15)");

    return QStringLiteral("QMenu { background-color: %1; color: %2; border: 1px solid %3; "
                           "border-radius: 7px; padding: 4px; } "
                           "QMenu::item { padding: 5px 9px; border-radius: 4px; } "
                           "QMenu::item:selected { background-color: %4; } "
                           "QMenu::item:disabled { color: rgba(128, 128, 128, 150); } "
                           "QMenu::separator { height: 1px; background: %3; margin: 4px 2px; } "
                           "QMenu::icon { padding-left: 2px; }")
        .arg(bg.name(), text.name(), border, hover);
}

void styleContextMenu(QMenu *menu, bool darkBackground)
{
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setStyleSheet(contextMenuCss(darkBackground));
}

QString flatCircleButtonCss(const QString &selector, const QColor &background, int diameter)
{
    const double luminance = (0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue()) / 255.0;
    const bool isLight = luminance > 0.55;
    const QColor hover = isLight ? background.darker(112) : background.lighter(125);
    const QColor pressed = isLight ? background.darker(124) : background.lighter(150);

    return QStringLiteral("%1 { background-color: %2; border: none; border-radius: %3px; } "
                           "%1:hover { background-color: %4; } "
                           "%1:pressed { background-color: %5; }")
        .arg(selector)
        .arg(background.name())
        .arg(diameter / 2)
        .arg(hover.name())
        .arg(pressed.name());
}
