#include "FileManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <algorithm>

namespace
{
QString extractSummary(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QByteArray head = file.read(512);
    const QString text = QString::fromUtf8(head);
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        // Strip leading heading/list/quote markup so the preview reads
        // as plain text rather than "## Some Heading".
        while (!line.isEmpty() && (line.front() == QLatin1Char('#') || line.front() == QLatin1Char('-')
                                    || line.front() == QLatin1Char('>') || line.front() == QLatin1Char('*'))) {
            line.remove(0, 1);
        }
        line = line.trimmed();
        if (!line.isEmpty()) {
            return line;
        }
    }
    return QString();
}
}

FileManager::FileManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileManager::folderContentsChanged);
}

void FileManager::setCurrentFolder(const QString &path)
{
    if (path == m_folder) {
        return;
    }
    if (!m_folder.isEmpty()) {
        m_watcher.removePath(m_folder);
    }
    QDir().mkpath(path);
    m_folder = path;
    m_watcher.addPath(m_folder);
    Q_EMIT folderChanged(m_folder);
}

QVector<NoteEntry> FileManager::listNotes() const
{
    QVector<NoteEntry> notes;
    if (m_folder.isEmpty()) {
        return notes;
    }

    QDir dir(m_folder);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.md"), QStringLiteral("*.markdown")},
                                                    QDir::Files, QDir::Time);
    notes.reserve(files.size());
    for (const QFileInfo &info : files) {
        NoteEntry entry;
        entry.filePath = info.absoluteFilePath();
        entry.displayName = info.completeBaseName();
        entry.modifiedMsecs = info.lastModified().toMSecsSinceEpoch();
        entry.summary = extractSummary(entry.filePath);
        notes.append(entry);
    }
    return notes;
}

NoteEntry FileManager::entryFor(const QString &filePath) const
{
    NoteEntry entry;
    const QFileInfo info(filePath);
    entry.filePath = info.absoluteFilePath();
    entry.displayName = info.completeBaseName();
    entry.modifiedMsecs = info.lastModified().toMSecsSinceEpoch();
    entry.summary = extractSummary(entry.filePath);
    return entry;
}

QString FileManager::load(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    return stream.readAll();
}

bool FileManager::save(const QString &filePath, const QString &markdown) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << markdown;
    return true;
}

QString FileManager::newNotePath(const QString &baseName) const
{
    QDir dir(m_folder);
    const QString resolvedBase = baseName.isEmpty() ? tr("Untitled") : baseName;
    QString candidate = resolvedBase;
    int suffix = 1;
    while (dir.exists(candidate + QStringLiteral(".md"))) {
        candidate = resolvedBase + QStringLiteral("-%1").arg(++suffix);
    }
    return dir.filePath(candidate + QStringLiteral(".md"));
}
