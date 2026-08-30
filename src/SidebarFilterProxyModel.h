#pragma once

#include <QSortFilterProxyModel>

// Filters SidebarListModel rows by filename OR summary text (a plain
// QSortFilterProxyModel's single-role filtering can't match across two
// roles at once).
class SidebarFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit SidebarFilterProxyModel(QObject *parent = nullptr);

    void setFilterText(const QString &text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_filterText;
};
