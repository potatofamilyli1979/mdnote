#pragma once

#include <QAbstractListModel>
#include <QVector>

struct SidebarRow
{
    QString filePath;
    QString displayName;
    qint64 modifiedMsecs = 0;
    QString summary;
    bool starred = false;
    // Only populated (and only shown by the delegate) for the "recent"
    // tab when this note isn't in the currently browsed folder.
    QString parentDirName;
};

// Backs the sidebar's QListView. Deliberately a flat, fully-reset-on-
// refresh model (see setRows()) rather than incremental dataChanged()
// diffing -- the sidebar's own refresh() re-selects by path and restores
// scroll position around the reset, which covers the common cases
// (external file add/remove/rename) without the bookkeeping an
// incremental model would need.
class SidebarListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        FilePathRole = Qt::UserRole + 1,
        ModifiedRole,
        SummaryRole,
        StarredRole,
        ParentDirRole,
    };

    explicit SidebarListModel(QObject *parent = nullptr);

    void setRows(const QVector<SidebarRow> &rows);
    const QVector<SidebarRow> &rows() const { return m_rows; }
    int indexOfPath(const QString &path) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

Q_SIGNALS:
    // Emitted instead of performing the rename directly: Sidebar owns
    // the actual QFile::rename() call (and the resulting fileRenamed()
    // signal to the rest of the app), this model only forwards the
    // user's edited text.
    void renameRequested(const QString &filePath, const QString &newName);

private:
    QVector<SidebarRow> m_rows;
};
