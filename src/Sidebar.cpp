#include "Sidebar.h"
#include "FileManager.h"
#include "ConfigManager.h"
#include "DialogUtils.h"
#include "Theme.h"
#include "IconGlyphs.h"
#include "SidebarListModel.h"
#include "SidebarFilterProxyModel.h"
#include "SidebarItemDelegate.h"
#include "OverlayScrollBar.h"

#include <QListView>
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QButtonGroup>
#include <QIcon>
#include <QColor>
#include <QSize>
#include <QFrame>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QTimer>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QFontMetrics>
#include <algorithm>

namespace
{
constexpr int kSearchDebounceMs = 120;

double luminance(const QColor &c)
{
    return (0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue()) / 255.0;
}

QString rgba(const QColor &c, double alpha)
{
    QColor a = c;
    a.setAlphaF(alpha);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(a.red()).arg(a.green()).arg(a.blue()).arg(a.alpha());
}

QString elideMiddleHome(const QString &path)
{
    const QString home = QDir::homePath();
    QString shortened = path;
    if (shortened.startsWith(home)) {
        shortened.replace(0, home.length(), QStringLiteral("~"));
    }
    return shortened;
}
}

Sidebar::Sidebar(FileManager *fileManager, ConfigManager *config, QWidget *parent)
    : QWidget(parent)
    , m_fileManager(fileManager)
    , m_config(config)
{
    setObjectName(QStringLiteral("sidebar"));
    // A literal pixel width, unlike the main window's own width (a
    // fraction of screen width, so it scales with the screen
    // automatically) -- reads as too narrow on a screen with a much
    // wider logical resolution. See uiChromeScale()'s doc comment.
    setFixedWidth(qRound(240 * uiChromeScale(this)));
    setAttribute(Qt::WA_StyledBackground, true);

    m_starredPaths = m_config->starredPaths();

    // -- Header: search, segmented tabs, path + sort row --------------

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("sidebarSearch"));
    m_searchEdit->addAction(makeGlyphIcon(Glyph::ZoomReset, QColor(255, 255, 255, 128)), QLineEdit::LeadingPosition);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(26);
    m_searchEdit->installEventFilter(this);

    m_tabAll = new QToolButton(this);
    m_tabAll->setText(tr("All"));
    m_tabRecent = new QToolButton(this);
    m_tabRecent->setText(tr("Recent"));
    m_tabStarred = new QToolButton(this);
    m_tabStarred->setText(tr("Starred"));
    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);
    int tabId = 0;
    for (QToolButton *tab : {m_tabAll, m_tabRecent, m_tabStarred}) {
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        // QToolButton's default size policy resists growing past its own
        // text/icon sizeHint, so the layout's stretch factor (below) has
        // nothing to distribute without this, and the tabs render
        // pill-tight around their text instead of filling the segmented
        // control evenly.
        tab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_tabGroup->addButton(tab, tabId++);
    }
    m_tabAll->setChecked(true);

    auto *tabsWidget = new QWidget(this);
    tabsWidget->setObjectName(QStringLiteral("sidebarTabs"));
    auto *tabsLayout = new QHBoxLayout(tabsWidget);
    tabsLayout->setContentsMargins(2, 2, 2, 2);
    tabsLayout->setSpacing(0);
    tabsLayout->addWidget(m_tabAll, 1);
    tabsLayout->addWidget(m_tabRecent, 1);
    tabsLayout->addWidget(m_tabStarred, 1);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setObjectName(QStringLiteral("sidebarPath"));
    m_pathLabel->setCursor(Qt::PointingHandCursor);
    m_pathLabel->installEventFilter(this);

    m_sortLabel = new QLabel(this);
    m_sortLabel->setObjectName(QStringLiteral("sidebarSort"));
    m_sortLabel->setCursor(Qt::PointingHandCursor);
    m_sortLabel->installEventFilter(this);

    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_pathLabel, 1);
    pathRow->addWidget(m_sortLabel, 0, Qt::AlignRight);

    auto *pathRowContainer = new QWidget(this);
    pathRowContainer->setObjectName(QStringLiteral("sidebarPathRow"));
    pathRowContainer->setLayout(pathRow);

    auto *headerLayout = new QVBoxLayout;
    headerLayout->setContentsMargins(12, 10, 12, 0);
    headerLayout->setSpacing(0);
    headerLayout->addWidget(m_searchEdit);
    headerLayout->addSpacing(10);
    headerLayout->addWidget(tabsWidget);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(pathRowContainer);

    // -- Document list --------------------------------------------------

    m_model = new SidebarListModel(this);
    m_proxyModel = new SidebarFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_delegate = new SidebarItemDelegate(this);

    m_list = new QListView(this);
    m_list->setObjectName(QStringLiteral("sidebarList"));
    m_list->setModel(m_proxyModel);
    m_list->setItemDelegate(m_delegate);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_list->setMouseTracking(true);
    m_list->setSpacing(1);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listScrollBar = OverlayScrollBar::attach(m_list);

    m_emptyLabel = new QLabel(m_list->viewport());
    m_emptyLabel->setObjectName(QStringLiteral("sidebarEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->hide();

    // -- Bottom action bar -----------------------------------------------

    // QPushButton rather than QToolButton: under custom QSS (border-radius
    // etc.), QToolButton's icon+text label stayed left-anchored inside
    // the button once it was stretched wider than its content -- QPushButton
    // centers reliably in the same situation.
    m_changeFolderButton = new QPushButton(this);
    m_changeFolderButton->setObjectName(QStringLiteral("sidebarSecondaryButton"));
    m_changeFolderButton->setText(tr("Change Folder"));
    m_changeFolderButton->setIcon(makeGlyphIcon(Glyph::FolderOpen, QColor(255, 255, 255)));

    m_newNoteButton = new QPushButton(this);
    m_newNoteButton->setObjectName(QStringLiteral("sidebarPrimaryButton"));
    m_newNoteButton->setText(tr("New"));
    m_newNoteButton->setIcon(makeGlyphIcon(Glyph::NewNote, QColor(0, 0, 0)));

    for (QPushButton *button : {m_changeFolderButton, m_newNoteButton}) {
        button->setIconSize(QSize(13, 13));
        button->setFixedHeight(28);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    auto *bottomBar = new QHBoxLayout;
    bottomBar->setContentsMargins(10, 8, 10, 8);
    bottomBar->setSpacing(8);
    bottomBar->addWidget(m_changeFolderButton, 1);
    bottomBar->addWidget(m_newNoteButton, 1);

    auto *bottomBarContainer = new QWidget(this);
    bottomBarContainer->setObjectName(QStringLiteral("sidebarBottomBar"));
    bottomBarContainer->setLayout(bottomBar);

    // -- Overall layout ---------------------------------------------------

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(headerLayout);
    layout->addWidget(m_list, 1);
    layout->addWidget(bottomBarContainer);

    // -- Wiring -------------------------------------------------------

    connect(m_tabAll, &QToolButton::clicked, this, [this] { setActiveTab(Tab::All); });
    connect(m_tabRecent, &QToolButton::clicked, this, [this] { setActiveTab(Tab::Recent); });
    connect(m_tabStarred, &QToolButton::clicked, this, [this] { setActiveTab(Tab::Starred); });

    auto *searchDebounce = new QTimer(this);
    searchDebounce->setSingleShot(true);
    searchDebounce->setInterval(kSearchDebounceMs);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this, searchDebounce] { searchDebounce->start(); });
    connect(searchDebounce, &QTimer::timeout, this, [this] {
        m_proxyModel->setFilterText(m_searchEdit->text());
        updateEmptyState();
    });

    connect(m_changeFolderButton, &QPushButton::clicked, this, &Sidebar::changeFolderRequested);
    connect(m_newNoteButton, &QPushButton::clicked, this, &Sidebar::newNoteRequested);

    connect(m_list, &QListView::clicked, this, &Sidebar::onActivated);
    connect(m_list, &QListView::customContextMenuRequested, this, &Sidebar::showContextMenu);
    connect(m_model, &SidebarListModel::renameRequested, this, &Sidebar::onRenameRequested);

    connect(m_fileManager, &FileManager::folderChanged, this, [this] {
        m_searchEdit->clear();
        setActiveTab(Tab::All);
        rebuildRows();
    });
    connect(m_fileManager, &FileManager::folderContentsChanged, this, [this] { rebuildRows(); });

    auto *ctrlN = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_N), this);
    ctrlN->setContext(Qt::WidgetWithChildrenShortcut);
    connect(ctrlN, &QShortcut::activated, this, &Sidebar::newNoteRequested);

    auto *ctrlO = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_O), this);
    ctrlO->setContext(Qt::WidgetWithChildrenShortcut);
    connect(ctrlO, &QShortcut::activated, this, &Sidebar::changeFolderRequested);

    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, [this] {
        if (!m_searchEdit->text().isEmpty()) {
            m_searchEdit->clear();
        } else {
            Q_EMIT closeRequested();
        }
    });

    applyTheme(nullptr);
    rebuildRows();
}

