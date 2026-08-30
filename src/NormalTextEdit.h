#pragma once

#include <QTextEdit>
#include <QHash>
#include <QSet>
#include <QImage>
#include <functional>

class QNetworkAccessManager;
class QMenu;
class QTextTable;

// QTextEdit as used by setMarkdown()/toMarkdown() only resolves local
// resources and never makes clicking a link do anything (an editable
// QTextEdit treats a click as "place the cursor here", by design).
// This subclass adds the two things a note-taking WYSIWYG view needs:
// fetching http(s) images asynchronously, and Ctrl+click to open a
// link in the browser instead of just moving the cursor into it.
class NormalTextEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit NormalTextEdit(QWidget *parent = nullptr);

    // Caps every inline image's display size to the viewport width
    // (preserving aspect ratio) so wide images can't force a
    // horizontal scrollbar. Called after loading new content and
    // whenever the viewport is resized or an async image arrives.
    void rescaleImages();

    // Alternating row shading, applied to any table -- freshly
    // inserted, freshly loaded from a file, or after a row/column was
    // added or removed via the right-click menu (which shifts row
    // parity and would otherwise leave the shading out of sync).
    void restyleTable(QTextTable *table);

    // Lets EditorArea (which owns the higher-level formatting/insert
    // actions) add its items to the right-click menu this widget
    // builds, without NormalTextEdit needing to know EditorArea exists.
    using ContextMenuBuilder = std::function<void(QMenu *)>;
    void setContextMenuBuilder(ContextMenuBuilder builder);

    // So the right-click menu (built via createStandardContextMenu(),
    // which parents it under this widget) can be given an explicit
    // light/dark stylesheet matching the current theme instead of
    // inheriting this widget's own background-color/color rule -- see
    // Theme::contextMenuCss()'s doc comment for why that inheritance
    // happens and looks broken if left unstyled.
    void setContentIsDark(bool dark) { m_contentIsDark = dark; }

Q_SIGNALS:
    // A raw clipboard image needs a file on disk before it can become a
    // real QTextImageFormat reference (see insertFromMimeData()'s doc
    // comment) -- EditorArea is the one that knows the current note's
    // path/folder, so it does the saving and the actual cursor.insertImage().
    void imagePasted(const QImage &image);
    // Pasted HTML lands as plain inserted content -- none of the code
    // block/table/heading/list post-processing EditorArea normally runs
    // right after loading a file (postProcessNormalDocument()) has had
    // a chance to touch it yet. EditorArea re-runs that pass on this
    // signal so a pasted table or fenced code block picks up the same
    // styling as one loaded from disk.
    void richContentPasted();

protected:
    QVariant loadResource(int type, const QUrl &url) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    // Draws two decorations QTextBlockFormat has no property for:
    // a left accent bar over blockquote blocks, and a thin rule under
    // H1/H2 blocks. Pure paint-time overlay on top of the normal text
    // layout -- doesn't touch the document, so it can't affect editing
    // or markdown export.
    void paintEvent(QPaintEvent *event) override;

private:
    void buildTableMenu(QMenu *menu, QTextTable *table);

    QNetworkAccessManager *m_network;
    QHash<QUrl, QImage> m_imageCache;
    QSet<QUrl> m_pendingRequests;
    ContextMenuBuilder m_menuBuilder;
    bool m_contentIsDark = false;
};
