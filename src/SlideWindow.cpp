#include "SlideWindow.h"
#include "Sidebar.h"
#include "EditorArea.h"
#include "FileManager.h"
#include "ConfigManager.h"
#include "DialogUtils.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QFileDialog>
#include <QShowEvent>
#include <QResizeEvent>
#include <QPolygon>
#include <QRegion>
#include <QGraphicsDropShadowEffect>
#include <QtMath>
#include <cmath>

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
#include <dwmapi.h>
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

// The visible "card": rounds its left (open) edge while leaving the
// right edge -- flush against the screen edge -- square. setMask() only
// works reliably here because this is a *child* widget: Qt clips a
// child's own compositing into its parent entirely in software, with no
// window-system shape protocol involved. Masking the top-level window
// directly does not work under this externally (GNOME-extension-)
// managed setup, which has no window-shaping protocol support.
class RoundedCard : public QWidget
{
public:
    using QWidget::QWidget;

    // Split out of resizeEvent() so it can also be re-run when
    // something *other* than a resize clears the mask -- a native
    // QStyle's dialog-teardown style-polishing path can clear a child
    // widget's mask as a side effect. SlideWindow re-invokes this on
    // QEvent::WindowActivate so it self-heals after any dialog closes.
    void applyMask()
    {
#ifdef Q_OS_WIN
        // Windows gets real, compositor-antialiased rounded corners from
        // DWM instead (see configureWindows()) -- a pixel-mask on top of
        // that would double up with (and look worse than) what DWM
        // already draws on the actual top-level frame.
        return;
#endif
        if (width() <= 0 || height() <= 0) {
            return;
        }
        constexpr int kRadius = 20;
        // QRegion (what setMask() ultimately needs) has no anti-aliasing --
        // every edge is a hard, binary pixel boundary, arc or not. What
        // *is* controllable is how finely the arc gets approximated by
        // straight segments before rasterizing: going through
        // QPainterPath::arcTo() + toFillPolygon() uses Qt's default
        // Bezier-flattening tolerance, which is too coarse at this 20px
        // radius to look round. Sampling the actual circle with many
        // points directly gives much finer control over that tradeoff.
        // setMask()'s QPolygon is built and rounded in *logical* pixels,
        // though, so the same point count looks coarser the more each
        // logical pixel expands into physical ones -- scaling the sample
        // count by devicePixelRatioF() keeps each step under a physical
        // pixel on a scaled-up (100% isn't the only common setting on
        // Windows, where 125%/150% are routine) display, not just at 1:1.
        const int kArcSteps = qRound(48 * devicePixelRatioF());
        const QRectF r(rect());

        QPolygon polygon;
        auto addArc = [&](qreal cx, qreal cy, qreal startDeg, qreal sweepDeg) {
            for (int i = 0; i <= kArcSteps; ++i) {
                const qreal deg = startDeg + sweepDeg * (qreal(i) / kArcSteps);
                const qreal rad = qDegreesToRadians(deg);
                polygon << QPoint(qRound(cx + kRadius * std::cos(rad)), qRound(cy - kRadius * std::sin(rad)));
            }
        };
        polygon << QPoint(qRound(r.right()), qRound(r.top()));
        addArc(r.left() + kRadius, r.top() + kRadius, 90, 90);
        addArc(r.left() + kRadius, r.bottom() - kRadius, 180, 90);
        polygon << QPoint(qRound(r.right()), qRound(r.bottom()));

        setMask(QRegion(polygon));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        applyMask();
    }
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

    m_card = new RoundedCard(m_shadowWrapper);
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
            setGeometry(targetGeometry());
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

    m_geometryAnimation = new QPropertyAnimation(this, "geometry", this);
}

