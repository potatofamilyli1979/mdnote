#include "SlideWindow.h"
#include "Sidebar.h"
#include "EditorArea.h"
#include "FileManager.h"
#include "ConfigManager.h"
#include "DialogUtils.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QFileDialog>
#include <QLabel>
#include <QShowEvent>
#include <QResizeEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsEffect>
#include <QPainter>
#include <QPainterPath>

#ifdef Q_OS_WIN
// NOMINMAX: windows.h otherwise `#define`s min/max, which silently
// breaks any unrelated std::min/max or qMin/qMax call anywhere later in
// this translation unit (targetGeometry()'s qMax() included).
// WIN32_LEAN_AND_MEAN: excludes rarely-needed API surface (winsock1,
// COM, GDI extras, ...) that both bloats build time and risks its own
// macro/typedef clashes with Qt headers. Both guarded with #ifndef --
// Qt6's own CMake integration already defines them globally for
// anything linking Qt6::Core on Windows.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <KX11Extras>

#ifdef HAVE_LAYERSHELLQT
#include <LayerShellQt/window.h>
#endif
#endif

namespace
{
// Matches the GNOME extension's own default for "vertical-margin-px"
// (gnome-extension/schemas/org.gnome.shell.extensions.mdnote-quake.gschema.xml).
// Used by both targetGeometry() (X11 and the externally-managed-under-
// GNOME case, where it's computed but never actually consulted) and
// configureWayland() (which additionally needs it as a real
// LayerShellQt margin -- see that function's comment for why).
constexpr int kVerticalScreenMargin = 100;

#ifdef Q_OS_WIN
// Windows-only: the GNOME extension's 100px figure was tuned to look
// right against that extension's own "docked card" presentation
// (see gnome-extension/quake-mode.js), which this platform doesn't
// share -- 100px top and bottom reads as too much dead space rather
// than intentional framing here, next to how much taller other normal
// windows on the same screen sit. Smaller margin, taller card; X11 and
// Wayland keep kVerticalScreenMargin as-is.
constexpr int kVerticalScreenMarginWindows = 24;
#endif

// Which monitor a fresh slide-in should appear on: wherever the pointer
// currently is, matching the GNOME extension's own
// monitorDisplayScreenIndex getter (Shell.Global.get().display.
// get_current_monitor(), quake-mode.js) rather than always the primary
// display. Only consulted by the platforms that position the window
// themselves (X11, Windows) -- GNOME's extension and native Wayland's
// LayerShellQt anchoring make their own monitor choice independently.
QScreen *screenUnderCursor()
{
    if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos())) {
        return screen;
    }
    return QGuiApplication::primaryScreen();
}

// Clips the card's rendered content (and everything inside it -- editor,
// sidebar, all of it) to a shape that rounds the left (open) edge while
// leaving the right edge -- flush against the screen edge -- square.
//
// QWidget::setMask() was tried first, but a mask is a QRegion: an
// inherently binary, non-antialiased pixel boundary. No amount of
// polygon-arc sampling fixes that -- it only controls how closely the
// *shape* approximates a circle, not whether its edge is smooth, so the
// curve always looked staircased, worst of all at fractional DPI scale
// factors (Windows' common 125%/150%) where the physical-pixel grid
// doesn't line up cleanly with the logical one.
//
// A QGraphicsEffect instead intercepts the widget's already-rendered
// pixmap and recomposites it through a QPainterPath *clip path* (not a
// region) with antialiasing on -- Qt's raster engine computes real
// per-pixel coverage for an antialiased clip path, giving a genuinely
// smooth edge at any scale factor. This runs entirely in Qt's own
// software compositing, the same as setMask() did for a child widget, so
// it works identically under the externally-managed GNOME setup that has
// no window-shaping protocol support.
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
        path.moveTo(r.right(), r.top());
        path.lineTo(r.left() + m_radius, r.top());
        path.arcTo(QRectF(r.left(), r.top(), 2 * m_radius, 2 * m_radius), 90, 90);
        path.lineTo(r.left(), r.bottom() - m_radius);
        path.arcTo(QRectF(r.left(), r.bottom() - 2 * m_radius, 2 * m_radius, 2 * m_radius), 180, 90);
        path.lineTo(r.right(), r.bottom());
        path.closeSubpath();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setClipPath(path.translated(offset));
        painter->drawPixmap(offset, pixmap);
        painter->restore();
    }

