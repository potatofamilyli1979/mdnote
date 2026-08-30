#include "HotkeyManager.h"

#include <KGlobalAccel>
#include <QAction>
#include <QKeySequence>

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    // kglobalaccel is a KDE-only D-Bus service, present only when Plasma's
    // own kglobalaccel daemon is running -- allowlisted on "KDE" rather
    // than denylisted on just "GNOME", since attempting registration
    // with no such service running is worse than simply not having the
    // shortcut: each of KGlobalAccel::self()'s D-Bus calls blocks on a
    // service-activation attempt that takes on the order of 10+ seconds
    // to give up, and that risk applies identically under XFCE, LXQt,
    // MATE, or anything else that isn't Plasma, not just GNOME
    // specifically. Under GNOME, the mdnote-quake GNOME Shell extension
    // owns F10 entirely from outside this process instead (see
    // gnome-extension/).
    if (!qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"))) {
        return;
    }

    m_toggleAction = new QAction(tr("Toggle mdnote"), this);
    // objectName must stay stable across runs: kglobalaccel keys the
    // stored shortcut on componentName + this identifier.
    m_toggleAction->setObjectName(QStringLiteral("toggle_mdnote"));

    KGlobalAccel::self()->setDefaultShortcut(m_toggleAction, {QKeySequence(Qt::Key_F10)});
    KGlobalAccel::self()->setShortcut(m_toggleAction, {QKeySequence(Qt::Key_F10)});

    connect(m_toggleAction, &QAction::triggered, this, &HotkeyManager::toggleRequested);
}
