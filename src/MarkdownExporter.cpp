#include "MarkdownExporter.h"

#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextList>
#include <QTextListFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextImageFormat>
#include <QFileInfo>
#include <QStringList>

namespace
{

// Backslash-escapes characters that would otherwise be read as inline
// Markdown syntax if they appeared literally in plain text. Applied to
// every non-code, non-image text fragment; deliberately not applied
// inside inline code spans or fenced code blocks, where content must
// stay byte-for-byte literal.
QString escapeInlineText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar &ch : text) {
        switch (ch.unicode()) {
        case '\\':
        case '`':
        case '*':
        case '_':
        case '[':
        case ']':
        case '<':
        case '>':
            out += QLatin1Char('\\');
            break;
        default:
            break;
        }
        out += ch;
    }
    return out;
}

// A paragraph/list-item/quote whose literal text happens to start with
// a character that means something at the start of a Markdown line
// (heading #, list marker -/+/*, a leading "1." etc, a blockquote >,
// or a code-fence backtick/tilde) needs that one character escaped, or
// re-parsing the saved file would misread plain text as block markup.
// escapeInlineText() only covers characters escaped *inline*; this is
// a separate, position-specific check applied once to the assembled
// line, since it depends on what happens to be first, not each
// fragment's own formatting.
QString escapeLeadingMarkup(const QString &content)
{
    if (content.isEmpty()) {
        return content;
    }
    static const QString leadChars = QStringLiteral("#->+~");
    const QChar first = content.at(0);
    if (leadChars.contains(first)) {
        return QLatin1Char('\\') + content;
    }
    // "1. " / "12) " etc at line start reads as an ordered-list item.
    int i = 0;
    while (i < content.size() && content.at(i).isDigit()) {
        ++i;
    }
    if (i > 0 && i < content.size() && (content.at(i) == QLatin1Char('.') || content.at(i) == QLatin1Char(')'))) {
        return content.left(i) + QLatin1Char('\\') + content.mid(i);
    }
    return content;
}

// CommonMark's rule for an inline code span: the delimiter run must be
// longer than the longest run of backticks inside the content, so it
// can never be read as closing early.
QString wrapInlineCode(const QString &text)
{
    int longestRun = 0;
    int currentRun = 0;
    for (const QChar &ch : text) {
        if (ch == QLatin1Char('`')) {
            ++currentRun;
            longestRun = qMax(longestRun, currentRun);
        } else {
            currentRun = 0;
        }
    }
    const QString fence(longestRun + 1, QLatin1Char('`'));
    const bool needsPad = text.startsWith(QLatin1Char('`')) || text.endsWith(QLatin1Char('`'))
        || text.startsWith(QLatin1Char(' ')) || text.endsWith(QLatin1Char(' '));
    const QString pad = needsPad ? QStringLiteral(" ") : QString();
    return fence + pad + text + pad + fence;
}

QString serializeInline(const QTextBlock &block, bool isHeading = false)
{
    QString out;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) {
            continue;
        }
        const QTextCharFormat fmt = fragment.charFormat();

        if (fmt.isImageFormat()) {
            const QTextImageFormat img = fmt.toImageFormat();
            // An empty alt silently drops the image entirely on the
            // next setMarkdown() round-trip, so it's never left blank.
            QString alt = QFileInfo(img.name()).completeBaseName();
            if (alt.isEmpty()) {
                alt = QStringLiteral("image");
            }
            out += QStringLiteral("![%1](%2)").arg(escapeInlineText(alt), img.name());
            continue;
        }

        const QString text = fragment.text();
        if (text.isEmpty()) {
            continue;
        }

        if (fmt.fontFixedPitch()) {
            out += wrapInlineCode(text);
            continue;
        }

        QString piece = escapeInlineText(text);
        // Nested one marker at a time (strike, then italic, then bold,
        // each wrapping the previous) rather than a combined
        // "***text***" shortcut, so any subset of the three formats
        // round-trips correctly.
        if (fmt.fontStrikeOut()) {
            piece = QStringLiteral("~~%1~~").arg(piece);
        }
        if (fmt.fontItalic()) {
            piece = QStringLiteral("*%1*").arg(piece);
        }
        // Heading text comes back from setMarkdown() with bold baked
        // into how it's rendered, not as a real **bold** span in the
        // source -- skip re-emitting it for headings, same as before.
        if (fmt.fontWeight() == QFont::Bold && !isHeading) {
            piece = QStringLiteral("**%1**").arg(piece);
        }
        if (fmt.isAnchor() && !fmt.anchorHref().isEmpty()) {
            piece = QStringLiteral("[%1](%2)").arg(piece, fmt.anchorHref());
        }
        out += piece;
    }
    return out;
}

