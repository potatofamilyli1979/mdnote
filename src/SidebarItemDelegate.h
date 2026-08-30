#pragma once

#include <QStyledItemDelegate>
#include <QColor>

// Color set the delegate paints with, refreshed by Sidebar::applyTheme().
// Kept separate from Theme (accent/content only) since the sidebar needs
// several derived shades per state.
struct SidebarColors
{
    QColor text;
    QColor textMuted;
    QColor hover;
    QColor accent;    // selected ("currently open") row background
    QColor onAccent;  // text on the selected row
    QColor onAccentMuted;
    QColor starOn;
    QColor starOff;
    QColor starOnSelected;
    QColor focusBg;
    QColor focusBorder;
};

// Paints the two-line "filename + time / summary" row for each sidebar
// entry. A QStyledItemDelegate rather than one QWidget per row, since a
// widget per row doesn't scale to folders with many notes.
class SidebarItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SidebarItemDelegate(QObject *parent = nullptr);

    void setColors(const SidebarColors &colors) { m_colors = colors; }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    SidebarColors m_colors;
};
