#pragma once

#include <QObject>
#include <QString>
#include <QFileSystemWatcher>

struct NoteEntry {
    QString filePath;
    QString displayName;
    qint64 modifiedMsecs = 0;
    // First non-empty line of the document, with leading #/-/> markup
    // stripped, truncated to a small preview -- read from at most the
    // first 512 bytes of the file so scanning a large document doesn't
    // stall the sidebar.
    QString summary;
};

// Owns "which folder are we browsing" and file I/O for .md notes.
// Deliberately has no database: the folder on disk is the source of
// truth, sorted by mtime for the sidebar's "recent" list.
class FileManager : public QObject
{
    Q_OBJECT

public:
    explicit FileManager(QObject *parent = nullptr);

    QString currentFolder() const { return m_folder; }
    void setCurrentFolder(const QString &path);

    QVector<NoteEntry> listNotes() const;
    // Builds a single NoteEntry for an arbitrary path, not necessarily
    // inside currentFolder() -- used for the sidebar's "recent" tab,
    // which can span multiple directories.
    NoteEntry entryFor(const QString &filePath) const;

    // Returns empty string on failure.
    QString load(const QString &filePath) const;
    bool save(const QString &filePath, const QString &markdown) const;

    QString newNotePath(const QString &baseName = QString()) const;

Q_SIGNALS:
    void folderChanged(const QString &path);
    void folderContentsChanged();

private:
    QString m_folder;
    QFileSystemWatcher m_watcher;
};
