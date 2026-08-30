#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <KSharedConfig>

// Central place for all user-facing settings, backed by KConfig
// (~/.config/mdnoterc). Keep this the single source of truth for
// anything the user can tweak; SlideWindow/Sidebar/EditorArea just
// read/write through here rather than touching KConfig directly.
class ConfigManager : public QObject
{
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);

    double widthRatio() const;
    void setWidthRatio(double ratio);

    int animationDurationMs() const;
    void setAnimationDurationMs(int ms);

    bool hideOnFocusLost() const;
    void setHideOnFocusLost(bool hide);

    QString defaultFolder() const;
    void setDefaultFolder(const QString &path);

    QStringList recentFolders() const;
    void addRecentFolder(const QString &path);

    QString lastMode() const; // "source" or "normal"
    void setLastMode(const QString &mode);

    // Empty string means "system default" (no color override applied).
    QString themeKey() const;
    void setThemeKey(const QString &key);

    bool sidebarOpen() const;
    void setSidebarOpen(bool open);

    // "name" or "mtime".
    QString sidebarSortKey() const;
    void setSidebarSortKey(const QString &key);
    bool sidebarSortAscending() const;
    void setSidebarSortAscending(bool ascending);

    // Ordered, most-recent/most-recently-starred first. Persist across
    // directories -- these are cross-folder lists, not tied to
    // currentFolder() or defaultFolder().
    QStringList starredPaths() const;
    void setStarredPaths(const QStringList &paths);

    QStringList recentPaths() const;
    // Moves path to the front, trims to the cap, and persists.
    void noteOpened(const QString &path);

    void sync();

Q_SIGNALS:
    void widthRatioChanged(double ratio);
    void defaultFolderChanged(const QString &path);

private:
    KSharedConfigPtr m_config;
};
