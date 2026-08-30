#pragma once

#include <QWidget>
#include <QIcon>
#include <QColor>

struct Theme;
class QImage;

class QStackedWidget;
class QPlainTextEdit;
class QToolButton;
class QLabel;
class MarkdownHighlighter;
class CodeHighlighter;
class FileManager;
class NormalTextEdit;
class ThemePickerButton;
class QAction;
class OverlayScrollBar;
class QLineEdit;

// Hosts the two editing modes for one open note:
//  - source mode: raw markdown text (QPlainTextEdit + highlighter)
//  - normal mode: rendered rich text, edited in place, backed by
//    QTextDocument's built-in Markdown import/export so no custom
//    markdown<->richtext conversion code is needed.
class EditorArea : public QWidget
{
    Q_OBJECT

public:
    explicit EditorArea(FileManager *fileManager, QWidget *parent = nullptr);

    void openFile(const QString &filePath);
    void newFile(const QString &filePath);
    bool save();
    // Keeps the open note in sync when the sidebar renames the file
    // it points to out from under it.
    void notifyFileRenamed(const QString &oldPath, const QString &newPath);
    // Resets to an empty, no-file-open state when the sidebar deletes
    // the note currently being edited out from under it.
    void notifyFileDeleted(const QString &filePath);
    bool isSourceMode() const { return m_sourceMode; }
    void setSourceMode(bool source);
    QString currentFilePath() const { return m_currentFile; }
    // Height of the top toolbar row, so SlideWindow can keep the
    // floating sidebar from covering it.
    int toolbarHeight() const;
    // theme == nullptr reverts to the system default palette/style.
    void applyTheme(const Theme *theme);
    // Syncs the theme picker's displayed selection to match a theme
    // loaded from config at startup, without emitting themeSelected()
    // back out.
    void setThemeSelection(const QString &key);

Q_SIGNALS:
    void sidebarToggleRequested();
    void fileSaved(const QString &filePath);
    void titleChanged(const QString &title);
    void editorClicked();
    // Committed (click/Enter in the picker): persist and keep applied.
    void themeSelected(const QString &themeKey);
    // Live preview only (hover/keyboard-focus in the picker popup): apply
    // without persisting; themePreviewCanceled() rolls back to whatever
    // was last committed.
    void themePreviewRequested(const QString &themeKey);
    void themePreviewCanceled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void switchToSource();
    void switchToNormal();
    void markDirty();
    void updateWindowTitle();
    // QTextDocument::setMarkdown() renders fenced code blocks in a
    // monospace font, marked non-wrapping, with no visual background --
    // this gives code blocks an actual background box, lets long lines
    // wrap instead of forcing a horizontal scrollbar, and constrains
    // tables to the viewport width.
    void postProcessNormalDocument();
    // Reassigns unordered-list bullet glyphs (disc/circle/square) by
    // nesting depth across the whole document -- QTextDocument's
    // markdown importer and cursor.createList() both default every
    // unordered list to a plain disc regardless of depth.
    void restyleListBullets();
    // Converts normal mode's rich text to markdown for saving/mode
    // switching. Walks the document directly (see MarkdownExporter.h)
    // rather than using QTextDocument::toMarkdown(), which has
    // correctness bugs (a code block immediately before a table
    // corrupts, empty table cells export as bare "||||").
    QString normalToMarkdown();
    // Populates the right-click context menu for the normal-mode
    // editor: format toggles, paragraph/heading submenu, insert
    // submenu. Table-specific items (row/column operations) are
    // added separately by NormalTextEdit itself, since it already
    // owns the QTextTable manipulation logic.
    void extendContextMenu(class QMenu *menu);
    void buildParagraphMenu(class QMenu *menu);
    void buildFormatMenu(class QMenu *menu);
    // Creates the persistent QActions below, with real setShortcut()
    // bindings, once at construction -- the menu-building methods above
    // just reuse them rather than creating fresh throwaway QActions (with
    // only a "\tCtrl+B"-style *label*, no actual working shortcut) on
    // every right-click, which is what left every formatting shortcut
    // keyboard-inert before this.
    void createEditingActions();
    void updateHeadingActionsChecked();
    void setHeadingLevel(int level);
    void toggleQuote();
    void toggleList(int listStyle);
    void insertHorizontalRule();
    void insertCodeBlock();
    void insertTable();
    void insertImage();
    // Saves a raw clipboard image (see NormalTextEdit::imagePasted()) to
    // a file next to the current note, then inserts it the same way
    // insertImage() does -- a real file path, not an in-memory-only
    // resource, and immediately rescaled to the viewport width.
    void handlePastedImage(const QImage &image);
    void insertLink();
    void insertParagraphAdjacent(bool above);
    void clearFormatting();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    // Recolors all six toolbar button icons (see IconGlyphs.h) at once.
    void updateButtonIcons(const QColor &color);
    // Finds the next/previous occurrence of m_searchBox's text in
    // whichever editor is currently active, wrapping around the
    // document if nothing turns up before the end/start, then scrolls
    // so the match sits vertically centered rather than just barely
    // in view.
    void searchNext(bool forward);
    // Live search-as-you-type: re-searches from the top of the document
    // for the first match on every keystroke, rather than advancing
    // from wherever the cursor happens to be.
    void searchFromStart();
    void updateSearchFeedback(bool found);