void Sidebar::focusSearch()
{
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

bool Sidebar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Down) {
            m_list->setFocus();
            if (m_proxyModel->rowCount() > 0) {
                m_list->setCurrentIndex(m_proxyModel->index(0, 0));
            }
            return true;
        }
    } else if (watched == m_list && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Slash) {
            focusSearch();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Up && m_list->currentIndex().row() <= 0) {
            focusSearch();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            onActivated(m_list->currentIndex());
            return true;
        }
        if (keyEvent->key() == Qt::Key_Delete) {
            const QModelIndex index = m_list->currentIndex();
            if (index.isValid()) {
                deleteFile(index.data(SidebarListModel::FilePathRole).toString(), index.data(Qt::DisplayRole).toString());
            }
            return true;
        }
    } else if ((watched == m_pathLabel || watched == m_sortLabel) && event->type() == QEvent::MouseButtonRelease) {
        if (watched == m_pathLabel) {
            Q_EMIT changeFolderRequested();
        } else {
            showSortMenu();
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void Sidebar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_emptyLabel->setGeometry(m_list->viewport()->rect());
}

void Sidebar::setActiveTab(Tab tab)
{
    m_activeTab = tab;
    m_tabAll->setChecked(tab == Tab::All);
    m_tabRecent->setChecked(tab == Tab::Recent);
    m_tabStarred->setChecked(tab == Tab::Starred);
    rebuildRows();
}

void Sidebar::refresh()
{
    rebuildRows();
}

void Sidebar::rebuildRows()
{
    if (m_editingItem) {
        return;
    }

    const QString shortPath = elideMiddleHome(m_fileManager->currentFolder());
    const QFontMetrics pathMetrics(m_pathLabel->font());
    m_pathLabel->setText(pathMetrics.elidedText(shortPath, Qt::ElideRight, 130));
    m_pathLabel->setToolTip(m_fileManager->currentFolder());

    const bool sortAscending = m_config->sidebarSortAscending();
    const bool sortByName = m_config->sidebarSortKey() == QStringLiteral("name");
    m_sortLabel->setText(QStringLiteral("%1 %2")
                              .arg(sortByName ? tr("Name") : tr("Modified"))
                              .arg(sortAscending ? QStringLiteral("↑") : QStringLiteral("↓")));

    QVector<SidebarRow> rows;
    const QString currentDir = m_fileManager->currentFolder();

    if (m_activeTab == Tab::All) {
        const auto notes = m_fileManager->listNotes();
        rows.reserve(notes.size());
        for (const NoteEntry &note : notes) {
            SidebarRow row;
            row.filePath = note.filePath;
            row.displayName = note.displayName;
            row.modifiedMsecs = note.modifiedMsecs;
            row.summary = note.summary;
            row.starred = isStarred(note.filePath);
            rows.append(row);
        }
        std::stable_sort(rows.begin(), rows.end(), [sortByName, sortAscending](const SidebarRow &a, const SidebarRow &b) {
            const int cmp = sortByName ? a.displayName.localeAwareCompare(b.displayName)
                                        : (a.modifiedMsecs < b.modifiedMsecs ? -1 : (a.modifiedMsecs > b.modifiedMsecs ? 1 : 0));
            return sortAscending ? cmp < 0 : cmp > 0;
        });
        // Starred rows pinned to the top, preserving the sort above
        // among both the starred and unstarred partitions.
        std::stable_partition(rows.begin(), rows.end(), [](const SidebarRow &r) { return r.starred; });
    } else if (m_activeTab == Tab::Recent) {
        for (const QString &path : m_config->recentPaths()) {
            if (!QFile::exists(path)) {
                continue;
            }
            const NoteEntry note = m_fileManager->entryFor(path);
            SidebarRow row;
            row.filePath = note.filePath;
            row.displayName = note.displayName;
            row.modifiedMsecs = note.modifiedMsecs;
            row.starred = isStarred(note.filePath);
            const QString dir = QFileInfo(path).absolutePath();
            if (dir != currentDir) {
                row.parentDirName = QDir(dir).dirName();
            } else {
                row.summary = note.summary;
            }
            rows.append(row);
        }
    } else {
        for (const QString &path : m_starredPaths) {
            if (!QFile::exists(path)) {
                continue;
            }
            const NoteEntry note = m_fileManager->entryFor(path);
            SidebarRow row;
            row.filePath = note.filePath;
            row.displayName = note.displayName;
            row.modifiedMsecs = note.modifiedMsecs;
            row.summary = note.summary;
            row.starred = true;
            rows.append(row);
        }
    }

    m_model->setRows(rows);
    if (!m_currentDocPath.isEmpty()) {
        selectFile(m_currentDocPath);
    }
    updateEmptyState();

    if (m_activeTab == Tab::All) {
        m_searchEdit->setPlaceholderText(tr("Search %n note(s)", nullptr, rows.size()));
    }
}

void Sidebar::updateEmptyState()
{
    const int total = m_model->rowCount();
    const int visible = m_proxyModel->rowCount();
    m_list->viewport()->update();

    if (visible > 0) {
        m_emptyLabel->hide();
        return;
    }

    QString text;
    if (total > 0) {
        text = tr("No notes match “%1”").arg(m_searchEdit->text());
    } else if (m_activeTab == Tab::Recent) {
        text = tr("No notes opened yet");
    } else if (m_activeTab == Tab::Starred) {
        text = tr("Right-click a note to star it");
    } else {
        text = tr("No Markdown notes in this folder yet");
    }
    m_emptyLabel->setText(text);
    m_emptyLabel->setGeometry(m_list->viewport()->rect());
    m_emptyLabel->show();
    m_emptyLabel->raise();
}

void Sidebar::selectFile(const QString &filePath)
{
    m_currentDocPath = filePath;
    const int sourceRow = m_model->indexOfPath(filePath);
    if (sourceRow < 0) {
        m_list->clearSelection();
        return;
    }
    const QModelIndex proxyIndex = m_proxyModel->mapFromSource(m_model->index(sourceRow, 0));
    if (proxyIndex.isValid()) {
        m_list->setCurrentIndex(proxyIndex);
    }
}

void Sidebar::startRename(const QString &filePath)
{
    const int sourceRow = m_model->indexOfPath(filePath);
    if (sourceRow < 0) {
        return;
    }
    const QModelIndex proxyIndex = m_proxyModel->mapFromSource(m_model->index(sourceRow, 0));
    if (!proxyIndex.isValid()) {
        return;
    }
    m_list->setCurrentIndex(proxyIndex);
    m_editingItem = true;
    m_list->edit(proxyIndex);
}

void Sidebar::onActivated(const QModelIndex &proxyIndex)
{
    if (!proxyIndex.isValid()) {
        return;
    }
    Q_EMIT fileActivated(proxyIndex.data(SidebarListModel::FilePathRole).toString());
}

void Sidebar::onRenameRequested(const QString &filePath, const QString &newNameRaw)
{
    m_editingItem = false;
    const QFileInfo oldInfo(filePath);
    const QString newName = newNameRaw.trimmed();
    if (newName.isEmpty() || newName == oldInfo.completeBaseName()) {
        rebuildRows();
        return;
    }

    const QString newPath = oldInfo.dir().filePath(newName + QStringLiteral(".") + oldInfo.suffix());
    if (QFile::exists(newPath)) {
        rebuildRows();
        return;
    }

    if (QFile::rename(filePath, newPath)) {
        if (m_starredPaths.removeOne(filePath)) {
            m_starredPaths.prepend(newPath);
            m_config->setStarredPaths(m_starredPaths);
        }
        Q_EMIT fileRenamed(filePath, newPath);
    }
    rebuildRows();
}

void Sidebar::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_list->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    m_list->setCurrentIndex(index);
    const QString path = index.data(SidebarListModel::FilePathRole).toString();
    const QString name = index.data(Qt::DisplayRole).toString();
    const bool starred = isStarred(path);

    QMenu menu(this);
    styleContextMenu(&menu, m_isDarkTheme);
    const QColor menuIconColor = m_isDarkTheme ? QColor(0xf2, 0xf2, 0xf2) : QColor(0x20, 0x20, 0x20);
    QAction *renameAction = menu.addAction(makeGlyphIcon(Glyph::Rename, menuIconColor, 14), tr("Rename"));
    QAction *starAction = menu.addAction(makeGlyphIcon(Glyph::Star, starred ? QColor(0xE6, 0xD1, 0x8A) : menuIconColor, 14),
                                          starred ? tr("Unstar") : tr("Star"));
    QAction *revealAction = menu.addAction(makeGlyphIcon(Glyph::FolderOpen, menuIconColor, 14), tr("Show in Folder"));
    menu.addSeparator();
    // A plain QAction has no per-item text-color hook QSS can target, so
    // the "this is destructive" cue is a red trash icon (consistent
    // alignment/hover with every other item) rather than red text.
    QAction *deleteAction = menu.addAction(makeGlyphIcon(Glyph::Trash, QColor(0xE0, 0x60, 0x55), 14), tr("Delete"));

    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == renameAction) {
        startRename(path);
    } else if (chosen == starAction) {
        toggleStarred(path);
    } else if (chosen == revealAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    } else if (chosen == deleteAction) {
        deleteFile(path, name);
    }
}

