#pragma once

#include <QColor>
#include <QString>
#include <vector>

class QWidget;

// A scale factor derived from how wide the given widget's screen is
// relative to this app's own reference screen (1440 logical px --
// verified via QScreen::logicalDotsPerInch(), which came back 96 --
// the assumed Qt/Linux baseline -- on that reference machine, ruling
// out a logical-DPI difference as the cause this was first written to
// address). The main window's own width already scales with screen
// width on its own (it's set as a fraction of it); a handful of other
// UI constants are literal pixel counts instead (sidebar width, its
// own QSS font sizes, the main window's drop-shadow margin), and on a
// screen with a much wider logical resolution they end up occupying a
// noticeably smaller *proportion* of an otherwise-proportionally-larger
// window, reading as disproportionately small even though nothing about
// the desktop environment itself is misconfigured. Multiply those
// specific constants by this before using them; leave anything already
// point-sized or ratio-of-screen-size-based alone. Deliberately damped
// (half-strength) rather than a straight width ratio, and capped, since
// a much wider screen doesn't need chrome exactly proportionally
// bigger, just noticeably more than the reference's.
qreal uiChromeScale(const QWidget *widget);

// A named pair of colors: accent (titlebar + sidebar background) and
// content (editor background). Text color on each is derived at apply
// time via contrastTextColor() rather than stored, so it always stays
// legible regardless of which color is picked.
struct Theme
{
    QString key;      // stable identifier, persisted in ConfigManager
    // English source text, marked via QT_TRANSLATE_NOOP("Theme", ...) --
    // NOT pre-translated, since kThemes (see Theme.cpp) is a namespace-
    // scope table built once at static-init time, before QApplication
    // (and its installed QTranslators) exist. Pass through translatedLabel()
    // below to get the actual on-screen text.
    const char *label;
    QColor accent;
    QColor content;
};

// The fixed set of built-in presets, in dropdown display order.
const std::vector<Theme> &availableThemes();

// Looks up a preset by its stored key. Returns nullptr for an unknown
// or empty key (the "system default" / no-override case).
const Theme *findTheme(const QString &key);

// theme.label, translated at call time (see Theme::label's doc comment).
QString translatedLabel(const Theme &theme);

// The fixed black/white flat look applied for "system default" (findTheme()
// returning nullptr). Not literally "leave everything unstyled": once the
// top-level window became fully translucent (for rounded corners + a drop
// shadow), an unstyled toolbar/sidebar had no opaque ancestor background
// left to fall back on and let the desktop show through behind it.
const Theme &defaultFlatTheme();

// Picks readable text color for a given background: dark text on light
// backgrounds, light text on dark ones.
QColor contrastTextColor(const QColor &background);

// QSS for a flat, circular QToolButton: `background` at rest, with
// built-in hover/pressed feedback shades derived from it. `selector`
// scopes the rule (e.g. "#editorToolbar QToolButton").
QString flatCircleButtonCss(const QString &selector, const QColor &background, int diameter);

// QSS for a QMenu: opaque background, 1px border, rounded corners, and
// item hover highlight. Menus built via createStandardContextMenu() (or
// any QMenu parented under a widget with its own bare, unscoped
// background-color/color rule) otherwise inherit that ancestor's colors,
// rendering solid content-colored with no visible border.
QString contextMenuCss(bool darkBackground);

// Applies contextMenuCss() AND Qt::WA_TranslucentBackground -- the
// border-radius in that QSS alone paints a rounded rect *inside* the
// menu's still-rectangular top-level window, leaving square corners
// showing through outside it. Call this instead of
// setStyleSheet(contextMenuCss(...)) directly on every QMenu the app
// constructs.
void styleContextMenu(class QMenu *menu, bool darkBackground);
