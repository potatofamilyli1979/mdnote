#pragma once

#include <QWidget>

class QVariantAnimation;
class QScreen;
class Sidebar;
class EditorArea;
class FileManager;
class ConfigManager;

#ifdef HAVE_LAYERSHELLQT
namespace LayerShellQt { class Window; }
#endif

// The Yakuake-style top-level: frameless, always-on-top, pinned to the
// right screen edge, shown/hidden by HotkeyManager's F10 signal.
//
// Presentation differs by platform because Wayland gives clients no
// way to freely position a top-level window:
//  - X11 and Windows: geometry is settled on the target monitor (whichever
//    one the pointer is on) the moment the window is shown, and only a
//    reveal mask (see setRevealMask()) animates -- an "unfurl"/"retract"
//    at the docked right edge rather than a position slide, so the
//    window never visually crosses a neighboring monitor. KX11Extras
//    (keep-above/skip-taskbar/all-desktops) vs. Win32 SetWindowPos()'s
//    HWND_TOPMOST/WS_EX_TOOLWINDOW is the one remaining platform split.
//  - Wayland (KWin): pinned via LayerShellQt (anchored top+bottom+right,
//    overlay layer) since layer-shell surfaces can't be freely
//    repositioned either; show/hide is an instant toggle instead of an
//    animated transition (KWin's layer-shell plugin doesn't support
//    QWidget::setWindowOpacity(), so even a fade isn't available).
class SlideWindow : public QWidget
{
    Q_OBJECT

public:
    SlideWindow(FileManager *fileManager, ConfigManager *config, QWidget *parent = nullptr);

public Q_SLOTS:
    void toggle();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void ensurePlatformConfigured();
#ifdef Q_OS_WIN
    void configureWindows();
#else
    void configureX11();
    void configureWayland();
#endif
    // screen is the monitor to size/position against; null defaults to
    // the primary screen (used by the platforms/paths -- GNOME, native
    // Wayland -- that pick their own monitor independently of this).
    QRect targetGeometry(const QScreen *screen = nullptr) const;
    void slideIn();
    void slideOut();
    void setRevealMask(int revealWidth);
    void openFileAndShow(const QString &filePath);
    void promptChangeFolder();
    void createNewNote();
    // Called once at startup so the editor never sits on an unnamed,
    // unsaveable document: opens the most recently edited note in the
    // folder, or creates a fresh (already-saved-to-disk) one if the
    // folder is empty.
    void openInitialNote();
    void updateSidebarGeometry();

    FileManager *m_fileManager;
    ConfigManager *m_config;
    Sidebar *m_sidebar;
    EditorArea *m_editor;
    // m_shadowWrapper (this window's sole child, carries the drop shadow
    // effect) contains m_card (the rounded-left-corner clipped surface,
    // see RoundedCornersEffect in SlideWindow.cpp) which in turn contains
    // m_editor and m_sidebar. Split across two widgets because a widget's
    // own QGraphicsEffect and one applied to it by a parent both operate
    // on the same rendered buffer -- keeping them on separate widgets
    // means the corner-clip's effect runs first and its already-rounded
    // result becomes the shadow effect's own source, rather than the two
    // fighting over one buffer.
    QWidget *m_shadowWrapper = nullptr;
    QWidget *m_card = nullptr;

    QVariantAnimation *m_revealAnimation = nullptr;
    // Last width (in px) applied by setRevealMask() -- read back by
    // slideOut() as the animation's start value, so closing always eases
    // from wherever the window is currently (visually) revealed, even if
    // it's mid-slideIn.
    int m_revealWidth = 0;

    bool m_platformConfigured = false;
    bool m_isOpen = false;
#ifndef Q_OS_WIN
    bool m_isWayland = false;
    // True under GNOME/mutter, where neither KGlobalAccel nor
    // LayerShellQt work: the mdnote-quake GNOME Shell extension (see
    // gnome-extension/) drives F10 and all window geometry/animation
    // itself from outside the process. slideIn()/configureWayland() must
    // not also call setFixedSize()/resize() here, or the two would fight
    // over the same window's geometry.
    bool m_externallyManaged = false;
#endif

#ifdef HAVE_LAYERSHELLQT
    LayerShellQt::Window *m_layerWindow = nullptr;
#endif
};
