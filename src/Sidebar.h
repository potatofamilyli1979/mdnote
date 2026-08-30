#pragma once

#include <QWidget>
#include <QStringList>

struct Theme;
class SidebarListModel;
class SidebarFilterProxyModel;
class SidebarItemDelegate;

class QListView;
class QLineEdit;
class QLabel;
class QToolButton;
class QPushButton;
class QButtonGroup;
class QModelIndex;
class FileManager;
class ConfigManager;
class OverlayScrollBar;

// Left-hand panel inside the slide-out window: search + all/recent/
// starred tabs + a two-line document list + a folder/new-note action bar.
// Floats over the editor (doesn't share layout space / doesn't reflow
// it) -- positioned/raised manually by SlideWindow.
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    enum class Tab
    {
        All,
        Recent,
        Starred,
    };

    Sidebar(FileManager *fileManager, ConfigManager *config, QWidget *parent = nullptr);

    void refresh();
    void selectFile(const QString &filePath);
    // Selects the item for filePath and puts it into inline rename
    // editing, for the "new note" flow where the user should be able
    // to type a name right away.
    void startRename(const QString &filePath);
    // theme == nullptr applies the fixed black/white default look (see
    // Theme::defaultFlatTheme()).
    void applyTheme(const Theme *theme);

    // For SlideWindow's "/", Ctrl+N, Ctrl+O shortcuts and click-outside
    // handling.
    void focusSearch();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void fileActivated(const QString &filePath);
    void changeFolderRequested();
    void newNoteRequested();
    void fileRenamed(const QString &oldPath, const QString &newPath);
    void fileDeleted(const QString &filePath);
    void closeRequested();

private:
    void setActiveTab(Tab tab);
    void rebuildRows();
    void updateEmptyState();
    void onActivated(const QModelIndex &proxyIndex);
    void onRenameRequested(const QString &filePath, const QString &newName);
    void showContextMenu(const QPoint &pos);
    void deleteFile(const QString &filePath, const QString &displayName);
    void toggleStarred(const QString &filePath);
    bool isStarred(const QString &filePath) const;
    void toggleSortAscending();
    void showSortMenu();
    void resizeEvent(QResizeEvent *event) override;

    FileManager *m_fileManager;
    ConfigManager *m_config;

    QLineEdit *m_searchEdit;
    QToolButton *m_tabAll;
    QToolButton *m_tabRecent;
    QToolButton *m_tabStarred;
    QButtonGroup *m_tabGroup;
    QLabel *m_pathLabel;
    QLabel *m_sortLabel;
    QListView *m_list;
    OverlayScrollBar *m_listScrollBar;
    SidebarListModel *m_model;
    SidebarFilterProxyModel *m_proxyModel;
    SidebarItemDelegate *m_delegate;
    QLabel *m_emptyLabel;
    QPushButton *m_changeFolderButton;
    QPushButton *m_newNoteButton;

    Tab m_activeTab = Tab::All;
    QString m_currentDocPath;
    bool m_editingItem = false;
    QStringList m_starredPaths;
    bool m_isDarkTheme = false;
};
