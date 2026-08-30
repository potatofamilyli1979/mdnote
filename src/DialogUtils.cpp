#include "DialogUtils.h"

#include <QWidget>
#include <QGuiApplication>
#include <QScreen>

#include <KX11Extras>

#ifdef HAVE_LAYERSHELLQT
#include <LayerShellQt/window.h>
#endif

void pinDialogAboveSlideWindow(QWidget *dialog)
{
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        dialog->setWindowFlags(dialog->windowFlags() | Qt::WindowStaysOnTopHint);
        return;
    }

    // Under GNOME, mdnote's own main window is entirely externally
    // managed by the mdnote-quake GNOME Shell extension (see
    // SlideWindow::ensurePlatformConfigured()'s m_externallyManaged),
    // which never touches layer-shell for it. LayerShellQt::Window::get()
    // below always returns null for a plain dialog under GNOME/mutter
    // anyway (layer-shell is for panel-style surfaces a privileged
    // client assigns itself, not an ordinary app dialog) -- skip the
    // attempt entirely rather than force this dialog's native window
    // into existence early via winId() just to discover that, since a
    // second client surface appearing while the main window is both
    // translucent and shaped via child-widget masking corrupts the main
    // window's rounded corners. The dialog falls back to being an
    // ordinary decorated toplevel, positioned by mutter the same as any
    // other app's dialog.
    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("GNOME"))) {
        return;
    }

#ifdef HAVE_LAYERSHELLQT
    dialog->setWindowFlags(dialog->windowFlags() | Qt::FramelessWindowHint);

    dialog->winId(); // force native window creation before assigning the layer-shell role
    QWindow *handle = dialog->windowHandle();
    if (!handle) {
        dialog->setWindowFlags(dialog->windowFlags() & ~Qt::FramelessWindowHint);
        return;
    }

    auto *layerWindow = LayerShellQt::Window::get(handle);
    if (!layerWindow) {
        // Compositor doesn't support the layer-shell protocol. Without a
        // layer-shell role, none of the anchoring/sizing below ever runs
        // to compensate for the FramelessWindowHint set above -- left in
        // place, that's a dialog with no decoration *and* no placement
        // logic, positioned wherever the compositor happens to put an
        // undecorated surface it knows nothing else about. Undo the flag
        // and let it be an ordinary decorated toplevel instead, same as
        // skipping this function entirely.
        dialog->setWindowFlags(dialog->windowFlags() & ~Qt::FramelessWindowHint);
        return;
    }

    layerWindow->setScope(QStringLiteral("mdnote-dialog"));
    layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
    layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    layerWindow->setExclusiveZone(-1);

    // Layer-shell surfaces have no built-in "centered" placement the
    // way a normal toplevel dialog gets from the window manager --
    // anchoring all four edges and using symmetric margins as the gap
    // is the standard way to fake it.
    const QSize hint = dialog->sizeHint().expandedTo(dialog->minimumSizeHint());
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    const int marginX = qMax(0, (avail.width() - hint.width()) / 2);
    const int marginY = qMax(0, (avail.height() - hint.height()) / 2);

    layerWindow->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop
                                                           | LayerShellQt::Window::AnchorBottom
                                                           | LayerShellQt::Window::AnchorLeft
                                                           | LayerShellQt::Window::AnchorRight));
    layerWindow->setMargins(QMargins(marginX, marginY, marginX, marginY));
    layerWindow->setDesiredSize(hint);
    dialog->resize(hint);
#else
    Q_UNUSED(dialog);
#endif
}
