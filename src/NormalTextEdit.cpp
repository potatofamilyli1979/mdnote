#include "NormalTextEdit.h"
#include "Theme.h"
#include "CodeHighlighter.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QPainter>
#include <QSignalBlocker>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QTextTableFormat>
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QPaintEvent>
#include <QMenu>
#include <QList>
#include <QMimeData>
#include <QToolTip>
#include <QRegularExpression>

namespace
{
// Strips the presentational baggage a browser/office-app clipboard's
// HTML carries (inline styles, <style>/<script> blocks, font/color/
// size/alignment attributes) while leaving structural markup --
// headings, bold/italic, links, lists, tables -- intact. Not a real
// HTML sanitizer/parser (no tag-balance checking, no XSS concerns
// since this only ever feeds QTextDocument::setHtml(), which doesn't
// execute anything); good enough to stop a pasted page's own visual
// style from fighting this app's theme.
QString sanitizeHtml(QString html)
{
    static const QRegularExpression styleBlock(
        QStringLiteral("<style[^>]*>.*?</style>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    html.remove(styleBlock);

    static const QRegularExpression scriptBlock(
        QStringLiteral("<script[^>]*>.*?</script>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    html.remove(scriptBlock);

    static const QRegularExpression comment(
        QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    html.remove(comment);

    // <font> is purely presentational (color/face/size attributes);
    // unwrap it rather than dropping its content.
    static const QRegularExpression fontTag(
        QStringLiteral("</?font[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    html.remove(fontTag);

    // Any of these attributes, on any tag, single- or double-quoted.
    static const QRegularExpression presentationalAttr(
        QStringLiteral("\\s(?:style|class|bgcolor|color|face|size|align|valign|"
                       "width|height|border|cellpadding|cellspacing)\\s*=\\s*(\"[^\"]*\"|'[^']*')"),
        QRegularExpression::CaseInsensitiveOption);
    html.remove(presentationalAttr);

    return html;
}

QImage placeholderImage()
{
    QImage img(120, 60, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setPen(QColor(128, 128, 128, 160));
    p.drawRect(0, 0, img.width() - 1, img.height() - 1);
    p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("..."));
    return img;
}
}

NormalTextEdit::NormalTextEdit(QWidget *parent)
    : QTextEdit(parent)
    , m_network(new QNetworkAccessManager(this))
{
    setMouseTracking(true);
    // WrapAnywhere fallback so a long token with no natural break
    // point (a URL, a path) still wraps instead of forcing a
    // horizontal scrollbar.
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void NormalTextEdit::insertFromMimeData(const QMimeData *source)
{
    // Qt's own default handling for a clipboard image embeds it as an
    // in-memory document resource with no file behind it, at full native
    // pixel size with nothing capping its display width until
    // rescaleImages() next runs (on the next resize -- never
    // synchronously right after paste), which can make the rich-text
    // layout engine grind badly on a large screenshot. Routing this
    // through EditorArea to save a real file and rescale immediately
    // fixes both that and the "not actually a file" problem, matching
    // every other image-insertion path in this app (see insertImage()).
    if (source->hasImage()) {
        const QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull()) {
            Q_EMIT imagePasted(image);
            return;
        }
    }

    // HTML takes priority over the plain-text fallback almost every
    // source also provides (browsers, office apps): sanitizeHtml()
    // strips out the presentational baggage (inline styles, fonts,
    // colors) that would otherwise clash with this app's own
    // theme-consistent look, keeping only structure -- bold/italic,
    // headings, links, lists, tables -- which insertHtml() then applies
    // using this widget's own formatting for each of those, the same
    // as typing them.
    if (source->hasHtml()) {
        insertHtml(sanitizeHtml(source->html()));
        Q_EMIT richContentPasted();
        return;
    }
    if (source->hasText()) {
        insertPlainText(source->text());
        return;
    }

    QToolTip::showText(mapToGlobal(cursorRect().center()), tr("This content format isn't supported for pasting"), this, rect(), 2000);
}

QVariant NormalTextEdit::loadResource(int type, const QUrl &url)
{
    if (type == QTextDocument::ImageResource && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))) {
        auto it = m_imageCache.constFind(url);
        if (it != m_imageCache.constEnd()) {
            return *it;
        }

        if (!m_pendingRequests.contains(url)) {
            m_pendingRequests.insert(url);
            QNetworkReply *reply = m_network->get(QNetworkRequest(url));
            connect(reply, &QNetworkReply::finished, this, [this, url, reply] {
                reply->deleteLater();
                m_pendingRequests.remove(url);
                QImage image;
                if (reply->error() == QNetworkReply::NoError) {
                    image.loadFromData(reply->readAll());
                }
                if (image.isNull()) {
                    return;
                }
                m_imageCache.insert(url, image);
                document()->addResource(QTextDocument::ImageResource, url, image);
                rescaleImages();
                // The block containing this image was laid out with no
                // size for it; force a relayout now that we know it.
                document()->markContentsDirty(0, document()->characterCount());
            });
        }
        return placeholderImage();
    }
    return QTextEdit::loadResource(type, url);
}

void NormalTextEdit::rescaleImages()
{
    const qreal maxWidth = viewport()->width() - 2 * document()->documentMargin();
    if (maxWidth <= 0) {
        return;
    }

    QTextDocument *doc = document();

    // Collect what needs resizing first, without touching the document:
    // mutating a fragment's format while a QTextBlock::iterator over
    // that same block is still live is asking for trouble (format
    // changes can split/merge the underlying fragment map, and there's
    // no guarantee the iterator stays valid across that). Applying all
    // changes in a second pass, after iteration has fully finished,
    // sidesteps the question entirely.
    struct Resize {
        int position;
        int length;
        QTextImageFormat format;
    };
    QList<Resize> pending;

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }

            QTextImageFormat imgFmt = fragment.charFormat().toImageFormat();
            const QImage image = doc->resource(QTextDocument::ImageResource, QUrl(imgFmt.name())).value<QImage>();
            const qreal naturalWidth = !image.isNull() ? image.width() : imgFmt.width();
            const qreal naturalHeight = !image.isNull() ? image.height() : imgFmt.height();
            if (naturalWidth <= 0 || naturalHeight <= 0) {
                continue;
            }

            qreal targetWidth = naturalWidth;
            qreal targetHeight = naturalHeight;
            if (naturalWidth > maxWidth) {
                targetWidth = maxWidth;
                targetHeight = naturalHeight * (maxWidth / naturalWidth);
            }
            if (qFuzzyCompare(imgFmt.width(), targetWidth) && qFuzzyCompare(imgFmt.height(), targetHeight)) {
                continue;
            }

            imgFmt.setWidth(targetWidth);
            imgFmt.setHeight(targetHeight);
            pending.append({fragment.position(), fragment.length(), imgFmt});
        }
    }

    if (pending.isEmpty()) {
        return;
    }

    // Format changes below would otherwise fire textChanged() and get
    // mistaken for the user having edited the note (e.g. on every
    // window resize).
    const QSignalBlocker blocker(this);
    for (const Resize &resize : std::as_const(pending)) {
        QTextCursor cursor(doc);
        cursor.setPosition(resize.position);
        cursor.setPosition(resize.position + resize.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(resize.format);
    }
}

void NormalTextEdit::resizeEvent(QResizeEvent *event)
{
    QTextEdit::resizeEvent(event);
    rescaleImages();
}

void NormalTextEdit::paintEvent(QPaintEvent *event)
{
    QTextDocument *doc = document();
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    const int yOffset = verticalScrollBar()->value();
    const QRect visible = event->rect();

    // Code-block boxes have to be painted *underneath* the text, so
    // this runs as a pre-pass before QTextEdit's own paintEvent() draws
    // the document (text included) on top -- unlike the quote bar/
    // heading rule below, which sit beside/under the text's own
    // baseline and are fine drawn afterward instead.
    //
    // A fenced block's own QTextBlockFormat only carries a plain
    // background matching its margin-constrained rect (see
    // EditorArea::postProcessNormalDocument()'s comment on why margin
    // can't create padding by itself); this draws one wider, taller box
    // per contiguous run of code lines instead, so the code text reads
    // with real breathing room around it.
    {
        QPainter painter(viewport());
        const qreal boxLeft = doc->documentMargin() - 4;
        const qreal boxRight = viewport()->width() - doc->documentMargin() + 4;

        bool inRun = false;
        qreal runTop = 0;
        qreal runBottom = 0;
        auto flushRun = [&] {
            if (!inRun) {
                return;
            }
            const QRectF box(boxLeft, runTop - CodeBlockChrome::kVerticalPadding,
                              boxRight - boxLeft, (runBottom - runTop) + 2 * CodeBlockChrome::kVerticalPadding);
            if (box.bottom() >= visible.top() && box.top() <= visible.bottom()) {
                painter.fillRect(box, CodeBlockChrome::kBackground);
            }
            inRun = false;
        };

        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            const bool isCode = block.blockFormat().property(CodeHighlighter::kLanguageProperty).isValid();
            if (!isCode) {
                flushRun();
                continue;
            }
            QRectF rect = layout->blockBoundingRect(block);
            rect.translate(0, -yOffset);
            if (!inRun) {
                inRun = true;
                runTop = rect.top();
            }
            runBottom = rect.bottom();
        }
        flushRun();
    }

    QTextEdit::paintEvent(event);

    QPainter painter(viewport());
    static const QColor quoteBarColor(90, 140, 200, 210);
    static const QColor headingRuleColor(0, 0, 0, 40);
    // Qt draws a plain "<hr>" itself (there's no QTextFormat property to
    // recolor it -- only BlockTrailingHorizontalRulerWidth, which just
    // says a rule exists), always in a light palette-derived gray that
    // barely shows up against a light background and vanishes against a
    // dark one. Painted here instead, picking light-vs-dark off the same
    // per-theme flag the context menu styling already uses, and kept
    // soft (low contrast) on either side so it reads as a subtle
    // separator rather than a hard line.
    const QColor hrColor = m_contentIsDark ? QColor(210, 210, 210, 130) : QColor(120, 120, 120, 130);

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat bfmt = block.blockFormat();
        const int quoteLevel = bfmt.intProperty(QTextFormat::BlockQuoteLevel);
        const int headingLevel = bfmt.headingLevel();
        const bool isRule = bfmt.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
        if (quoteLevel <= 0 && (headingLevel <= 0 || headingLevel > 2) && !isRule) {
            continue;
        }

        QRectF rect = layout->blockBoundingRect(block);
        rect.translate(0, -yOffset);
        if (rect.bottom() < visible.top() || rect.top() > visible.bottom()) {
            continue;
        }

        if (isRule) {
            const qreal ruleY = rect.top() + rect.height() / 2.0;
            painter.fillRect(QRectF(doc->documentMargin(), ruleY - 1, viewport()->width() - 2 * doc->documentMargin(), 2.0), hrColor);
        } else if (quoteLevel > 0) {
            const qreal barX = qMax(doc->documentMargin() + bfmt.leftMargin() - 14.0, 2.0);
            painter.fillRect(QRectF(barX, rect.top() + 1, 3.0, rect.height() - 2), quoteBarColor);
        } else {
            const qreal ruleY = rect.bottom() - 1;
            painter.fillRect(QRectF(doc->documentMargin(), ruleY, viewport()->width() - 2 * doc->documentMargin(), 1.0), headingRuleColor);
        }
    }
}

void NormalTextEdit::restyleTable(QTextTable *table)
{
    QTextTableFormat tableFmt = table->format();
    tableFmt.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    table->setFormat(tableFmt);

    const QColor altColor(128, 128, 128, 30);
    // GFM tables always have a header row -- give it a distinct tint,
    // bolder text, and a colored top accent line so it reads as a
    // header at a glance instead of just being row zero of the stripe
    // pattern.
    const QColor headerColor(90, 140, 200, 55);
    const QColor accentColor(90, 140, 200, 220);

    for (int row = 0; row < table->rows(); ++row) {
        for (int col = 0; col < table->columns(); ++col) {
            QTextTableCell cell = table->cellAt(row, col);
            QTextTableCellFormat cellFmt = cell.format().toTableCellFormat();
            if (row == 0) {
                cellFmt.setBackground(headerColor);
                cellFmt.setFontWeight(QFont::DemiBold);
                cellFmt.setTopBorder(2.0);
                cellFmt.setTopBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                cellFmt.setTopBorderBrush(accentColor);
            } else {
                cellFmt.setFontWeight(QFont::Normal);
                cellFmt.setBackground(row % 2 == 0 ? QBrush(altColor) : QBrush(Qt::NoBrush));
                cellFmt.setTopBorder(0.0);
            }
            cell.setFormat(cellFmt);
        }
    }
}

void NormalTextEdit::mouseMoveEvent(QMouseEvent *event)
{
    const bool overLink = (event->modifiers() & Qt::ControlModifier) && !anchorAt(event->pos()).isEmpty();
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
    setToolTip(overLink ? anchorAt(event->pos()) : QString());
    QTextEdit::mouseMoveEvent(event);
}

void NormalTextEdit::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const QString href = anchorAt(event->pos());
        if (!href.isEmpty()) {
            QDesktopServices::openUrl(QUrl(href));
            return;
        }
    }
    QTextEdit::mouseReleaseEvent(event);
}

