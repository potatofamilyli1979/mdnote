#include "ConfigManager.h"

#include <KConfigGroup>
#include <QStandardPaths>
#include <QDir>

namespace
{
constexpr double kDefaultWidthRatio = 0.5;
constexpr int kDefaultAnimationMs = 220;
constexpr int kMaxRecentFolders = 8;
constexpr int kMaxRecentNotes = 20;
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("mdnoterc")))
{
    if (defaultFolder().isEmpty()) {
        const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString fallback = docs.isEmpty() ? QDir::homePath() : docs;
        setDefaultFolder(QDir(fallback).filePath(QStringLiteral("Notes")));
    }
}

double ConfigManager::widthRatio() const
{
    return m_config->group(QStringLiteral("General")).readEntry("widthRatio", kDefaultWidthRatio);
}

void ConfigManager::setWidthRatio(double ratio)
{
    ratio = qBound(0.2, ratio, 0.9);
    m_config->group(QStringLiteral("General")).writeEntry("widthRatio", ratio);
    Q_EMIT widthRatioChanged(ratio);
}

int ConfigManager::animationDurationMs() const
{
    return m_config->group(QStringLiteral("General")).readEntry("animationDurationMs", kDefaultAnimationMs);
}

void ConfigManager::setAnimationDurationMs(int ms)
{
    m_config->group(QStringLiteral("General")).writeEntry("animationDurationMs", ms);
}

bool ConfigManager::hideOnFocusLost() const
{
    return m_config->group(QStringLiteral("General")).readEntry("hideOnFocusLost", false);
}

void ConfigManager::setHideOnFocusLost(bool hide)
{
    m_config->group(QStringLiteral("General")).writeEntry("hideOnFocusLost", hide);
}

QString ConfigManager::defaultFolder() const
{
    return m_config->group(QStringLiteral("Folders")).readEntry("defaultFolder", QString());
}

void ConfigManager::setDefaultFolder(const QString &path)
{
    QDir().mkpath(path);
    m_config->group(QStringLiteral("Folders")).writeEntry("defaultFolder", path);
    addRecentFolder(path);
    Q_EMIT defaultFolderChanged(path);
}

QStringList ConfigManager::recentFolders() const
{
    return m_config->group(QStringLiteral("Folders")).readEntry("recentFolders", QStringList());
}

void ConfigManager::addRecentFolder(const QString &path)
{
    QStringList list = recentFolders();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecentFolders) {
        list.removeLast();
    }
    m_config->group(QStringLiteral("Folders")).writeEntry("recentFolders", list);
}

QString ConfigManager::lastMode() const
{
    return m_config->group(QStringLiteral("General")).readEntry("lastMode", QStringLiteral("normal"));
}

void ConfigManager::setLastMode(const QString &mode)
{
    m_config->group(QStringLiteral("General")).writeEntry("lastMode", mode);
}

QString ConfigManager::themeKey() const
{
    return m_config->group(QStringLiteral("General")).readEntry("themeKey", QString());
}

void ConfigManager::setThemeKey(const QString &key)
{
    m_config->group(QStringLiteral("General")).writeEntry("themeKey", key);
}

bool ConfigManager::sidebarOpen() const
{
    return m_config->group(QStringLiteral("Sidebar")).readEntry("open", false);
}

void ConfigManager::setSidebarOpen(bool open)
{
    m_config->group(QStringLiteral("Sidebar")).writeEntry("open", open);
}

QString ConfigManager::sidebarSortKey() const
{
    return m_config->group(QStringLiteral("Sidebar")).readEntry("sortKey", QStringLiteral("mtime"));
}

void ConfigManager::setSidebarSortKey(const QString &key)
{
    m_config->group(QStringLiteral("Sidebar")).writeEntry("sortKey", key);
}

bool ConfigManager::sidebarSortAscending() const
{
    return m_config->group(QStringLiteral("Sidebar")).readEntry("sortAscending", false);
}

void ConfigManager::setSidebarSortAscending(bool ascending)
{
    m_config->group(QStringLiteral("Sidebar")).writeEntry("sortAscending", ascending);
}

QStringList ConfigManager::starredPaths() const
{
    return m_config->group(QStringLiteral("Sidebar")).readEntry("starredPaths", QStringList());
}

void ConfigManager::setStarredPaths(const QStringList &paths)
{
    m_config->group(QStringLiteral("Sidebar")).writeEntry("starredPaths", paths);
    // Starring is a deliberate, low-frequency action worth not losing --
    // everything else relies on the aboutToQuit-triggered sync() in
    // main.cpp, which only fires on a clean Qt quit. This app spends
    // most of its life hidden rather than closed (F10 toggles
    // visibility, it doesn't quit), so an ungraceful process end
    // (killed directly, logged out from under it) between star actions
    // and an actual clean quit would otherwise silently drop whichever
    // stars were added since the last one.
    m_config->sync();
}

QStringList ConfigManager::recentPaths() const
{
    return m_config->group(QStringLiteral("Sidebar")).readEntry("recentPaths", QStringList());
}

void ConfigManager::noteOpened(const QString &path)
{
    QStringList list = recentPaths();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecentNotes) {
        list.removeLast();
    }
    m_config->group(QStringLiteral("Sidebar")).writeEntry("recentPaths", list);
}

void ConfigManager::sync()
{
    m_config->sync();
}
