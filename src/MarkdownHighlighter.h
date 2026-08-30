#pragma once

#include <QSyntaxHighlighter>
#include <QVector>
#include <QRegularExpression>

// Lightweight highlighter for the raw-source editing mode. Not a full
// CommonMark parser -- just enough visual structure (headings, bold,
// italic, inline code, code fences, quotes, links) to make the source
// pleasant to read while typing.
class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<Rule> m_rules;
    QTextCharFormat m_codeBlockFormat;
    QRegularExpression m_codeFenceStart;
};
