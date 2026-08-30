#include <QApplication>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <KAboutData>
#include <KDBusService>

#include "ConfigManager.h"
#include "FileManager.h"
#include "HotkeyManager.h"
#include "SlideWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Deliberately no QApplication::setStyle() call -- this app leaves
    // the base QStyle to whatever the platform/qt6ct picks, everywhere,
    // GNOME included. (A GNOME-only Fusion override lived here for a
    // while, to fix QTextDocument's built-in task-list checkbox marker
    // rendering as an X instead of a checkmark under GNOME's GTK-
    // bridged platform theme -- removed on request: respecting the
    // user's own configured style matters more than that one glyph.)

    // This app's own strings are English (tr() source language), with
    // Simplified/Traditional Chinese translations compiled from
    // translations/mdnote_zh_{Hans,Hant}.ts and embedded as Qt resources
    // (see qt6_add_translations() in CMakeLists.txt) -- both loaded
    // against the desktop's actual locale so the UI language follows
    // whatever the system is set to, falling back to English (i.e. no
    // translator installed at all) for anything else. qtbase's own
    // translation covers a handful of menu items -- Cut/Copy/Paste/Undo/
    // Redo/Select All -- that come from QTextEdit::createStandardContextMenu()
    // rather than this app's own code, and would otherwise stay in
    // English regardless of the app's own language.
    const QLocale systemLocale = QLocale::system();
    QTranslator qtTranslator;
    if (qtTranslator.load(systemLocale, QStringLiteral("qtbase"), QStringLiteral("_"),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(&qtTranslator);
    }
    QTranslator appTranslator;
    if (appTranslator.load(systemLocale, QStringLiteral("mdnote"), QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        QApplication::installTranslator(&appTranslator);
    }

    app.setOrganizationName(QStringLiteral("mdnote"));
    app.setApplicationName(QStringLiteral("mdnote"));
    app.setOrganizationDomain(QStringLiteral("kde.org"));
    app.setQuitOnLastWindowClosed(false);
    // Must match the installed .desktop file's basename: xdg-desktop-portal
    // integration and, more importantly, KWin's global-shortcuts watcher
    // both resolve our app identity as org.kde.<componentName> by default,
    // so KDBusService below registers under that same name.
    app.setDesktopFileName(QStringLiteral("org.kde.mdnote"));

    KAboutData aboutData(QStringLiteral("mdnote"),
                          QStringLiteral("mdnote"),
                          QStringLiteral("0.1.0"));
    KAboutData::setApplicationData(aboutData);

    // Registers org.kde.mdnote on the session bus and handles single-
    // instance activation: a second launch forwards to activateRequested()
    // here and the duplicate process exits. KWin's global-shortcuts
    // daemon needs this well-known service name present to treat our
    // F10 registration as "live" -- without it the shortcut is accepted
    // but silently never delivered.
    KDBusService dbusService(KDBusService::Unique);

    auto *hotkeys = new HotkeyManager(&app);

    auto *config = new ConfigManager(&app);
    auto *fileManager = new FileManager(&app);
    auto *window = new SlideWindow(fileManager, config);

    QObject::connect(hotkeys, &HotkeyManager::toggleRequested, window, &SlideWindow::toggle);
    QObject::connect(&dbusService, &KDBusService::activateRequested, window, [window](const QStringList &, const QString &) {
        window->toggle();
    });

    QObject::connect(&app, &QApplication::aboutToQuit, config, &ConfigManager::sync);

    return app.exec();
}