void Sidebar::deleteFile(const QString &path, const QString &displayName)
{
    if (path.isEmpty()) {
        return;
    }

    QMessageBox box(QMessageBox::Question, tr("Delete Note"),
                     tr("Delete “%1”? This cannot be undone.").arg(displayName),
                     QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    pinDialogAboveSlideWindow(&box);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }

    if (QFile::remove(path)) {
        m_starredPaths.removeAll(path);
        m_config->setStarredPaths(m_starredPaths);
        Q_EMIT fileDeleted(path);
        rebuildRows();
    }
}

bool Sidebar::isStarred(const QString &filePath) const
{
    return m_starredPaths.contains(filePath);
}

void Sidebar::toggleStarred(const QString &filePath)
{
    if (m_starredPaths.removeOne(filePath)) {
        // now unstarred
    } else {
        m_starredPaths.prepend(filePath);
    }
    m_config->setStarredPaths(m_starredPaths);
    rebuildRows();
}

void Sidebar::toggleSortAscending()
{
    m_config->setSidebarSortAscending(!m_config->sidebarSortAscending());
    rebuildRows();
}

void Sidebar::showSortMenu()
{
    QMenu menu(this);
    styleContextMenu(&menu, m_isDarkTheme);
    QAction *byName = menu.addAction(tr("Name"));
    QAction *byMtime = menu.addAction(tr("Modified"));
    byName->setCheckable(true);
    byMtime->setCheckable(true);
    const bool sortByName = m_config->sidebarSortKey() == QStringLiteral("name");
    byName->setChecked(sortByName);
    byMtime->setChecked(!sortByName);

    QAction *chosen = menu.exec(m_sortLabel->mapToGlobal(QPoint(0, m_sortLabel->height())));
    if (chosen == byName) {
        if (sortByName) {
            toggleSortAscending();
        } else {
            m_config->setSidebarSortKey(QStringLiteral("name"));
            rebuildRows();
        }
    } else if (chosen == byMtime) {
        if (!sortByName) {
            toggleSortAscending();
        } else {
            m_config->setSidebarSortKey(QStringLiteral("mtime"));
            rebuildRows();
        }
    }
}