private:
    qreal m_radius;
};
}

SlideWindow::SlideWindow(FileManager *fileManager, ConfigManager *config, QWidget *parent)
    : QWidget(parent)
    , m_fileManager(fileManager)
    , m_config(config)
{
    setObjectName(QStringLiteral("SlideWindow"));
    setWindowTitle(QStringLiteral("mdnote"));
    // Fully transparent: the visible content is m_card, inset from this
    // window's true (externally-managed) frame by kShadowMargin so its
    // drop shadow has room to render on the left/top/bottom.
    setAttribute(Qt::WA_TranslucentBackground, true);

    m_fileManager->setCurrentFolder(m_config->defaultFolder());

    // Purely technical -- just enough room for the drop shadow below to
    // render without being clipped by this window's own edge. Not the
    // user-visible "gap between the window and the screen's top/bottom"
    // (see targetGeometry()'s kVerticalMargin for that) -- conflating
    // the two here was the wrong lever for that ask.
    constexpr int kShadowMargin = 32;

    m_shadowWrapper = new QWidget(this);
    auto *shadow = new QGraphicsDropShadowEffect(m_shadowWrapper);
    shadow->setBlurRadius(42);
    shadow->setOffset(0, 0);
    shadow->setColor(QColor(0, 0, 0, 140));
    m_shadowWrapper->setGraphicsEffect(shadow);

    m_card = new QWidget(m_shadowWrapper);
    m_card->setGraphicsEffect(new RoundedCornersEffect(20, m_card));
    auto *cardLayout = new QVBoxLayout(m_shadowWrapper);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->addWidget(m_card);

    auto *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(kShadowMargin, kShadowMargin, 0, kShadowMargin);
    outerLayout->addWidget(m_shadowWrapper);

    m_editor = new EditorArea(m_fileManager, m_card);
    m_editor->setSourceMode(m_config->lastMode() == QStringLiteral("source"));

    auto *cardInnerLayout = new QHBoxLayout(m_card);
    cardInnerLayout->setContentsMargins(0, 0, 0, 0);
    cardInnerLayout->setSpacing(0);
    cardInnerLayout->addWidget(m_editor);

    // The sidebar floats on top of the editor rather than sharing the
    // layout with it, so it doesn't shrink the text area -- it's
    // positioned/raised manually in updateSidebarGeometry(). Parented to
    // m_card (not this window) so it's clipped by the same rounded mask.
    m_sidebar = new Sidebar(m_fileManager, m_config, m_card);
    m_sidebar->raise();

    m_sidebar->refresh();
    openInitialNote();

    connect(m_sidebar, &Sidebar::fileActivated, this, &SlideWindow::openFileAndShow);
    connect(m_sidebar, &Sidebar::changeFolderRequested, this, &SlideWindow::promptChangeFolder);
    connect(m_sidebar, &Sidebar::newNoteRequested, this, &SlideWindow::createNewNote);
    connect(m_sidebar, &Sidebar::fileRenamed, m_editor, &EditorArea::notifyFileRenamed);
    connect(m_sidebar, &Sidebar::fileDeleted, m_editor, &EditorArea::notifyFileDeleted);
    connect(m_editor, &EditorArea::sidebarToggleRequested, this, [this] {
        m_sidebar->setVisible(!m_sidebar->isVisible());
    });
    connect(m_editor, &EditorArea::editorClicked, this, [this] {
        m_sidebar->hide();
    });
    connect(m_sidebar, &Sidebar::closeRequested, this, [this] {
        m_sidebar->hide();
    });
    connect(m_editor, &EditorArea::titleChanged, this, [this](const QString &title) {
        setWindowTitle(QStringLiteral("mdnote - %1").arg(title));
    });
    connect(m_config, &ConfigManager::widthRatioChanged, this, [this] {
        if (isVisible()) {
            // The screen this window is actually being shown on, not
            // necessarily the pointer's current one (which may have moved
            // since this window last opened) or the primary display.
            setGeometry(targetGeometry(screen()));
        }
    });
    connect(m_editor, &EditorArea::themeSelected, this, [this](const QString &key) {
        const Theme *theme = findTheme(key);
        m_editor->applyTheme(theme);
        m_sidebar->applyTheme(theme);
        m_config->setThemeKey(key);
        // Flush immediately rather than relying on aboutToQuit: under the
        // GNOME extension the process is hidden, not quit, for its whole
        // lifetime, and an external SIGTERM (session end, crash, or just
        // redeploying during development) skips Qt's normal shutdown path
        // entirely, losing anything KConfig hadn't synced yet.
        m_config->sync();
    });
    // Live preview from the theme picker popup (hover/keyboard-focus):
    // apply visually without touching persisted config at all.
    // previewCanceled() (any path that ends preview -- mouse leave, Esc,
    // popup closing) re-applies whatever's actually persisted, so a
    // preview can never "stick" by accident.
    connect(m_editor, &EditorArea::themePreviewRequested, this, [this](const QString &key) {
        const Theme *theme = findTheme(key);
        m_editor->applyTheme(theme);
        m_sidebar->applyTheme(theme);
    });
    connect(m_editor, &EditorArea::themePreviewCanceled, this, [this] {
        const Theme *theme = findTheme(m_config->themeKey());
        m_editor->applyTheme(theme);
        m_sidebar->applyTheme(theme);
    });

    const QString savedThemeKey = m_config->themeKey();
    m_editor->applyTheme(findTheme(savedThemeKey));
    m_sidebar->applyTheme(findTheme(savedThemeKey));
    m_editor->setThemeSelection(savedThemeKey);

    m_revealAnimation = new QVariantAnimation(this);
    connect(m_revealAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setRevealMask(value.toInt());
    });
}

void SlideWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensurePlatformConfigured();
    updateSidebarGeometry();
    m_sidebar->raise();
}

void SlideWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateSidebarGeometry();
}

void SlideWindow::updateSidebarGeometry()
{
    const int top = m_editor->toolbarHeight();
    m_sidebar->setGeometry(0, top, m_sidebar->width(), m_card->height() - top);
}

// Clips this top-level window to a plain rectangle revealWidth wide,
// anchored at its own right (docked) edge -- used by slideIn()/slideOut()
// to "unfurl"/"retract" the window in place instead of translating its
// position across the screen. See slideIn()'s doc comment for why: a
// position-based slide used to start from just off the target screen's
// right edge, but on a multi-monitor setup with a display extending that
// edge, "just off screen" actually lands ON the neighboring monitor,
// making the window visibly cross it during the animation. A width-only
// reveal never leaves the target screen's bounds, since the window's
// real position/size are already final the moment it's shown -- only how
// much of it is currently clipped visible changes. Never called with 0:
// Qt's setMask() special-cases a fully empty QRegion as "clear the mask"
// (i.e. fully visible) rather than "fully hidden", so revealWidth is
// clamped to at least 1 physical pixel -- imperceptible, and side-steps
// that gotcha entirely.
void SlideWindow::setRevealMask(int revealWidth)
{
    const int w = width();
    m_revealWidth = qBound(1, revealWidth, w);
    if (m_revealWidth >= w) {
        // Full reveal is the steady state the window sits in almost all
        // the time -- clearMask() rather than a mask matching the exact
        // full rect, so idle repaints don't pay for a no-op clip.
        clearMask();
        return;
    }
    setMask(QRect(w - m_revealWidth, 0, m_revealWidth, height()));
}

