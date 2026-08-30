#pragma once

#include <QObject>

class QAction;

// Registers the system-wide F10 toggle via kglobalaccel so it fires
// even when mdnote has no focus (works on both X11 and Wayland KWin
// sessions, unlike a plain in-app QShortcut).
class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyManager(QObject *parent = nullptr);

Q_SIGNALS:
    void toggleRequested();

private:
    QAction *m_toggleAction = nullptr;
};
