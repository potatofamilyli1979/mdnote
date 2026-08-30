#include "SidebarFilterProxyModel.h"
#include "SidebarListModel.h"

SidebarFilterProxyModel::SidebarFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void SidebarFilterProxyModel::setFilterText(const QString &text)
{
    if (m_filterText == text) {
        return;
    }
    m_filterText = text;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool SidebarFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_filterText.isEmpty()) {
        return true;
    }
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    const QString name = index.data(Qt::DisplayRole).toString();
    const QString summary = index.data(SidebarListModel::SummaryRole).toString();
    return name.contains(m_filterText, Qt::CaseInsensitive) || summary.contains(m_filterText, Qt::CaseInsensitive);
}