void SlideWindow::ensurePlatformConfigured()
{
    if (m_platformConfigured) {
        return;
    }
    m_platformConfigured = true;

#ifdef Q_OS_WIN
    configureWindows();
#else
    m_isWayland = QGuiApplication::platformName() == QLatin1String("wayland");
    m_externallyManaged = qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("GNOME"));
    if (m_isWayland) {
        configureWayland();
    } else {
        configureX11();
    }
#endif
}

#ifdef Q_OS_WIN
void SlideWindow::configureWindows()
{
    // Qt::Tool (rather than a plain top-level) is what keeps this out of
    // the taskbar and the Alt-Tab switcher -- WS_EX_TOOLWINDOW under the
    // hood, the same exclusion X11's NET::SkipTaskbar achieves via a
    // different mechanism. HWND_TOPMOST (applied per-call in slideIn(),
    // since it's a z-order request rather than a persistent window
    // style) is the Win32 equivalent of X11's NET::KeepAbove.
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | windowFlags());
    // Rounded corners come from the same RoundedCornersEffect every
    // platform uses (see that class's doc comment) -- an earlier attempt
    // at using DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE) here
    // rounded the wrong rectangle: this top-level window is deliberately
    // larger than the visible card (kShadowMargin's padding, reserved for
    // the drop shadow to render into), so DWM ended up rounding that
    // oversized, mostly-transparent outer boundary instead of the actual
    // content area, leaving a visible square-cornered card floating
    // inside a faintly-visible rounded, mostly-empty frame.
}
#else
void SlideWindow::configureX11()
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | windowFlags());

    const WId id = winId();
    KX11Extras::setType(id, NET::Utility);
    KX11Extras::setState(id, NET::SkipTaskbar | NET::SkipPager | NET::KeepAbove);
    KX11Extras::setOnAllDesktops(id, true);
}

void SlideWindow::configureWayland()
{
    setWindowFlags(Qt::FramelessWindowHint);

    if (m_externallyManaged) {
        return;
    }
#ifdef HAVE_LAYERSHELLQT
    QWindow *handle = windowHandle();
    if (!handle) {
        winId();
        handle = windowHandle();
    }
    if (!handle) {
        return;
    }

    m_layerWindow = LayerShellQt::Window::get(handle);
    if (!m_layerWindow) {
        return;
    }

    m_layerWindow->setScope(QStringLiteral("mdnote"));
    m_layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
    m_layerWindow->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop
                                                             | LayerShellQt::Window::AnchorBottom
                                                             | LayerShellQt::Window::AnchorRight));
    m_layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
    m_layerWindow->setExclusiveZone(0);
    // Anchoring both Top and Bottom tells the compositor to stretch the
    // surface to fill the entire space between those two edges --
    // setDesiredSize()'s height is not consulted at all once both
    // opposing anchors on an axis are set (this is standard wlr-layer-
    // shell behavior, not a KWin quirk), which is why trying to shrink
    // the window's own height to get a visible top/bottom gap had no
    // effect under native Wayland specifically. setMargins() is the
    // actual protocol-level way to inset an anchored surface from its
    // anchored edges.
    m_layerWindow->setMargins(QMargins(0, kVerticalScreenMargin, 0, kVerticalScreenMargin));

    const QRect geo = targetGeometry();
    m_layerWindow->setDesiredSize(geo.size());
    resize(geo.size());
#endif
}
#endif

QRect SlideWindow::targetGeometry(const QScreen *screen) const
{
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return QRect(0, 0, 800, 600);
    }
    const QRect avail = screen->availableGeometry();
    const int width = qRound(avail.width() * m_config->widthRatio());
#ifdef Q_OS_WIN
    const int verticalMargin = kVerticalScreenMarginWindows;