void NormalTextEdit::setContextMenuBuilder(ContextMenuBuilder builder)
{
    m_menuBuilder = std::move(builder);
}

void NormalTextEdit::contextMenuEvent(QContextMenuEvent *event)
{
    // A right-click inside an existing selection should act on that
    // selection (matches normal text-widget behavior); only reposition
    // the cursor when there's nothing already selected.
    if (!textCursor().hasSelection()) {
        setTextCursor(cursorForPosition(event->pos()));
    }
    QTextTable *table = textCursor().currentTable();

    // createStandardContextMenu() gives the native cut/copy/paste/undo
    // /redo/select-all items with correct enabled-state handling for
    // free; everything below is appended to it.
    QMenu *menu = createStandardContextMenu(event->pos());
    styleContextMenu(menu, m_contentIsDark);
    menu->addSeparator();
    if (m_menuBuilder) {
        m_menuBuilder(menu);
    }
    if (table) {
        QMenu *tableMenu = menu->addMenu(tr("Table"));
        buildTableMenu(tableMenu, table);
    }
    menu->exec(event->globalPos());
    menu->deleteLater();
}

void NormalTextEdit::buildTableMenu(QMenu *menu, QTextTable *table)
{
    const QTextTableCell cell = table->cellAt(textCursor());
    const int row = cell.row();
    const int col = cell.column();

    QAction *insertRowAbove = menu->addAction(tr("Insert Row Above"));
    QAction *insertRowBelow = menu->addAction(tr("Insert Row Below") + QStringLiteral("\tCtrl+Enter"));
    QAction *insertColLeft = menu->addAction(tr("Insert Column Left"));
    QAction *insertColRight = menu->addAction(tr("Insert Column Right"));
    menu->addSeparator();
    QAction *deleteRow = menu->addAction(tr("Delete Row"));
    deleteRow->setEnabled(table->rows() > 1);
    QAction *deleteCol = menu->addAction(tr("Delete Column"));
    deleteCol->setEnabled(table->columns() > 1);
    menu->addSeparator();
    QAction *deleteTable = menu->addAction(tr("Delete Table"));

    connect(insertRowAbove, &QAction::triggered, this, [this, table, row] {
        table->insertRows(row, 1);
        restyleTable(table);
    });
    connect(insertRowBelow, &QAction::triggered, this, [this, table, row] {
        table->insertRows(row + 1, 1);
        restyleTable(table);
    });
    connect(insertColLeft, &QAction::triggered, this, [this, table, col] {
        table->insertColumns(col, 1);
        restyleTable(table);
    });
    connect(insertColRight, &QAction::triggered, this, [this, table, col] {
        table->insertColumns(col + 1, 1);
        restyleTable(table);
    });
    connect(deleteRow, &QAction::triggered, this, [this, table, row] {
        table->removeRows(row, 1);
        restyleTable(table);
    });
    connect(deleteCol, &QAction::triggered, this, [this, table, col] {
        table->removeColumns(col, 1);
        restyleTable(table);
    });
    connect(deleteTable, &QAction::triggered, this, [this, table] {
        QTextCursor removeCursor(document());
        removeCursor.setPosition(table->firstPosition() - 1);
        removeCursor.setPosition(table->lastPosition() + 1, QTextCursor::KeepAnchor);
        removeCursor.removeSelectedText();
    });
}