QString serializeTable(QTextTable *table)
{
    QString md;
    const int cols = table->columns();
    for (int row = 0; row < table->rows(); ++row) {
        md += QLatin1Char('|');
        for (int col = 0; col < cols; ++col) {
            const QTextTableCell cell = table->cellAt(row, col);
            QStringList lines;
            for (auto it = cell.begin(); !it.atEnd(); ++it) {
                const QTextBlock block = it.currentBlock();
                if (block.isValid()) {
                    // Same deal as headings: setMarkdown() renders a
                    // GFM table's header row in bold as part of how it
                    // displays it, not because the source text actually
                    // had ** around it -- re-emitting that would add
                    // literal bold markers the original never had.
                    lines << serializeInline(block, row == 0);
                }
            }
            // GFM table cells can't contain real paragraph breaks --
            // <br> is the standard workaround for a multi-block cell.
            QString cellText = lines.join(QStringLiteral("<br>"));
            cellText.replace(QLatin1Char('|'), QStringLiteral("\\|"));
            md += QLatin1Char(' ') + cellText + QStringLiteral(" |");
        }
        md += QLatin1Char('\n');
        if (row == 0) {
            md += QLatin1Char('|');
            for (int col = 0; col < cols; ++col) {
                md += QStringLiteral(" --- |");
            }
            md += QLatin1Char('\n');
        }
    }
    // Drop the table's own trailing newline -- the caller joins top-
    // level blocks with its own blank-line separator.
    if (md.endsWith(QLatin1Char('\n'))) {
        md.chop(1);
    }
    return md;
}

} // namespace

QString exportMarkdown(const QTextDocument *doc)
{
    QStringList blocks;

    QStringList currentListLines;
    QTextList *openList = nullptr;
    QStringList codeLines;
    QString codeLanguage;
    bool inCode = false;

    auto flushList = [&] {
        if (!currentListLines.isEmpty()) {
            blocks << currentListLines.join(QLatin1Char('\n'));
            currentListLines.clear();
        }
        openList = nullptr;
    };
    auto flushCode = [&] {
        if (inCode) {
            const QString fence = QStringLiteral("```");
            blocks << QStringLiteral("%1%2\n%3\n%1").arg(fence, codeLanguage, codeLines.join(QLatin1Char('\n')));
            codeLines.clear();
            codeLanguage.clear();
            inCode = false;
        }
    };

    QTextFrame *root = doc->rootFrame();
    for (auto it = root->begin(); !it.atEnd(); ++it) {
        if (auto *table = qobject_cast<QTextTable *>(it.currentFrame())) {
            flushList();
            flushCode();
            blocks << serializeTable(table);
            continue;
        }

        const QTextBlock block = it.currentBlock();
        if (!block.isValid()) {
            continue;
        }
        const QTextBlockFormat bfmt = block.blockFormat();

        if (bfmt.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
            flushList();
            flushCode();
            blocks << QStringLiteral("---");
            continue;
        }

        // BlockCodeFence is what both setMarkdown() (parsing a fenced
        // ```block```) and insertCodeBlock()/EditorArea's paste
        // post-processing tag every line of a code block with;
        // consecutive tagged blocks merge into one fence, not one per
        // line.
        if (bfmt.hasProperty(QTextFormat::BlockCodeFence)) {
            flushList();
            if (!inCode) {
                inCode = true;
                codeLanguage = bfmt.property(QTextFormat::BlockCodeLanguage).toString();
            }
            codeLines << block.text();
            continue;
        }
        flushCode();

        QTextList *list = block.textList();
        if (list) {
            if (list != openList) {
                flushList();
                openList = list;
            }
            const QTextListFormat listFmt = list->format();
            // indent() is 1 at the top level -- depth 0 needs no
            // leading spaces at all, hence the -1. 4 spaces per level
            // rather than 2: safely past what any CommonMark-family
            // parser (including this app's own importer) needs to
            // recognize the item as nested under its parent regardless
            // of the parent marker's width ("- " vs "1. ").
            const int depth = qMax(0, listFmt.indent() - 1);
            const QString indent(depth * 4, QLatin1Char(' '));

            QString marker;
            if (listFmt.style() == QTextListFormat::ListDecimal) {
                marker = QStringLiteral("%1.").arg(list->itemNumber(block) + 1);
            } else {
                marker = QStringLiteral("-");
            }

            QString content = serializeInline(block);
            // GFM task list: this app's own checkbox toggle and
            // setMarkdown() both key off this marker property; it was
            // never read on the way back out before, silently losing
            // checked state on every save.
            const auto marker_ = bfmt.marker();
            if (marker_ == QTextBlockFormat::MarkerType::Checked) {
                content = QStringLiteral("[x] ") + content;
            } else if (marker_ == QTextBlockFormat::MarkerType::Unchecked) {
                content = QStringLiteral("[ ] ") + content;
            }

            currentListLines << QStringLiteral("%1%2 %3").arg(indent, marker, content);
            continue;
        }
        flushList();

        const int heading = bfmt.headingLevel();
        const bool isQuote = bfmt.intProperty(QTextFormat::BlockQuoteLevel) > 0;
        QString content = escapeLeadingMarkup(serializeInline(block, heading > 0));

        if (heading > 0) {
            blocks << QString(heading, QLatin1Char('#')) + QLatin1Char(' ') + content;
        } else if (isQuote) {
            blocks << QStringLiteral("> ") + content;
        } else {
            blocks << content;
        }
    }
    flushList();
    flushCode();

    return blocks.join(QStringLiteral("\n\n"));
}
