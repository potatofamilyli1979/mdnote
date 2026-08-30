#include "SidebarListModel.h"

SidebarListModel::SidebarListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void SidebarListModel::setRows(const QVector<SidebarRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

int SidebarListModel::indexOfPath(const QString &path) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).filePath == path) {
            return i;
        }
    }
    return -1;
}

int SidebarListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant SidebarListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return QVariant();
    }
    const SidebarRow &row = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return row.displayName;
    case FilePathRole:
        return row.filePath;
    case ModifiedRole:
        return row.modifiedMsecs;
    case SummaryRole:
        return row.summary;
    case StarredRole:
        return row.starred;
    case ParentDirRole:
        return row.parentDirName;
    default:
        return QVariant();
    }
}

bool SidebarListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    const SidebarRow &row = m_rows.at(index.row());
    Q_EMIT renameRequested(row.filePath, value.toString());
    return true;
}

Qt::ItemFlags SidebarListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}
