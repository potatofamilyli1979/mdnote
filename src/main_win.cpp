#include <QApplication>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QLocalServer>
#include <QLocalSocket>

#include "ConfigManager.h"
#include "FileManager.h"
#include "HotkeyManager.h"
#include "SlideWindow.h"

namespace
{
// Matches the single-instance server name a second launch connects to
// below -- arbitrary, just needs to be unique enough not to collide
// with another app's QLocalServer name.
const QString kSingleInstanceKey = QStringLiteral("mdnote-single-instance");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Deliberately no QApplication::setStyle() call -- respects
    // whatever style the user (or their theme) has configured, same
    // policy as the Linux build.

    // This app's own strings are English (tr() source language), with
    // Simplified/Traditional Chinese translations compiled from
    // translations/mdnote_zh_{Hans,Hant}.ts and embedded as Qt resources
    // (see qt6_add_translations() in CMakeLists.txt) -- both loaded
    // against the system locale so the UI language follows whatever
    // Windows is set to, falling back to English (i.e. no translator
    // installed at all) for anything else. qtbase's own translation
    // covers a handful of menu items -- Cut/Copy/Paste/Undo/Redo/Select
    // All -- that come from QTextEdit::createStandardContextMenu()
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
    app.setQuitOnLastWindowClosed(false);

    // Single-instance handling without KDBusService (no D-Bus on
    // Windows): try connecting to an already-running instance's local
    // server first. If one answers, this is a second launch -- ask it
    // to toggle and exit rather than starting a duplicate process.
    {
        QLocalSocket probe;
        probe.connectToServer(kSingleInstanceKey);
        if (probe.waitForConnected(200)) {
            probe.write("toggle");
            probe.waitForBytesWritten(200);
            return 0;
        }
    }
    // No existing instance answered -- this is the primary instance.
    // removeServer() first clears a stale socket left behind by a
    // previous instance that didn't shut down cleanly (crashed, killed
    // directly); listen() would otherwise fail against it.
    QLocalServer::removeServer(kSingleInstanceKey);
    QLocalServer singleInstanceServer;
    singleInstanceServer.listen(kSingleInstanceKey);

    auto *hotkeys = new HotkeyManager(&app);

    auto *config = new ConfigManager(&app);
    auto *fileManager = new FileManager(&app);
    auto *window = new SlideWindow(fileManager, config);

    QObject::connect(hotkeys, &HotkeyManager::toggleRequested, window, &SlideWindow::toggle);
    QObject::connect(&singleInstanceServer, &QLocalServer::newConnection, window, [&singleInstanceServer, window] {
        QLocalSocket *client = singleInstanceServer.nextPendingConnection();
        QObject::connect(client, &QLocalSocket::readyRead, window, [client, window] {
            client->readAll();
            window->toggle();
            client->disconnectFromServer();
        });
    });

    QObject::connect(&app, &QApplication::aboutToQuit, config, &ConfigManager::sync);

    return app.exec();
}