void Sidebar::applyTheme(const Theme *theme)
{
    const Theme &effective = theme ? *theme : defaultFlatTheme();

    const double accentLuminance = luminance(effective.accent);
    const QColor bg = accentLuminance > 0.55 ? effective.accent.lighter(108) : effective.accent.darker(112);
    const QColor text = contrastTextColor(bg);
    const bool darkText = luminance(text) < 0.5;
    // sidebar.bg is dark whenever its text is light (and vice versa) --
    // used to pick a matching light/dark look for popup menus too.
    m_isDarkTheme = !darkText;
    // Overlay direction (white vs. black at low alpha) flips so hover/
    // divider/well tints stay visible regardless of which way sidebar.bg
    // itself leans.
    auto overlay = [darkText](double alpha) { return rgba(darkText ? QColor(0, 0, 0) : QColor(255, 255, 255), alpha); };

    const QColor accentColor = effective.content; // "sidebar.accent" token
    const QColor onAccent = contrastTextColor(accentColor);

    // QSS font-size, unlike the delegate-painted list items (already
    // point-sized via setPointSizeF -- see SidebarItemDelegate.cpp) and
    // unlike the main window's own ratio-based width, doesn't scale with
    // screen size on its own. See uiChromeScale()'s doc comment.
    const qreal scale = uiChromeScale(this);
    const QString fs115 = QString::number(11.5 * scale, 'f', 2) + QStringLiteral("px");
    const QString fs11 = QString::number(11.0 * scale, 'f', 2) + QStringLiteral("px");
    const QString fs10 = QString::number(10.0 * scale, 'f', 2) + QStringLiteral("px");
    const QString fs95 = QString::number(9.5 * scale, 'f', 2) + QStringLiteral("px");

    setStyleSheet(
        QStringLiteral(
            "#sidebar { background-color: %1; border-right: 1px solid rgba(0,0,0,0.30); }"
            "#sidebarSearch { background-color: %2; border: none; border-radius: 13px; padding: 0 8px; "
            "color: %3; font-size: %16; }"
            "#sidebarTabs { background-color: %2; border-radius: 7px; }"
            "#sidebarTabs QToolButton { background: transparent; border: none; border-radius: 5px; "
            "color: %4; font-size: %17; font-weight: 500; padding: 4px 0; }"
            "#sidebarTabs QToolButton:hover { color: %3; }"
            "#sidebarTabs QToolButton:checked { background-color: %5; color: %6; font-weight: 600; }"
            "#sidebarPathRow { border-bottom: 1px solid %7; padding-bottom: 8px; }"
            "#sidebarPath { color: %8; font-size: %18; }"
            "#sidebarSort { color: %9; font-size: %19; }"
            "#sidebarList { background: transparent; border: none; padding: 6px 8px; }"
            "#sidebarEmpty { color: %8; font-size: %16; background: transparent; }"
            "#sidebarBottomBar { border-top: 1px solid %10; }"
            "#sidebarSecondaryButton { background: transparent; border: 1px solid %11; border-radius: 14px; "
            "color: %12; font-size: %16; font-weight: 500; }"
            "#sidebarSecondaryButton:hover { background-color: %13; }"
            "#sidebarPrimaryButton { background-color: %5; border: none; border-radius: 14px; "
            "color: %6; font-size: %16; font-weight: 600; }"
            "#sidebarPrimaryButton:hover { background-color: %14; }"
            "#sidebarPrimaryButton:pressed { background-color: %15; }")
            .arg(bg.name())                  // 1
            .arg(overlay(0.20))               // 2 well
            .arg(text.name())                 // 3
            .arg(overlay(0.60))               // 4 tab unselected text
            .arg(accentColor.name())          // 5
            .arg(onAccent.name())             // 6
            .arg(overlay(0.12))               // 7 path row divider
            .arg(overlay(0.48))               // 8 muted text
            .arg(overlay(0.55))               // 9 sort text
            .arg(overlay(0.14))               // 10 bottom bar divider
            .arg(overlay(0.24))               // 11 secondary border
            .arg(overlay(0.85))               // 12 secondary text
            .arg(overlay(0.10))               // 13 secondary hover
            .arg(accentColor.lighter(105).name()) // 14 primary hover
            .arg(accentColor.darker(108).name())  // 15 primary pressed
            .arg(fs115)                           // 16
            .arg(fs11)                            // 17
            .arg(fs10)                            // 18
            .arg(fs95));                          // 19

    m_changeFolderButton->setIcon(makeGlyphIcon(Glyph::FolderOpen, darkText ? QColor(0, 0, 0) : QColor(255, 255, 255)));
    m_newNoteButton->setIcon(makeGlyphIcon(Glyph::NewNote, onAccent));

    // text already contrasts against sidebar.bg the same way it
    // contrasts against paper in the editor -- the scrollbar design
    // calls for deriving its color from "the container's own
    // background," which this is.
    m_listScrollBar->applyColor(text);

    SidebarColors colors;
    colors.text = text;
    colors.textMuted = darkText ? QColor(0, 0, 0, 115) : QColor(255, 255, 255, 115);
    colors.hover = darkText ? QColor(0, 0, 0, 20) : QColor(255, 255, 255, 20);
    colors.accent = accentColor;
    colors.onAccent = onAccent;
    colors.onAccentMuted = onAccent;
    colors.onAccentMuted.setAlpha(150);
    colors.starOn = accentColor;
    colors.starOff = darkText ? QColor(0, 0, 0, 90) : QColor(255, 255, 255, 90);
    colors.starOnSelected = onAccent;
    colors.focusBg = darkText ? QColor(0, 0, 0, 36) : QColor(255, 255, 255, 36);
    colors.focusBorder = darkText ? QColor(0, 0, 0, 70) : QColor(255, 255, 255, 70);
    m_delegate->setColors(colors);
    m_list->viewport()->update();
}