#else
    // Under GNOME this value is computed but never actually consulted
    // (that extension owns geometry independently -- see slideIn()'s
    // m_externallyManaged branch); under native Wayland, the real fix is
    // configureWayland()'s setMargins() call, since setDesiredSize()'s
    // height is ignored there once both Top and Bottom are anchored --
    // this return value's height still needs to match it for X11, whose
    // window manager (unlike layer-shell) does respect a plain resize.
    const int verticalMargin = kVerticalScreenMargin;
#endif
    const int height = qMax(100, avail.height() - 2 * verticalMargin);
    return QRect(avail.right() - width + 1, avail.top() + verticalMargin, width, height);
}

void SlideWindow::toggle()
{
    if (m_isOpen) {
        slideOut();
    } else {
        slideIn();
    }
}

void SlideWindow::slideIn()
{
    m_isOpen = true;

    if (!isVisible()) {
        ensurePlatformConfigured();
    }

#ifdef Q_OS_WIN
    // Windows places no restriction on a client freely moving its own
    // top-level window (unlike Wayland), so geometry is settled up front
    // -- on whichever screen the pointer is currently on, matching the
    // GNOME extension's own follow-the-pointer monitor choice (see
    // screenUnderCursor()) -- and only the reveal mask animates, rather
    // than the window's position. See setRevealMask()'s doc comment for
    // why: the previous approach set the window's start position just off
    // the target screen's right edge and animated it sliding in from
    // there, which visibly crossed any monitor extending past that edge.
    const QRect target = targetGeometry(screenUnderCursor());
    setGeometry(target);
    setRevealMask(1);
    show();

    disconnect(m_revealAnimation, &QVariantAnimation::finished, this, nullptr);
    m_revealAnimation->stop();
    m_revealAnimation->setDuration(m_config->animationDurationMs());
    m_revealAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_revealAnimation->setStartValue(1);
    m_revealAnimation->setEndValue(target.width());
    m_revealAnimation->start();

    // SetWindowPos with HWND_TOPMOST is the Win32 always-on-top
    // primitive -- Qt::WindowStaysOnTopHint alone is honored inconsistently
    // once a window has already been shown, so this is reapplied on
    // every slideIn() rather than relied on as a one-time window flag.
    SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
#else
    if (m_externallyManaged) {
        // GNOME/mutter: the mdnote-quake Shell extension owns geometry,
        // monitor placement and the slide animation entirely from
        // outside this process (see gnome-extension/quake-mode.js). Just
        // map the window -- calling setFixedSize()/resize() here too
        // would fight the extension's move_resize_frame() calls over
        // the same window, which is exactly what caused the window to
        // settle at coordinates matching neither side's own math.
        show();
    } else if (m_isWayland) {
        // The layer-shell backed platform window doesn't support
        // QWidget::setWindowOpacity() (KWin logs "This plugin does not
        // support setting window opacity" and the surface just stays
        // at whatever alpha it last had), so there's no cheap fade
        // here -- just map it directly.
        //
        // setFixedSize() rather than plain resize(): a bare resize() is
        // only ever an advisory hint the compositor is free to ignore,
        // and fixed min==max size is a firmer signal that's more likely
        // to be honored. Harmless when layer-shell is working since
        // LayerShellQt's own anchors/setDesiredSize control the real
        // surface size regardless.
        const QRect target = targetGeometry();
        setFixedSize(target.size());
        show();
    } else {
        // Real X11 slide -- same reveal-mask technique and pointer-
        // follow monitor choice as the Windows branch above (see its
        // comment); X11 has the identical multi-monitor bleed potential
        // a plain position-slide would have, since it's the same
        // technique either way.
        const QRect target = targetGeometry(screenUnderCursor());
        setGeometry(target);
        setRevealMask(1);
        show();

        disconnect(m_revealAnimation, &QVariantAnimation::finished, this, nullptr);
        m_revealAnimation->stop();
        m_revealAnimation->setDuration(m_config->animationDurationMs());
        m_revealAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_revealAnimation->setStartValue(1);
        m_revealAnimation->setEndValue(target.width());
        m_revealAnimation->start();
    }

    if (!m_isWayland) {
        KX11Extras::forceActiveWindow(winId());
    }
#endif
    raise();
    activateWindow();
}

