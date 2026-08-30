#include "CodeHighlighter.h"

#include <QTextBlock>
#include <QColor>
#include <QFont>

namespace
{
QRegularExpression keywordPattern(std::initializer_list<const char *> words)
{
    QStringList escaped;
    escaped.reserve(static_cast<int>(words.size()));
    for (const char *w : words) {
        escaped.append(QRegularExpression::escape(QString::fromLatin1(w)));
    }
    return QRegularExpression(QStringLiteral("\\b(?:") + escaped.join(QLatin1Char('|')) + QStringLiteral(")\\b"));
}
}

CodeHighlighter::CodeHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Colors chosen against code blocks' fixed dark background (see
    // postProcessNormalDocument()'s codeBg) -- that background doesn't
    // change with the app theme, so these don't either.
    m_keywordFormat.setForeground(QColor(0x56, 0x9c, 0xd6));
    m_keywordFormat.setFontWeight(QFont::Bold);
    m_stringFormat.setForeground(QColor(0xce, 0x91, 0x78));
    m_commentFormat.setForeground(QColor(0x6a, 0x99, 0x55));
    m_commentFormat.setFontItalic(true);
    m_numberFormat.setForeground(QColor(0xb5, 0xce, 0xa8));

    m_numberPattern = QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"));
    m_stringPattern = QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"|'(?:\\\\.|[^'\\\\])*'"));

    m_keywordPatterns.insert(QStringLiteral("python"), keywordPattern({
        "def", "class", "return", "if", "elif", "else", "for", "while", "in", "not", "and", "or", "is",
        "import", "from", "as", "with", "try", "except", "finally", "raise", "pass", "break", "continue",
        "lambda", "yield", "global", "nonlocal", "assert", "del", "None", "True", "False", "async", "await", "self"
    }));
    m_keywordPatterns.insert(QStringLiteral("javascript"), keywordPattern({
        "function", "return", "if", "else", "for", "while", "do", "break", "continue", "var", "let", "const",
        "class", "extends", "new", "this", "typeof", "instanceof", "in", "of", "try", "catch", "finally", "throw",
        "switch", "case", "default", "import", "export", "from", "as", "async", "await", "yield", "null", "undefined",
        "true", "false", "super", "static", "get", "set"
    }));
    m_keywordPatterns.insert(QStringLiteral("typescript"), keywordPattern({
        "function", "return", "if", "else", "for", "while", "do", "break", "continue", "var", "let", "const",
        "class", "extends", "new", "this", "typeof", "instanceof", "in", "of", "try", "catch", "finally", "throw",
        "switch", "case", "default", "import", "export", "from", "as", "async", "await", "yield", "null", "undefined",
        "true", "false", "super", "static", "get", "set",
        "interface", "type", "implements", "namespace", "enum", "public", "private", "protected", "readonly", "abstract", "declare"
    }));
    m_keywordPatterns.insert(QStringLiteral("java"), keywordPattern({
        "class", "interface", "extends", "implements", "public", "private", "protected", "static", "final", "void",
        "return", "if", "else", "for", "while", "do", "break", "continue", "new", "this", "super", "try", "catch", "finally",
        "throw", "throws", "import", "package", "enum", "switch", "case", "default", "true", "false", "null", "abstract", "synchronized"
    }));
    m_keywordPatterns.insert(QStringLiteral("c"), keywordPattern({
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "goto",
        "int", "float", "double", "char", "void", "long", "short", "unsigned", "signed", "const", "static", "struct",
        "union", "enum", "typedef", "sizeof", "volatile", "extern", "true", "false", "nullptr"
    }));
    m_keywordPatterns.insert(QStringLiteral("cpp"), keywordPattern({
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "goto",
        "int", "float", "double", "char", "void", "long", "short", "unsigned", "signed", "const", "static", "struct",
        "union", "enum", "typedef", "sizeof", "volatile", "extern", "true", "false", "nullptr",
        "class", "public", "private", "protected", "namespace", "using", "new", "delete", "this", "virtual", "override",
        "template", "typename", "try", "catch", "throw", "operator", "friend", "inline", "auto", "constexpr", "explicit"
    }));
    m_keywordPatterns.insert(QStringLiteral("rust"), keywordPattern({
        "fn", "let", "mut", "if", "else", "match", "for", "while", "loop", "break", "continue", "return", "struct", "enum",
        "impl", "trait", "pub", "use", "mod", "crate", "self", "Self", "as", "in", "where", "move", "ref", "static", "const",
        "true", "false", "unsafe", "async", "await", "dyn"
    }));
    m_keywordPatterns.insert(QStringLiteral("go"), keywordPattern({
        "func", "package", "import", "var", "const", "type", "struct", "interface", "map", "chan", "go", "defer", "select",
        "if", "else", "for", "range", "switch", "case", "default", "break", "continue", "return", "true", "false", "nil"
    }));
    m_keywordPatterns.insert(QStringLiteral("bash"), keywordPattern({
        "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac", "function", "return", "exit",
        "break", "continue", "in", "export", "local", "echo", "source"
    }));
    m_keywordPatterns.insert(QStringLiteral("sql"), keywordPattern({
        "select", "insert", "update", "delete", "from", "where", "join", "inner", "outer", "left", "right", "on", "group",
        "by", "order", "having", "as", "and", "or", "not", "null", "values", "into", "set", "create", "table", "drop", "alter",
        "primary", "key", "foreign", "references", "index", "distinct", "limit", "union", "exists"
    }));
    m_keywordPatterns.insert(QStringLiteral("json"), keywordPattern({"true", "false", "null"}));

    m_lineComments.insert(QStringLiteral("python"), QStringLiteral("#"));
    m_lineComments.insert(QStringLiteral("bash"), QStringLiteral("#"));
    m_lineComments.insert(QStringLiteral("sql"), QStringLiteral("--"));
    m_lineComments.insert(QStringLiteral("json"), QString());
    m_lineComments.insert(QStringLiteral("javascript"), QStringLiteral("//"));
    m_lineComments.insert(QStringLiteral("typescript"), QStringLiteral("//"));
    m_lineComments.insert(QStringLiteral("java"), QStringLiteral("//"));
    m_lineComments.insert(QStringLiteral("c"), QStringLiteral("//"));
    m_lineComments.insert(QStringLiteral("cpp"), QStringLiteral("//"));
    m_lineComments.insert(QStringLiteral("rust"), QStringLiteral("//"));
    m_lineComments.insert(QStringLiteral("go"), QStringLiteral("//"));
}