bool SlideWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate) {
        // Re-applies m_card's rounded-corner mask -- see RoundedCard::
        // applyMask()'s doc comment. Regaining activation (which
        // includes a modal QFileDialog closing) is broad enough to
        // self-heal after any dialog, not just the specific one this
        // was first noticed on, and cheap enough (a few dozen QPolygon
        // points) to not worry about it firing more often than
        // strictly necessary.
        static_cast<RoundedCard *>(m_card)->applyMask();
    }
    return QWidget::event(event);
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

    // setWindowFlags() above can recreate the native window, so winId()
    // is called fresh afterward to make sure this targets the actual
    // final HWND, not one about to be torn down.
    const HWND hwnd = reinterpret_cast<HWND>(winId());

    // DWMWA_WINDOW_CORNER_PREFERENCE/DWM_WINDOW_CORNER_PREFERENCE were
    // only added to the Windows 11 SDK -- defined manually (matching
    // Microsoft's own published values) rather than depending on
    // <dwmapi.h> being new enough, since MinGW distributions commonly
    // bundle an older one. DwmSetWindowAttribute() simply returns an
    // error (silently ignored here) on Windows 10, where there's no
    // compositor-level rounding to opt into -- the window just stays
    // square there, same as it always has.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
    constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
#endif
    constexpr DWORD DWMWCP_ROUND = 2;
    const DWORD preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
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

QRect SlideWindow::targetGeometry() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return QRect(0, 0, 800, 600);
    }
    const QRect avail = screen->availableGeometry();
    const int width = qRound(avail.width() * m_config->widthRatio());
    // Under GNOME this value is computed but never actually consulted
    // (that extension owns geometry independently -- see slideIn()'s
    // m_externallyManaged branch); under native Wayland, the real fix is
    // configureWayland()'s setMargins() call, since setDesiredSize()'s
    // height is ignored there once both Top and Bottom are anchored --
    // this return value's height still needs to match it for X11, whose
    // window manager (unlike layer-shell) does respect a plain resize.
    const int height = qMax(100, avail.height() - 2 * kVerticalScreenMargin);
    return QRect(avail.right() - width + 1, avail.top() + kVerticalScreenMargin, width, height);
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
    const QRect target = targetGeometry();

    if (!isVisible()) {
        ensurePlatformConfigured();
    }

#ifdef Q_OS_WIN
    // Windows places no restriction on a client freely moving its own
    // top-level window (unlike Wayland), so this is a real geometry
    // slide-in exactly like the X11 path below.
    QRect start = target;
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        start.moveLeft(screen->geometry().right() + 1);
    }
    setGeometry(start);
    show();
    disconnect(m_geometryAnimation, &QPropertyAnimation::finished, this, nullptr);
    m_geometryAnimation->stop();
    m_geometryAnimation->setDuration(m_config->animationDurationMs());
    m_geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_geometryAnimation->setStartValue(start);
    m_geometryAnimation->setEndValue(target);
    m_geometryAnimation->start();

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
        setFixedSize(target.size());
        show();
    } else {
        QRect start = target;
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            start.moveLeft(screen->geometry().right() + 1);
        }
        setGeometry(start);
        show();
        disconnect(m_geometryAnimation, &QPropertyAnimation::finished, this, nullptr);
        m_geometryAnimation->stop();
        m_geometryAnimation->setDuration(m_config->animationDurationMs());
        m_geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_geometryAnimation->setStartValue(start);
        m_geometryAnimation->setEndValue(target);
        m_geometryAnimation->start();
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
    QRect end = geometry();
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        end.moveLeft(screen->geometry().right() + 1);
    }
    disconnect(m_geometryAnimation, &QPropertyAnimation::finished, this, nullptr);
    m_geometryAnimation->stop();
    m_geometryAnimation->setDuration(m_config->animationDurationMs());
    m_geometryAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_geometryAnimation->setStartValue(geometry());
    m_geometryAnimation->setEndValue(end);
    connect(m_geometryAnimation, &QPropertyAnimation::finished, this, [this] { hide(); });
    m_geometryAnimation->start();
#else
    if (m_isWayland) {
        hide();
    } else {
        QRect end = geometry();
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            end.moveLeft(screen->geometry().right() + 1);
        }
        disconnect(m_geometryAnimation, &QPropertyAnimation::finished, this, nullptr);
        m_geometryAnimation->stop();
        m_geometryAnimation->setDuration(m_config->animationDurationMs());
        m_geometryAnimation->setEasingCurve(QEasingCurve::InCubic);
        m_geometryAnimation->setStartValue(geometry());
        m_geometryAnimation->setEndValue(end);
        connect(m_geometryAnimation, &QPropertyAnimation::finished, this, [this] { hide(); });
        m_geometryAnimation->start();
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
