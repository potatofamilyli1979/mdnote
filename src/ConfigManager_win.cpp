#include "ConfigManager.h"

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
    , m_config(new QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("mdnote"), QStringLiteral("mdnote"), this))
{
    if (defaultFolder().isEmpty()) {
        const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString fallback = docs.isEmpty() ? QDir::homePath() : docs;
        setDefaultFolder(QDir(fallback).filePath(QStringLiteral("Notes")));
    }
}

double ConfigManager::widthRatio() const
{
    return m_config->value(QStringLiteral("General/widthRatio"), kDefaultWidthRatio).toDouble();
}

void ConfigManager::setWidthRatio(double ratio)
{
    ratio = qBound(0.2, ratio, 0.9);
    m_config->setValue(QStringLiteral("General/widthRatio"), ratio);
    Q_EMIT widthRatioChanged(ratio);
}

int ConfigManager::animationDurationMs() const
{
    return m_config->value(QStringLiteral("General/animationDurationMs"), kDefaultAnimationMs).toInt();
}

void ConfigManager::setAnimationDurationMs(int ms)
{
    m_config->setValue(QStringLiteral("General/animationDurationMs"), ms);
}

bool ConfigManager::hideOnFocusLost() const
{
    return m_config->value(QStringLiteral("General/hideOnFocusLost"), false).toBool();
}

void ConfigManager::setHideOnFocusLost(bool hide)
{
    m_config->setValue(QStringLiteral("General/hideOnFocusLost"), hide);
}

QString ConfigManager::defaultFolder() const
{
    return m_config->value(QStringLiteral("Folders/defaultFolder"), QString()).toString();
}

void ConfigManager::setDefaultFolder(const QString &path)
{
    QDir().mkpath(path);
    m_config->setValue(QStringLiteral("Folders/defaultFolder"), path);
    addRecentFolder(path);
    Q_EMIT defaultFolderChanged(path);
}

QStringList ConfigManager::recentFolders() const
{
    return m_config->value(QStringLiteral("Folders/recentFolders"), QStringList()).toStringList();
}

void ConfigManager::addRecentFolder(const QString &path)
{
    QStringList list = recentFolders();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecentFolders) {
        list.removeLast();
    }
    m_config->setValue(QStringLiteral("Folders/recentFolders"), list);
}

QString ConfigManager::lastMode() const
{
    return m_config->value(QStringLiteral("General/lastMode"), QStringLiteral("normal")).toString();
}

void ConfigManager::setLastMode(const QString &mode)
{
    m_config->setValue(QStringLiteral("General/lastMode"), mode);
}

QString ConfigManager::themeKey() const
{
    return m_config->value(QStringLiteral("General/themeKey"), QString()).toString();
}

void ConfigManager::setThemeKey(const QString &key)
{
    m_config->setValue(QStringLiteral("General/themeKey"), key);
}

bool ConfigManager::sidebarOpen() const
{
    return m_config->value(QStringLiteral("Sidebar/open"), false).toBool();
}

void ConfigManager::setSidebarOpen(bool open)
{
    m_config->setValue(QStringLiteral("Sidebar/open"), open);
}

QString ConfigManager::sidebarSortKey() const
{
    return m_config->value(QStringLiteral("Sidebar/sortKey"), QStringLiteral("mtime")).toString();
}

void ConfigManager::setSidebarSortKey(const QString &key)
{
    m_config->setValue(QStringLiteral("Sidebar/sortKey"), key);
}

bool ConfigManager::sidebarSortAscending() const
{
    return m_config->value(QStringLiteral("Sidebar/sortAscending"), false).toBool();
}

void ConfigManager::setSidebarSortAscending(bool ascending)
{
    m_config->setValue(QStringLiteral("Sidebar/sortAscending"), ascending);
}

QStringList ConfigManager::starredPaths() const
{
    return m_config->value(QStringLiteral("Sidebar/starredPaths"), QStringList()).toStringList();
}

void ConfigManager::setStarredPaths(const QStringList &paths)
{
    m_config->setValue(QStringLiteral("Sidebar/starredPaths"), paths);
    // See the Linux implementation's comment: starring is a deliberate,
    // low-frequency action worth flushing immediately rather than
    // risking it to an ungraceful process end.
    m_config->sync();
}

QStringList ConfigManager::recentPaths() const
{
    return m_config->value(QStringLiteral("Sidebar/recentPaths"), QStringList()).toStringList();
}

void ConfigManager::noteOpened(const QString &path)
{
    QStringList list = recentPaths();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecentNotes) {
        list.removeLast();
    }
    m_config->setValue(QStringLiteral("Sidebar/recentPaths"), list);
}

void ConfigManager::sync()
{
    m_config->sync();
}