QString CodeHighlighter::normalizeLanguage(const QString &raw)
{
    const QString lang = raw.trimmed().toLower();
    if (lang == QStringLiteral("js")) return QStringLiteral("javascript");
    if (lang == QStringLiteral("ts")) return QStringLiteral("typescript");
    if (lang == QStringLiteral("py")) return QStringLiteral("python");
    if (lang == QStringLiteral("c++") || lang == QStringLiteral("cxx") || lang == QStringLiteral("h") || lang == QStringLiteral("hpp")) return QStringLiteral("cpp");
    if (lang == QStringLiteral("sh") || lang == QStringLiteral("shell") || lang == QStringLiteral("zsh")) return QStringLiteral("bash");
    if (lang == QStringLiteral("rs")) return QStringLiteral("rust");
    if (lang == QStringLiteral("golang")) return QStringLiteral("go");
    return lang;
}

void CodeHighlighter::highlightBlock(const QString &text)
{
    const QVariant langVar = currentBlock().blockFormat().property(kLanguageProperty);
    if (!langVar.isValid()) {
        return;
    }

    const QString lang = normalizeLanguage(langVar.toString());

    int commentIndex = -1;
    const QString comment = m_lineComments.value(lang, QStringLiteral("//"));
    if (!comment.isEmpty()) {
        commentIndex = text.indexOf(comment);
    }
    const int codeLength = commentIndex >= 0 ? commentIndex : text.length();

    const auto applyInCode = [&](const QRegularExpression &pattern, const QTextCharFormat &format) {
        if (pattern.pattern().isEmpty()) {
            return;
        }
        auto it = pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedStart() >= codeLength) {
                break;
            }
            setFormat(m.capturedStart(), qMin(m.capturedLength(), codeLength - m.capturedStart()), format);
        }
    };

    applyInCode(m_keywordPatterns.value(lang), m_keywordFormat);
    applyInCode(m_numberPattern, m_numberFormat);
    applyInCode(m_stringPattern, m_stringFormat);

    if (commentIndex >= 0) {
        setFormat(commentIndex, text.length() - commentIndex, m_commentFormat);
    }
}
