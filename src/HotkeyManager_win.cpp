#include "HotkeyManager.h"

#include <QWidget>
#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QByteArray>

// See SlideWindow.cpp's windows.h include for why both of these matter
// -- guarded with #ifndef since Qt6's own CMake integration already
// defines both globally for anything linking Qt6::Core on Windows.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
constexpr int kHotkeyId = 1;

// Windows delivers WM_HOTKEY to whichever window RegisterHotKey() named
// as its target -- a native event filter is the Qt-native way to
// intercept that message before it reaches Qt's own event dispatching,
// since WM_HOTKEY has no corresponding QEvent of its own.
class HotkeyEventFilter : public QAbstractNativeEventFilter
{
public:
    explicit HotkeyEventFilter(HotkeyManager *owner)
        : m_owner(owner)
    {
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
    {
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
            return false;
        }
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == kHotkeyId) {
            Q_EMIT m_owner->toggleRequested();
            return true;
        }
        return false;
    }

private:
    HotkeyManager *m_owner;
};
}

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
    , m_hiddenWindow(new QWidget)
    , m_eventFilter(new HotkeyEventFilter(this))
{
    // winId() forces creation of the real native window RegisterHotKey()
    // needs as a target -- the widget itself is never shown.
    m_hiddenWindow->winId();
    qApp->installNativeEventFilter(m_eventFilter);

    const HWND hwnd = reinterpret_cast<HWND>(m_hiddenWindow->winId());
    // MOD_NOREPEAT: one toggleRequested() per physical key press, not
    // one per auto-repeat tick while F10 is held down.
    RegisterHotKey(hwnd, kHotkeyId, MOD_NOREPEAT, VK_F10);
}

HotkeyManager::~HotkeyManager()
{
    UnregisterHotKey(reinterpret_cast<HWND>(m_hiddenWindow->winId()), kHotkeyId);
    qApp->removeNativeEventFilter(m_eventFilter);
    delete m_eventFilter;
    delete m_hiddenWindow;
}
