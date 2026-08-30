#include "MarkdownHighlighter.h"

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    Rule rule;

    QTextCharFormat headingFormat;
    headingFormat.setFontWeight(QFont::Bold);
    headingFormat.setForeground(QColor("#4c8bf5"));
    rule.pattern = QRegularExpression(QStringLiteral("^#{1,6}\\s.*"));
    rule.format = headingFormat;
    m_rules.append(rule);

    QTextCharFormat boldFormat;
    boldFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegularExpression(QStringLiteral("(\\*\\*|__)(?!\\s)(.+?)(?<!\\s)\\1"));
    rule.format = boldFormat;
    m_rules.append(rule);

    QTextCharFormat italicFormat;
    italicFormat.setFontItalic(true);
    rule.pattern = QRegularExpression(QStringLiteral("(?<!\\*)\\*(?!\\*)(?!\\s)(.+?)(?<!\\s)\\*(?!\\*)"));
    rule.format = italicFormat;
    m_rules.append(rule);

    QTextCharFormat inlineCodeFormat;
    inlineCodeFormat.setFontFamilies({QStringLiteral("monospace")});
    inlineCodeFormat.setForeground(QColor("#c0392b"));
    rule.pattern = QRegularExpression(QStringLiteral("`[^`]+`"));
    rule.format = inlineCodeFormat;
    m_rules.append(rule);

    QTextCharFormat quoteFormat;
    quoteFormat.setForeground(QColor("#7f8c8d"));
    quoteFormat.setFontItalic(true);
    rule.pattern = QRegularExpression(QStringLiteral("^>.*"));
    rule.format = quoteFormat;
    m_rules.append(rule);

    QTextCharFormat linkFormat;
    linkFormat.setForeground(QColor("#16a085"));
    linkFormat.setFontUnderline(true);
    rule.pattern = QRegularExpression(QStringLiteral("\\[[^\\]]*\\]\\([^\\)]*\\)"));
    rule.format = linkFormat;
    m_rules.append(rule);

    QTextCharFormat listFormat;
    listFormat.setForeground(QColor("#8e44ad"));
    rule.pattern = QRegularExpression(QStringLiteral("^\\s*([-*+]|\\d+\\.)\\s"));
    rule.format = listFormat;
    m_rules.append(rule);

    m_codeBlockFormat.setFontFamilies({QStringLiteral("monospace")});
    m_codeBlockFormat.setBackground(QColor("#2b2b2b"));
    m_codeBlockFormat.setForeground(QColor("#f1f1f1"));
    m_codeFenceStart = QRegularExpression(QStringLiteral("^```"));
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : std::as_const(m_rules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Fenced code blocks: state 1 means "still inside a ``` fence
    // opened on an earlier line".
    setCurrentBlockState(0);

    int searchFrom = 0;
    if (previousBlockState() == 1) {
        const int closeIndex = text.indexOf(m_codeFenceStart);
        if (closeIndex == -1) {
            setFormat(0, text.length(), m_codeBlockFormat);
            setCurrentBlockState(1);
            return;
        }
        setFormat(0, closeIndex + 3, m_codeBlockFormat);
        searchFrom = closeIndex + 3;
    }

    int startIndex = text.indexOf(m_codeFenceStart, searchFrom);
    while (startIndex >= 0) {
        const QRegularExpressionMatch endMatch = m_codeFenceStart.match(text, startIndex + 3);
        if (!endMatch.hasMatch()) {
            setFormat(startIndex, text.length() - startIndex, m_codeBlockFormat);
            setCurrentBlockState(1);
            break;
        }
        const int endIndex = endMatch.capturedStart() + endMatch.capturedLength();
        setFormat(startIndex, endIndex - startIndex, m_codeBlockFormat);
        startIndex = text.indexOf(m_codeFenceStart, endIndex);
    }
}