    FileManager *m_fileManager;
    QWidget *m_toolbar;
    QStackedWidget *m_stack;
    QPlainTextEdit *m_sourceEdit;
    NormalTextEdit *m_normalEdit;
    MarkdownHighlighter *m_highlighter;
    CodeHighlighter *m_codeHighlighter;
    OverlayScrollBar *m_sourceScrollBar;
    OverlayScrollBar *m_normalScrollBar;

    QToolButton *m_sidebarButton;
    QToolButton *m_modeButton;
    QToolButton *m_saveButton;
    QToolButton *m_zoomOutButton;
    QToolButton *m_zoomResetButton;
    QToolButton *m_zoomInButton;
    ThemePickerButton *m_themePicker;
    QLabel *m_titleLabel;
    QLineEdit *m_searchBox;
    // Set in applyTheme(); updateSearchFeedback() layers a not-found
    // tint on top of it rather than replacing the whole stylesheet.
    QString m_searchBoxBaseCss;
    // Set by updateButtonIcons(); setSourceMode() reuses it to redraw
    // just the mode button's icon in the right color when it swaps
    // between its two glyphs.
    QColor m_iconColor;

    // Ctrl+S: works in both modes, so lives on EditorArea itself
    // (Qt::WidgetWithChildrenShortcut) rather than one editor's action list.
    QAction *m_saveAction = nullptr;
    // Everything below only makes sense against rich-text formatting, so
    // each is added to m_normalEdit specifically with
    // Qt::WidgetShortcut context -- scoped to fire only while normal
    // mode's editor actually has focus, not source mode's raw markdown.
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QAction *m_underlineAction = nullptr;
    QAction *m_strikeAction = nullptr;
    QAction *m_codeAction = nullptr;
    QAction *m_linkAction = nullptr;
    QAction *m_clearFormatAction = nullptr;
    QAction *m_quoteAction = nullptr;
    QAction *m_orderedListAction = nullptr;
    QAction *m_unorderedListAction = nullptr;
    QAction *m_headingActions[6] = {};
    QAction *m_paragraphAction = nullptr;
    QAction *m_promoteHeadingAction = nullptr;
    QAction *m_demoteHeadingAction = nullptr;
    QAction *m_insertImageAction = nullptr;
    QAction *m_insertTableAction = nullptr;
    QAction *m_insertCodeBlockAction = nullptr;

    // Net zoomIn()/zoomOut() steps applied since the base font size, so
    // zoomReset() can cancel them back out exactly rather than needing
    // to independently track/restore an absolute font size per editor.
    int m_zoomLevel = 0;

    QString m_currentFile;
    bool m_sourceMode = false;
    bool m_dirty = false;
    // Which editor holds edits the other one doesn't have yet. A mode
    // switch only re-round-trips through setMarkdown()/toMarkdown()
    // when the side it's reading from actually changed -- besides being
    // wasted work otherwise, Qt's own markdown converter has a bug
    // where a code block immediately followed by a table corrupts
    // within a single round-trip, so avoiding needless round-trips
    // matters for more than just performance.
    bool m_sourceEditedSinceSync = false;
    bool m_normalEditedSinceSync = false;
};
