#pragma once

#include <QWidget>

class QPropertyAnimation;
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
//  - X11: real geometry slide-in/out (KX11Extras for keep-above /
//    skip-taskbar / all-desktops), exactly like Yakuake.
//  - Wayland (KWin): pinned via LayerShellQt (anchored top+bottom+right,
//    overlay layer) since layer-shell surfaces can't be freely
//    repositioned either; show/hide is an opacity fade instead of a
//    geometry slide.
//  - Windows: no such restriction -- a real geometry slide-in/out like
//    X11's, using Win32 SetWindowPos()'s HWND_TOPMOST/WS_EX_TOOLWINDOW
//    in place of KX11Extras.
class SlideWindow : public QWidget
{
    Q_OBJECT

public:
    SlideWindow(FileManager *fileManager, ConfigManager *config, QWidget *parent = nullptr);

public Q_SLOTS:
    void toggle();

protected:
    bool event(QEvent *event) override;
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
    QRect targetGeometry() const;
    void slideIn();
    void slideOut();
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
    // see RoundedCard in SlideWindow.cpp) which in turn contains m_editor
    // and m_sidebar. Split across two widgets because a widget's own
    // setMask() clip and a QGraphicsEffect applied to it both operate on
    // the same rendered buffer -- keeping them on separate widgets avoids
    // the mask clipping away the shadow's own blur bleed.
    QWidget *m_shadowWrapper = nullptr;
    QWidget *m_card = nullptr;

    QPropertyAnimation *m_geometryAnimation = nullptr;

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
