#pragma once

class QTextDocument;
class QString;

// Walks a QTextDocument and serializes it directly to GFM Markdown --
// no HTML intermediate, no external tool (e.g. pandoc) needed, since
// the walk already has full access to the document structure such a
// tool would otherwise have to reconstruct from HTML. Node-type
// dispatch straight off QTextBlock/QTextList/QTextTable, the same idea
// as ProseMirror's markdown serializer.
// Only covers the subset of formatting this app actually produces
// (headings, bold/italic/strikethrough, inline code, links, images,
// blockquote, ordered/unordered/task lists at arbitrary nesting depth,
// horizontal rules, tables, fenced code blocks) -- it is not a
// general-purpose Markdown writer.
QString exportMarkdown(const QTextDocument *doc);