void SlideWindow::slideOut()
{
    m_isOpen = false;

#ifdef Q_OS_WIN
    disconnect(m_revealAnimation, &QVariantAnimation::finished, this, nullptr);
    m_revealAnimation->stop();
    m_revealAnimation->setDuration(m_config->animationDurationMs());
    m_revealAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_revealAnimation->setStartValue(m_revealWidth);
    m_revealAnimation->setEndValue(1);
    connect(m_revealAnimation, &QVariantAnimation::finished, this, [this] { hide(); });
    m_revealAnimation->start();
#else
    if (m_isWayland) {
        hide();
    } else {
        disconnect(m_revealAnimation, &QVariantAnimation::finished, this, nullptr);
        m_revealAnimation->stop();
        m_revealAnimation->setDuration(m_config->animationDurationMs());
        m_revealAnimation->setEasingCurve(QEasingCurve::InCubic);
        m_revealAnimation->setStartValue(m_revealWidth);
        m_revealAnimation->setEndValue(1);
        connect(m_revealAnimation, &QVariantAnimation::finished, this, [this] { hide(); });
        m_revealAnimation->start();
    }
#endif
}

void SlideWindow::openFileAndShow(const QString &filePath)
{
    m_editor->openFile(filePath);
    m_sidebar->selectFile(filePath);
    m_config->noteOpened(filePath);
}

void SlideWindow::promptChangeFolder()
{
    QFileDialog dialog(this, tr("Choose Notes Folder"), m_fileManager->currentFolder());
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    // Two of this dialog's built-in labels never come out translated:
    // Qt's own QFileDialog source strings are "&Look in:" and "Files of
    // &type:" (with mnemonic ampersands), but this Qt build's bundled
    // qtbase_zh_CN.qm only has entries for the ampersand-less "Look in:"
    // and "Files of type:" -- an upstream mismatch between the shipped
    // translation catalog and the actual compiled dialog strings in this
    // Qt version, nothing mdnote's own translation setup can reach.
    // Overriding the two labels directly with mdnote's own tr() strings
    // (translated via mdnote's own .ts files, which don't have this
    // mismatch) works around it. Relies on QFileDialog's internal object
    // names (lookInLabel/fileTypeLabel), which aren't official API but
    // have been stable across Qt versions for a long time.
    if (auto *label = dialog.findChild<QLabel *>(QStringLiteral("lookInLabel"))) {
        label->setText(tr("Look in:"));
    }
    if (auto *label = dialog.findChild<QLabel *>(QStringLiteral("fileTypeLabel"))) {
        label->setText(tr("Files of type:"));
    }
    pinDialogAboveSlideWindow(&dialog);
    const int result = dialog.exec();
    if (result != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }
    const QString dir = dialog.selectedFiles().first();
    m_config->setDefaultFolder(dir);
    m_fileManager->setCurrentFolder(dir);
}

void SlideWindow::createNewNote()
{
    const QString path = m_fileManager->newNotePath();
    m_fileManager->save(path, QString());
    m_editor->newFile(path);
    m_sidebar->refresh();
    m_sidebar->startRename(path);
    m_config->noteOpened(path);
}

void SlideWindow::openInitialNote()
{
    const auto notes = m_fileManager->listNotes(); // newest-modified first
    if (!notes.isEmpty()) {
        m_editor->openFile(notes.first().filePath);
        m_sidebar->selectFile(notes.first().filePath);
        m_config->noteOpened(notes.first().filePath);
        return;
    }
    // Empty folder: create (and immediately save) a fresh note instead
    // of leaving the editor on an unnamed document that save() has no
    // path to write to.
    const QString path = m_fileManager->newNotePath();
    m_fileManager->save(path, QString());
    m_editor->newFile(path);
    m_sidebar->refresh();
}
