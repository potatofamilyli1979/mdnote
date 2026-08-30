#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QHash>
#include <QString>
#include <QColor>

// Shared between EditorArea (which stamps kLanguageProperty on code
// lines and reserves layout margin for them) and NormalTextEdit (which
// paints the actual box, wider/taller than the block format's own
// margin-constrained rect, in NormalTextEdit::paintEvent()) -- the box
// and the block's own background can't be the same thing, since a
// QTextBlockFormat's margin is simultaneously "gap from neighbors" and
// "background fill boundary", with no way to pull them apart, so text
// padding has to come from paint-time overlay instead.
namespace CodeBlockChrome
{
inline const QColor kBackground(30, 30, 30);
// How far the painted box extends beyond the first/last code line's
// own rect (vertical) and beyond the document's left/right margin
// (horizontal) -- i.e. the actual visible padding around the text.
constexpr qreal kVerticalPadding = 20.0;
}

// Token-level syntax coloring (keywords/strings/comments/numbers) for
// fenced code blocks in normal (rich-text) mode. Not a real per-language
// parser -- keyword lists plus a handful of shared regexes, same
// pragmatic spirit as MarkdownHighlighter's "enough visual structure to
// be pleasant, not a full grammar" approach.
//
// Scoping to just code-block lines (and not, say, every monospace-styled
// character) works off kLanguageProperty: EditorArea::postProcessNormalDocument()
// stamps this onto every fenced-code-block line's QTextBlockFormat (even
// an empty string, for a fence with no language tag). A block missing
// the property entirely is left untouched by highlightBlock().
class CodeHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    // Comfortably clear of Qt's own QTextFormat property IDs (all under
    // 0x2000 -- see BlockCodeFence/BlockCodeLanguage in qtextformat.h)
    // since QTextFormat::UserProperty starts at 0x100000.
    static constexpr int kLanguageProperty = QTextFormat::UserProperty + 1;

    explicit CodeHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    static QString normalizeLanguage(const QString &raw);

    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_numberFormat;
    QRegularExpression m_numberPattern;
    QRegularExpression m_stringPattern;

    // One combined "\b(?:kw1|kw2|...)\b" pattern per canonical language
    // name, built once at construction rather than matched keyword-by
    // -keyword on every block.
    QHash<QString, QRegularExpression> m_keywordPatterns;
    // Canonical language name -> its line-comment token ("//", "#", "--"),
    // or an empty string for a language with no line comments (JSON).
    QHash<QString, QString> m_lineComments;
};
