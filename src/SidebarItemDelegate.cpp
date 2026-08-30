#include "SidebarItemDelegate.h"
#include "SidebarListModel.h"
#include "Theme.h"

#include <QPainter>
#include <QDateTime>
#include <QFontMetrics>
#include <QApplication>
#include <QCoreApplication>

namespace
{
QString relativeTime(qint64 msecs)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(msecs);
    const QDate today = QDate::currentDate();
    const QDate date = dt.date();
    if (date == today) {
        return dt.toString(QStringLiteral("HH:mm"));
    }
    if (date == today.addDays(-1)) {
        return QObject::tr("Yesterday");
    }
    if (date >= today.addDays(-6)) {
        // QT_TR_NOOP (no explicit context) infers its context from an
        // enclosing QObject-derived class -- this free function has
        // none, so lupdate needs the context spelled out, matching
        // "Yesterday" above's QObject::tr() (context == "QObject").
        static const char *weekdays[] = {"", QT_TRANSLATE_NOOP("QObject", "Mon"), QT_TRANSLATE_NOOP("QObject", "Tue"),
                                          QT_TRANSLATE_NOOP("QObject", "Wed"), QT_TRANSLATE_NOOP("QObject", "Thu"),
                                          QT_TRANSLATE_NOOP("QObject", "Fri"), QT_TRANSLATE_NOOP("QObject", "Sat"),
                                          QT_TRANSLATE_NOOP("QObject", "Sun")};
        return QCoreApplication::translate("QObject", weekdays[date.dayOfWeek()]);
    }
    if (date.year() == today.year()) {
        return dt.toString(QStringLiteral("M/d"));
    }
    return dt.toString(QStringLiteral("yyyy/M/d"));
}

QFont monoFont(qreal pointSize)
{
    QFont f(QStringLiteral("monospace"));
    f.setPointSizeF(pointSize);
    return f;
}
}

SidebarItemDelegate::SidebarItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize SidebarItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index);
    const qreal scale = uiChromeScale(option.widget);
    return QSize(qRound(200 * scale), qRound(47 * scale));
}

void SidebarItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const bool isCurrentDoc = option.state & QStyle::State_Selected;
    const bool isKeyboardFocused = (option.state & QStyle::State_HasFocus) && !isCurrentDoc;
    const bool isHovered = option.state & QStyle::State_MouseOver;

    const QRectF itemRect(option.rect);

    if (isCurrentDoc) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_colors.accent);
        painter->drawRoundedRect(itemRect, 6, 6);
    } else if (isKeyboardFocused) {
        painter->setPen(QPen(m_colors.focusBorder, 1));
        painter->setBrush(m_colors.focusBg);
        painter->drawRoundedRect(itemRect.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    } else if (isHovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_colors.hover);
        painter->drawRoundedRect(itemRect, 6, 6);
    }

    const QColor nameColor = isCurrentDoc ? m_colors.onAccent : m_colors.text;
    const QColor timeColor = isCurrentDoc ? m_colors.onAccentMuted : m_colors.textMuted;
    const QColor summaryColor = isCurrentDoc ? m_colors.onAccentMuted : m_colors.textMuted;
    const bool starred = index.data(SidebarListModel::StarredRole).toBool();
    const QColor starColor = starred ? (isCurrentDoc ? m_colors.starOnSelected : m_colors.starOn) : m_colors.starOff;

    // Point sizes here (unlike the sidebar's own QSS font-size rules --
    // see Sidebar.cpp's applyTheme()) don't respond to uiChromeScale()
    // on their own, and don't need to for DPI reasons (point sizes
    // already track a screen's real logicalDotsPerInch()) -- but they
    // do still need scaling for *proportion*: the QSS-styled buttons
    // below this list are deliberately scaled by the same factor, and
    // leaving these fixed made the list's own title text end up smaller
    // than "Change Folder"/"New" on a screen where that factor isn't 1. The
    // row's own layout constants scale alongside the fonts so bigger
    // text doesn't clip against a still-fixed row height.
    const qreal scale = uiChromeScale(option.widget);
    const qreal kPadH = 10 * scale;
    const qreal kPadV = 7 * scale;
    const qreal kStarWidth = 12 * scale;
    const qreal kLineHeight = 14 * scale;
    const QRectF contentRect = itemRect.adjusted(kPadH, kPadV, -kPadH, -kPadV);

    // Row 1: star, filename, time.
    QFont starFont = painter->font();
    starFont.setPointSizeF(9 * scale);
    painter->setFont(starFont);
    const QRectF starRect(contentRect.left(), contentRect.top(), kStarWidth, kLineHeight);
    painter->setPen(starColor);
    painter->drawText(starRect, Qt::AlignLeft | Qt::AlignVCenter, starred ? QStringLiteral("★") : QStringLiteral("☆"));

    QFont timeFont = monoFont(9.5 * scale);
    painter->setFont(timeFont);
    const QString timeText = relativeTime(index.data(SidebarListModel::ModifiedRole).toLongLong());
    const QFontMetricsF timeMetrics(timeFont);
    const qreal timeWidth = timeMetrics.horizontalAdvance(timeText);
    const QRectF timeRect(contentRect.right() - timeWidth, contentRect.top(), timeWidth, kLineHeight);
    painter->setPen(timeColor);
    painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter, timeText);

    QFont nameFont = painter->font();
    nameFont.setPointSizeF(10.5 * scale);
    nameFont.setWeight(isCurrentDoc ? QFont::DemiBold : QFont::Medium);
    painter->setFont(nameFont);
    const QFontMetrics nameMetrics(nameFont);
    const qreal nameLeft = contentRect.left() + kStarWidth + 4 * scale;
    const qreal nameWidth = qMax<qreal>(0, timeRect.left() - 6 * scale - nameLeft);
    const QRectF nameRect(nameLeft, contentRect.top(), nameWidth, kLineHeight);
    const QString elidedName = nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, int(nameWidth));
    painter->setPen(nameColor);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    // Row 2: summary, or the containing folder name for cross-directory
    // "recent" entries.
    QString secondLine = index.data(SidebarListModel::ParentDirRole).toString();
    if (secondLine.isEmpty()) {
        secondLine = index.data(SidebarListModel::SummaryRole).toString();
    }
    if (!secondLine.isEmpty()) {
        QFont summaryFont = painter->font();
        summaryFont.setPointSizeF(9 * scale);
        summaryFont.setWeight(QFont::Normal);
        painter->setFont(summaryFont);
        const QFontMetrics summaryMetrics(summaryFont);
        const QRectF summaryRect(nameLeft, contentRect.top() + 17 * scale, contentRect.width() - kStarWidth - 4 * scale, kLineHeight);
        const QString elidedSummary = summaryMetrics.elidedText(secondLine, Qt::ElideRight, int(summaryRect.width()));
        painter->setPen(summaryColor);
        painter->drawText(summaryRect, Qt::AlignLeft | Qt::AlignVCenter, elidedSummary);
    }

    painter->restore();
}
