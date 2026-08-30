#pragma once

#include <QObject>

class QAction;
#ifdef Q_OS_WIN
class QWidget;
class QAbstractNativeEventFilter;
#endif

// Registers the system-wide F10 toggle so it fires even when mdnote has
// no focus (unlike a plain in-app QShortcut). Linux: kglobalaccel,
// works on both X11 and Wayland KWin sessions (see HotkeyManager.cpp).
// Windows: RegisterHotKey() against a hidden native window, delivered
// via a QAbstractNativeEventFilter (see HotkeyManager_win.cpp).
class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyManager(QObject *parent = nullptr);
#ifdef Q_OS_WIN
    ~HotkeyManager() override;
#endif

Q_SIGNALS:
    void toggleRequested();

private:
    QAction *m_toggleAction = nullptr;
#ifdef Q_OS_WIN
    // Never shown -- exists purely to give RegisterHotKey() a native
    // window handle to target, which WM_HOTKEY is then delivered to
    // regardless of visibility.
    QWidget *m_hiddenWindow = nullptr;
    QAbstractNativeEventFilter *m_eventFilter = nullptr;
#endif
};
