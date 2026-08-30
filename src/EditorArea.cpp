#include "EditorArea.h"
#include "FileManager.h"
#include "MarkdownHighlighter.h"
#include "CodeHighlighter.h"
#include "OverlayScrollBar.h"
#include "NormalTextEdit.h"
#include "MarkdownExporter.h"
#include "DialogUtils.h"
#include "Theme.h"
#include "IconGlyphs.h"
#include "ThemePicker.h"

#include <QStackedWidget>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QImage>
#include <QIcon>
#include <QSize>
#include <QEvent>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTextListFormat>
#include <QTextTableFormat>
#include <QTextTable>
#include <QTextFrame>
#include <QTextOption>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QInputDialog>
#include <QLineEdit>
#include <QFontInfo>
#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QTextTableCell>
#include <QFileDialog>
#include <QSet>
#include <QScrollBar>
#include <QKeyEvent>

namespace
{
// QTextDocument's own default block spacing for headings is small and
// uniform; this gives each level distinctly more top/bottom room,
// tapering off by depth, closer to how a typical markdown renderer
// spaces headings.
void applyHeadingSpacing(QTextBlockFormat &fmt, int level)
{
    static const int topMargins[6] = {30, 24, 20, 16, 14, 12};
    static const int bottomMargins[6] = {12, 10, 8, 6, 6, 4};
    const int idx = qBound(1, level, 6) - 1;
    fmt.setTopMargin(topMargins[idx]);
    fmt.setBottomMargin(bottomMargins[idx]);
}

// QPlainTextEdit and QTextEdit both expose cursorRect()/viewport()/
// verticalScrollBar() with matching signatures but share no common
// base declaring them, hence the template rather than a shared
// non-template helper -- Editor::find()'s own default scrolling only
// guarantees the match is somewhere in view (often right at an edge),
// not centered the way the search bar is meant to land it.
template <typename Editor>
void centerCursor(Editor *editor)
{
    const QRect r = editor->cursorRect();
    const int viewportCenter = editor->viewport()->height() / 2;
    const int delta = r.center().y() - viewportCenter;
    if (delta != 0) {
        editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->value() + delta);
    }
}
}

EditorArea::EditorArea(FileManager *fileManager, QWidget *parent)
    : QWidget(parent)
    , m_fileManager(fileManager)
{
    m_sourceEdit = new QPlainTextEdit(this);
    m_sourceEdit->setFrameStyle(QFrame::NoFrame);
    QFont mono(QStringLiteral("monospace"));
    // 13 read noticeably small on a 2K screen -- two clicks of the
    // zoom-in button (+1pt each) felt right there, so that's the new
    // baseline rather than something users regularly have to zoom into
    // by hand.
    mono.setPointSize(15);
    m_sourceEdit->setFont(mono);
    m_sourceEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_sourceEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_sourceEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sourceEdit->document()->setDocumentMargin(20);
    m_highlighter = new MarkdownHighlighter(m_sourceEdit->document());
    m_sourceScrollBar = OverlayScrollBar::attach(m_sourceEdit);

    m_normalEdit = new NormalTextEdit(this);
    m_normalEdit->setFrameStyle(QFrame::NoFrame);
    m_normalEdit->setAcceptRichText(true);
    QFont normalFont = m_normalEdit->font();
    normalFont.setPointSize(15);
    m_normalEdit->setFont(normalFont);
    m_normalEdit->document()->setDocumentMargin(20);
    m_codeHighlighter = new CodeHighlighter(m_normalEdit->document());
    m_normalScrollBar = OverlayScrollBar::attach(m_normalEdit);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_normalEdit); // index 0
    m_stack->addWidget(m_sourceEdit); // index 1

    m_sidebarButton = new QToolButton(this);
    m_sidebarButton->setToolTip(tr("Show/Hide Sidebar"));

    m_modeButton = new QToolButton(this);
    m_modeButton->setToolTip(tr("Toggle Source/Normal Mode"));

    m_saveButton = new QToolButton(this);
    m_saveButton->setToolTip(tr("Save"));

    m_zoomOutButton = new QToolButton(this);
    m_zoomOutButton->setToolTip(tr("Zoom Out"));

    m_zoomResetButton = new QToolButton(this);
    m_zoomResetButton->setToolTip(tr("Reset Zoom"));

    m_zoomInButton = new QToolButton(this);
    m_zoomInButton->setToolTip(tr("Zoom In"));

    // See uiChromeScale()'s doc comment: the toolbar's own literal
    // pixel sizes (button/icon diameter, search box height/width,
    // margins) don't track a screen's size the way point-sized text
    // (this title label included) already does on its own, and read as
    // disproportionately small on a screen with a much wider logical
    // resolution.
    const qreal toolbarScale = uiChromeScale(this);
    const int kButtonDiameter = qRound(28 * toolbarScale);
    const int kIconSize = qRound(16 * toolbarScale);
    for (QToolButton *button : {m_sidebarButton, m_modeButton, m_saveButton, m_zoomOutButton, m_zoomResetButton, m_zoomInButton}) {
        button->setFixedSize(kButtonDiameter, kButtonDiameter);
        button->setIconSize(QSize(kIconSize, kIconSize));
        button->setCursor(Qt::PointingHandCursor);
    }
    // Icons get their real color from applyTheme(), which SlideWindow
    // calls right after constructing this widget -- nothing is visible
    // yet at this point, so there's no need to set a placeholder here.

    m_themePicker = new ThemePickerButton(this);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));

    m_searchBox = new QLineEdit(this);
    m_searchBox->setObjectName(QStringLiteral("editorSearchBox"));
    m_searchBox->setPlaceholderText(tr("Search"));
    m_searchBox->setFixedHeight(qRound(26 * toolbarScale));
    m_searchBox->setMinimumWidth(qRound(160 * toolbarScale));
    m_searchBox->setMaximumWidth(qRound(280 * toolbarScale));
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->installEventFilter(this);

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("editorToolbar"));
    auto *toolbarLayout = new QHBoxLayout(m_toolbar);
    const int toolbarMargin = qRound(6 * toolbarScale);
    toolbarLayout->setContentsMargins(toolbarMargin, toolbarMargin, toolbarMargin, toolbarMargin);
    toolbarLayout->addWidget(m_sidebarButton);
    toolbarLayout->addWidget(m_titleLabel, 1);
    // Centered in the space between the title and the icon cluster via
    // matching stretches on both sides, rather than fighting for exact
    // pixel-center-of-the-whole-bar against two very differently-sized
    // siblings.
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(m_searchBox);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(m_themePicker);
    toolbarLayout->addSpacing(qRound(2 * toolbarScale));
    toolbarLayout->addWidget(m_zoomOutButton);
    toolbarLayout->addWidget(m_zoomResetButton);
    toolbarLayout->addWidget(m_zoomInButton);
    toolbarLayout->addWidget(m_modeButton);
    toolbarLayout->addWidget(m_saveButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_toolbar);
    layout->addWidget(m_stack, 1);

    connect(m_sidebarButton, &QToolButton::clicked, this, &EditorArea::sidebarToggleRequested);
    connect(m_saveButton, &QToolButton::clicked, this, [this] { save(); });
    connect(m_modeButton, &QToolButton::clicked, this, [this] { setSourceMode(!m_sourceMode); });
    connect(m_zoomOutButton, &QToolButton::clicked, this, &EditorArea::zoomOut);
    connect(m_zoomResetButton, &QToolButton::clicked, this, &EditorArea::zoomReset);
    connect(m_zoomInButton, &QToolButton::clicked, this, &EditorArea::zoomIn);
    connect(m_themePicker, &ThemePickerButton::themeCommitted, this, &EditorArea::themeSelected);
    connect(m_themePicker, &ThemePickerButton::themePreviewed, this, &EditorArea::themePreviewRequested);
    connect(m_themePicker, &ThemePickerButton::previewCanceled, this, &EditorArea::themePreviewCanceled);
    connect(m_searchBox, &QLineEdit::textChanged, this, [this] { searchFromStart(); });
    connect(m_searchBox, &QLineEdit::returnPressed, this, [this] { searchNext(true); });
    connect(m_sourceEdit, &QPlainTextEdit::textChanged, this, &EditorArea::markDirty);
    connect(m_normalEdit, &QTextEdit::textChanged, this, &EditorArea::markDirty);
    // These only fire on genuine user edits: our own programmatic
    // loads (openFile/newFile/switchTo*) are wrapped in blockSignals.
    connect(m_sourceEdit, &QPlainTextEdit::textChanged, this, [this] { m_sourceEditedSinceSync = true; });
    connect(m_normalEdit, &QTextEdit::textChanged, this, [this] { m_normalEditedSinceSync = true; });

    m_sourceEdit->installEventFilter(this);
    m_normalEdit->installEventFilter(this);
    // A mouse click in the actual text content lands on the viewport
    // child, not the outer QAbstractScrollArea widget -- installing the
    // filter only on m_sourceEdit/m_normalEdit above means it only ever
    // sees clicks somewhere the viewport doesn't cover, which with the
    // native scrollbars hidden (see OverlayScrollBar) is close to
    // nowhere. This is what editorClicked() actually needs to fire on.
    m_sourceEdit->viewport()->installEventFilter(this);
    m_normalEdit->viewport()->installEventFilter(this);
    m_normalEdit->setContextMenuBuilder([this](QMenu *menu) { extendContextMenu(menu); });
    connect(m_normalEdit, &NormalTextEdit::imagePasted, this, &EditorArea::handlePastedImage);
    connect(m_normalEdit, &NormalTextEdit::richContentPasted, this, [this] {
        postProcessNormalDocument();
        m_normalEdit->rescaleImages();
    });

    createEditingActions();

    setSourceMode(false);
}

int EditorArea::toolbarHeight() const
{
    return m_toolbar->sizeHint().height();
}

void EditorArea::zoomIn()
{
    m_sourceEdit->zoomIn(1);
    m_normalEdit->zoomIn(1);
    ++m_zoomLevel;
}

void EditorArea::zoomOut()
{
    m_sourceEdit->zoomOut(1);
    m_normalEdit->zoomOut(1);
    --m_zoomLevel;
}

void EditorArea::zoomReset()
{
    if (m_zoomLevel > 0) {
        m_sourceEdit->zoomOut(m_zoomLevel);
        m_normalEdit->zoomOut(m_zoomLevel);
    } else if (m_zoomLevel < 0) {
        m_sourceEdit->zoomIn(-m_zoomLevel);
        m_normalEdit->zoomIn(-m_zoomLevel);
    }
    m_zoomLevel = 0;
}

void EditorArea::searchFromStart()
{
    const QString term = m_searchBox->text();
    if (term.isEmpty()) {
        updateSearchFeedback(true);
        return;
    }
    if (m_sourceMode) {
        QTextCursor cursor = m_sourceEdit->textCursor();
        cursor.movePosition(QTextCursor::Start);
        m_sourceEdit->setTextCursor(cursor);
    } else {
        QTextCursor cursor = m_normalEdit->textCursor();
        cursor.movePosition(QTextCursor::Start);
        m_normalEdit->setTextCursor(cursor);
    }
    searchNext(true);
}

void EditorArea::searchNext(bool forward)
{
    const QString term = m_searchBox->text();
    if (term.isEmpty()) {
        updateSearchFeedback(true);
        return;
    }

    QTextDocument::FindFlags flags;
    if (!forward) {
        flags |= QTextDocument::FindBackward;
    }

    bool found;
    if (m_sourceMode) {
        found = m_sourceEdit->find(term, flags);
        if (!found) {
            // Wrap around: retry once from the opposite end instead of
            // just reporting "not found" the moment the cursor passes
            // the last match before the end (or first match before the
            // start) of the document.
            QTextCursor cursor = m_sourceEdit->textCursor();
            cursor.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
            m_sourceEdit->setTextCursor(cursor);
            found = m_sourceEdit->find(term, flags);
        }
        if (found) {
            centerCursor(m_sourceEdit);
        }
    } else {
        found = m_normalEdit->find(term, flags);
        if (!found) {
            QTextCursor cursor = m_normalEdit->textCursor();
            cursor.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
            m_normalEdit->setTextCursor(cursor);
            found = m_normalEdit->find(term, flags);
        }
        if (found) {
            centerCursor(m_normalEdit);
        }
    }
    updateSearchFeedback(found);
}

void EditorArea::updateSearchFeedback(bool found)
{
    // Layered on top of m_searchBoxBaseCss (set in applyTheme()) rather
    // than replacing the stylesheet outright, or a no-match search would
    // also wipe out the box's own pill shape/colors.
    QString css = m_searchBoxBaseCss;
    if (!found) {
        css += QStringLiteral("QLineEdit#editorSearchBox { color: #c0392b; }");
    }
    m_searchBox->setStyleSheet(css);
}

void EditorArea::updateButtonIcons(const QColor &color)
{
    // Stashed so setSourceMode() can re-render just the mode button's
    // icon (it swaps between two glyphs -- see that function) in the
    // same color without needing applyTheme() to run again.
    m_iconColor = color;
    m_sidebarButton->setIcon(makeGlyphIcon(Glyph::SidebarToggle, color));
    m_modeButton->setIcon(makeGlyphIcon(m_sourceMode ? Glyph::ModeSource : Glyph::ModePreview, color));
    m_saveButton->setIcon(makeGlyphIcon(Glyph::Save, color));
    m_zoomOutButton->setIcon(makeGlyphIcon(Glyph::ZoomOut, color));
    m_zoomResetButton->setIcon(makeGlyphIcon(Glyph::ZoomReset, color));
    m_zoomInButton->setIcon(makeGlyphIcon(Glyph::ZoomIn, color));
}

void EditorArea::applyTheme(const Theme *theme)
{
    // "System default" is a fixed black/white flat look (see
    // defaultFlatTheme()), not literally unstyled -- routed through the
    // exact same code as any of the 9 presets rather than a special-cased
    // early return, so there's only one place that can drift out of sync.
    const Theme &effective = theme ? *theme : defaultFlatTheme();

    const QColor accentText = contrastTextColor(effective.accent);
    // Buttons use the content/accent pairing the theme itself is built
    // from (circle background = content, icon = accent) rather than the
    // accent's own contrast color -- guaranteed legible together
    // regardless of which color pair is picked, unlike coloring against
    // whatever they happen to sit on.
    m_toolbar->setStyleSheet(
        QStringLiteral("#editorToolbar { background: %1; } "
                        "#editorToolbar QLabel { color: %2; }")
            .arg(effective.accent.name(), accentText.name())
        + flatCircleButtonCss(QStringLiteral("#editorToolbar QToolButton"), effective.content,
                               qRound(28 * uiChromeScale(this))));
    updateButtonIcons(effective.accent);
    m_themePicker->setButtonBackground(effective.content);
    m_themePicker->setCurrentThemeKey(effective.key);

    // Pill-shaped, flat: a low-alpha overlay of accentText (already the
    // contrast color against this toolbar's own background) rather than
    // a fixed color, so it reads correctly against every theme's accent
    // the same way the toolbar's buttons/labels already do.
    const qreal searchScale = uiChromeScale(this);
    const QString searchBgRgba = QStringLiteral("rgba(%1,%2,%3,%4)")
                                      .arg(accentText.red())
                                      .arg(accentText.green())
                                      .arg(accentText.blue())
                                      .arg(qRound(0.16 * 255));
    m_searchBoxBaseCss = QStringLiteral(
                              "QLineEdit#editorSearchBox { background-color: %1; border: none; "
                              "border-radius: %3px; padding: 0 %4px; color: %2; font-size: %5px; }")
                              .arg(searchBgRgba, accentText.name())
                              .arg(qRound(13 * searchScale))
                              .arg(qRound(12 * searchScale))
                              .arg(qRound(12 * searchScale));
    m_searchBox->setStyleSheet(m_searchBoxBaseCss);

    // A plain (selector-less) stylesheet applies directly to the widget
    // it's set on -- setPalette() alone doesn't reliably win here since
    // SlideWindow's own stylesheet (elsewhere) puts the whole hierarchy
    // under QStyleSheetStyle, which paints from style rules, not palette.
    const QColor contentText = contrastTextColor(effective.content);
    const QString editorCss = QStringLiteral("background-color: %1; color: %2;")
                                   .arg(effective.content.name(), contentText.name());
    m_sourceEdit->setStyleSheet(editorCss);
    m_normalEdit->setStyleSheet(editorCss);
    // Light text color means contrastTextColor() picked it against a
    // dark background.
    m_normalEdit->setContentIsDark(contentText.red() > 128);

    // contentText already flips to a light color against a dark paper
    // and a dark color against a light one -- exactly the "dark basis"
    // the scrollbar design derives its colors from, so it's reused
    // as-is rather than recomputed.
    m_sourceScrollBar->applyColor(contentText);
    m_normalScrollBar->applyColor(contentText);
}

void EditorArea::setThemeSelection(const QString &key)
{
    m_themePicker->setCurrentThemeKey(key);
}

bool EditorArea::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_sourceEdit || watched == m_normalEdit || watched == m_sourceEdit->viewport() || watched == m_normalEdit->viewport())
        && event->type() == QEvent::MouseButtonPress) {
        Q_EMIT editorClicked();
    }
    if (watched == m_searchBox && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                searchNext(false);
                return true;
            }
            // Plain Enter is already handled by returnPressed(); let it
            // through rather than searching twice.
        } else if (keyEvent->key() == Qt::Key_Escape) {
            m_searchBox->clear();
            (m_sourceMode ? static_cast<QWidget *>(m_sourceEdit) : static_cast<QWidget *>(m_normalEdit))->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EditorArea::openFile(const QString &filePath)
{
    const QString markdown = m_fileManager->load(filePath);
    m_currentFile = filePath;
    m_dirty = false;

    // Load into whichever editor is currently visible; the other one
    // will be re-synced lazily on the next mode switch (and only
    // then -- see m_sourceEditedSinceSync/m_normalEditedSinceSync).
    if (m_sourceMode) {
        m_sourceEdit->blockSignals(true);
        m_sourceEdit->setPlainText(markdown);
        m_sourceEdit->blockSignals(false);
        m_sourceEditedSinceSync = true;
        m_normalEditedSinceSync = false;
    } else {
        m_normalEdit->blockSignals(true);
        m_normalEdit->setMarkdown(markdown);
        postProcessNormalDocument();
        m_normalEdit->rescaleImages();
        m_normalEdit->blockSignals(false);
        m_normalEditedSinceSync = true;
        m_sourceEditedSinceSync = false;
    }
    updateWindowTitle();
}

void EditorArea::newFile(const QString &filePath)
{
    m_currentFile = filePath;
    m_dirty = false;
    m_sourceEdit->blockSignals(true);
    m_sourceEdit->clear();
    m_sourceEdit->blockSignals(false);
    m_normalEdit->blockSignals(true);
    m_normalEdit->clear();
    m_normalEdit->blockSignals(false);
    m_sourceEditedSinceSync = false;
    m_normalEditedSinceSync = false;
    updateWindowTitle();
}

void EditorArea::notifyFileRenamed(const QString &oldPath, const QString &newPath)
{
    if (m_currentFile != oldPath) {
        return;
    }
    m_currentFile = newPath;
    updateWindowTitle();
}

void EditorArea::notifyFileDeleted(const QString &filePath)
{
    if (m_currentFile != filePath) {
        return;
    }
    m_currentFile.clear();
    m_dirty = false;
    m_sourceEdit->blockSignals(true);
    m_sourceEdit->clear();
    m_sourceEdit->blockSignals(false);
    m_normalEdit->blockSignals(true);
    m_normalEdit->clear();
    m_normalEdit->blockSignals(false);
    m_sourceEditedSinceSync = false;
    m_normalEditedSinceSync = false;
    updateWindowTitle();
}

bool EditorArea::save()
{
    if (m_currentFile.isEmpty()) {
        return false;
    }
    const QString markdown = m_sourceMode ? m_sourceEdit->toPlainText() : normalToMarkdown();
    const bool ok = m_fileManager->save(m_currentFile, markdown);
    if (ok) {
        m_dirty = false;
        updateWindowTitle();
        Q_EMIT fileSaved(m_currentFile);
    }
    return ok;
}

void EditorArea::setSourceMode(bool source)
{
    if (source == m_sourceMode) {
        return;
    }

    // Not an exact mapping -- markdown source and rendered rich text
    // are different lengths (syntax characters like ** or [text](url)
    // exist in one but not the other), so there's no character-for-
    // character correspondence between them. How far through the
    // document the cursor sits, as a fraction, lands close enough to
    // "roughly the same spot" for what this is actually for: not
    // having to go re-find where you were reading/editing after a
    // mode toggle.
    QTextDocument *oldDoc = source ? m_normalEdit->document() : m_sourceEdit->document();
    const int oldPosition = source ? m_normalEdit->textCursor().position() : m_sourceEdit->textCursor().position();
    const int oldLength = qMax(1, oldDoc->characterCount() - 1);
    const qreal fraction = qBound(0.0, qreal(oldPosition) / qreal(oldLength), 1.0);

    if (source) {
        switchToSource();
    } else {
        switchToNormal();
    }

    m_sourceMode = source;
    m_stack->setCurrentWidget(source ? static_cast<QWidget *>(m_sourceEdit) : static_cast<QWidget *>(m_normalEdit));
    // Reflects which mode is now active -- via our own themed glyph set
    // (makeGlyphIcon()), not QIcon::fromTheme(): that was tried once
    // already and always came out an untheme'd system-default color
    // regardless of the active color theme, which is why this was
    // dropped for a while rather than fixed. m_iconColor is whatever
    // updateButtonIcons() last painted every other toolbar icon with.
    m_modeButton->setIcon(makeGlyphIcon(source ? Glyph::ModeSource : Glyph::ModePreview, m_iconColor));

    QPlainTextEdit *plainTarget = source ? m_sourceEdit : nullptr;
    QTextEdit *richTarget = source ? nullptr : static_cast<QTextEdit *>(m_normalEdit);
    QTextDocument *newDoc = source ? m_sourceEdit->document() : m_normalEdit->document();
    const int newLength = qMax(1, newDoc->characterCount() - 1);
    const int newPosition = qBound(0, qRound(fraction * newLength), newLength);

    if (plainTarget) {
        QTextCursor cursor = plainTarget->textCursor();
        cursor.setPosition(newPosition);
        plainTarget->setTextCursor(cursor);
        plainTarget->ensureCursorVisible();
    } else {
        QTextCursor cursor = richTarget->textCursor();
        cursor.setPosition(newPosition);
        richTarget->setTextCursor(cursor);
        richTarget->ensureCursorVisible();
    }
}

void EditorArea::switchToSource()
{
    // Only round-trip if normal mode actually has edits source mode
    // doesn't -- if the user just switched back and forth without
    // typing anything, source mode's text is already current and
    // re-deriving it would be needless work.
    if (!m_normalEditedSinceSync) {
        return;
    }
    const QString markdown = normalToMarkdown();
    m_sourceEdit->blockSignals(true);
    m_sourceEdit->setPlainText(markdown);
    m_sourceEdit->blockSignals(false);
    m_normalEditedSinceSync = false;
}

void EditorArea::switchToNormal()
{
    if (!m_sourceEditedSinceSync) {
        return;
    }
    const QString markdown = m_sourceEdit->toPlainText();
    m_normalEdit->blockSignals(true);
    m_normalEdit->setMarkdown(markdown);
    postProcessNormalDocument();
    m_normalEdit->rescaleImages();
    m_normalEdit->blockSignals(false);
    m_sourceEditedSinceSync = false;
}

QString EditorArea::normalToMarkdown()
{
    return exportMarkdown(m_normalEdit->document());
}

void EditorArea::postProcessNormalDocument()
{
    QTextDocument *doc = m_normalEdit->document();
    static const QColor codeFg(220, 220, 220);

    // Read the ORIGINAL nonBreakableLines flag for every block up
    // front: we're about to clear it (see below), and doing that in
    // the same forward pass would corrupt the "is the next/previous
    // block also a code line" check for whichever block is visited
    // next, making every line after the first look like the start of
    // its own new block -- which is exactly the striping bug this
    // function exists to avoid.
    QList<bool> isCodeLine;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        isCodeLine.append(block.blockFormat().nonBreakableLines());
    }

    // Only the fence-opening line reliably carries BlockCodeLanguage;
    // this carries that value forward across the rest of a fenced run
    // so CodeHighlighter can read a language off of every line in it,
    // not just the first.
    QString currentCodeLanguage;

    int index = 0;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next(), ++index) {
        if (!isCodeLine[index]) {
            continue;
        }
        const bool isFirstLine = index == 0 || !isCodeLine[index - 1];
        const bool isLastLine = index == isCodeLine.size() - 1 || !isCodeLine[index + 1];
        if (isFirstLine) {
            currentCodeLanguage = block.blockFormat().stringProperty(QTextFormat::BlockCodeLanguage);
        }

        // nonBreakableLines is how setMarkdown() marks fenced code
        // lines; it also happens to disable word-wrap for them, which
        // is exactly what forces the horizontal scrollbar on a long
        // line. The actual round-trip marker toMarkdown() reads is
        // BlockCodeFence/BlockCodeLanguage (verified separately), so
        // clearing this here is safe.
        // The box itself (background, top/bottom padding, left/right
        // padding beyond this) is painted by NormalTextEdit::paintEvent(),
        // not set here -- a QTextBlockFormat's margin is simultaneously
        // "gap from neighboring blocks" AND "background fill boundary"
        // with no way to separate the two, so background-via-blockFormat
        // can never put visible space between itself and the text it
        // contains, no matter the margin value. This block format only
        // needs to reserve the *text*'s own inset from where the paint
        // overlay will draw the box edge (kVerticalPadding's horizontal
        // counterpart), and the outer gap from neighboring paragraphs.
        QTextBlockFormat fmt = block.blockFormat();
        fmt.setNonBreakableLines(false);
        // setMarkdown() already stamps this on every fenced-code line,
        // but a pasted <pre><code> block (see NormalTextEdit's HTML
        // paste path) reaches here only carrying nonBreakableLines/
        // fixed-pitch font -- without this, it would look right in the
        // editor but silently save as inline `code` spans instead of a
        // fenced block (toMarkdown() keys off BlockCodeFence, not the
        // font or this loop's own styling). Harmless to re-set on
        // already-fenced content loaded from a file.
        fmt.setProperty(QTextFormat::BlockCodeFence, QStringLiteral("`"));
        // Kept as a fallback under the paint-overlay box NormalTextEdit
        // draws: if that overlay's paint-order assumption ever turns
        // out wrong for some Qt/platform combination, this guarantees
        // the code text still sits on a readable dark background
        // instead of becoming invisible against the normal page color.
        fmt.setBackground(CodeBlockChrome::kBackground);
        fmt.setLeftMargin(18);
        fmt.setRightMargin(18);
        // Qt collapses adjacent blocks' touching margins to their max
        // rather than summing them, so this has to clear whatever
        // margin the neighboring paragraph already carries *and* the
        // painted box's own vertical padding (which pushes the box edge
        // further out than this block's natural rect) to leave any
        // visible gap between the box and surrounding text at all.
        fmt.setTopMargin(isFirstLine ? 48 : 0);
        fmt.setBottomMargin(isLastLine ? 48 : 0);
        // CodeHighlighter reads this back per-block to decide whether
        // (and how) to syntax-highlight the line -- stamped on every
        // code line, including an empty string for a fence with no
        // language tag, so any block missing it entirely is plainly
        // not code and left alone.
        fmt.setProperty(CodeHighlighter::kLanguageProperty, currentCodeLanguage);
        QTextCursor blockCursor(block);
        blockCursor.setBlockFormat(fmt);

        blockCursor.select(QTextCursor::LineUnderCursor);
        QTextCharFormat cfmt;
        cfmt.setForeground(codeFg);
        blockCursor.mergeCharFormat(cfmt);
    }
    // The block-format edits above don't by themselves guarantee
    // QSyntaxHighlighter notices (they're format-only, not text
    // insert/remove), so ask it to reprocess explicitly now that every
    // code line carries its language tag.
    m_codeHighlighter->rehighlight();

    // Headings get more breathing room than QTextDocument's own default
    // block spacing gives them, plus (for H1/H2) a thin rule drawn by
    // NormalTextEdit::paintEvent() -- QTextBlockFormat has no border
    // primitive of its own, so that part is a paint-time overlay, not
    // a document property.
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        if (level <= 0) {
            continue;
        }
        QTextBlockFormat fmt = block.blockFormat();
        applyHeadingSpacing(fmt, level);
        QTextCursor blockCursor(block);
        blockCursor.setBlockFormat(fmt);
    }

    // Blockquotes get no visual treatment at all otherwise (only the
    // left margin toggleQuote()/the markdown importer already set).
    // The actual "quoted" look -- a left accent bar -- is drawn by
    // NormalTextEdit::paintEvent() rather than a background fill,
    // matching how most markdown renderers show quotes; nothing to do
    // with block format here beyond what's already set.

    restyleListBullets();

    QTextFrame *root = doc->rootFrame();
    for (QTextFrame::iterator it = root->begin(); !it.atEnd(); ++it) {
        if (auto *table = qobject_cast<QTextTable *>(it.currentFrame())) {
            m_normalEdit->restyleTable(table);
        }
    }

    // Touching block formats via QTextCursor::setBlockFormat()/
    // QTextList::setFormat() scattered across many blocks, the way the
    // loops above do, leaves Qt's document layout with stale zero-height
    // rects for a large range of blocks after the edits -- text that's
    // genuinely present in the document but never gets laid out, so it
    // renders as blank space. Same fix already used in
    // NormalTextEdit::loadResource() for async image loads: force a full
    // relayout explicitly rather than trust it to notice on its own.
    doc->markContentsDirty(0, doc->characterCount());
}

void EditorArea::restyleListBullets()
{
    QTextDocument *doc = m_normalEdit->document();
    QSet<QTextList *> visited;
    static const QTextListFormat::Style depthStyles[3] = {
        QTextListFormat::ListDisc, QTextListFormat::ListCircle, QTextListFormat::ListSquare
    };
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        QTextList *list = block.textList();
        if (!list || visited.contains(list)) {
            continue;
        }
        visited.insert(list);

        QTextListFormat listFmt = list->format();
        if (listFmt.style() != QTextListFormat::ListDisc
            && listFmt.style() != QTextListFormat::ListCircle
            && listFmt.style() != QTextListFormat::ListSquare) {
            continue; // ordered list -- decimal/letter/roman numbering is left alone
        }
        const int depthIndex = qBound(0, listFmt.indent() - 1, 2);
        if (listFmt.style() != depthStyles[depthIndex]) {
            listFmt.setStyle(depthStyles[depthIndex]);
            list->setFormat(listFmt);
        }
    }
}

void EditorArea::extendContextMenu(QMenu *menu)
{
    buildFormatMenu(menu);

    menu->addSeparator();
    menu->addAction(m_quoteAction);
    menu->addAction(m_orderedListAction);
    menu->addAction(m_unorderedListAction);

    menu->addSeparator();
    QMenu *paragraphMenu = menu->addMenu(tr("Paragraph"));
    buildParagraphMenu(paragraphMenu);

    QMenu *insertMenu = menu->addMenu(tr("Insert"));
    insertMenu->addAction(m_insertImageAction);
    insertMenu->addAction(tr("Horizontal Rule"), this, &EditorArea::insertHorizontalRule);
    insertMenu->addAction(m_insertTableAction);
    insertMenu->addAction(m_insertCodeBlockAction);
    insertMenu->addSeparator();
    insertMenu->addAction(tr("Paragraph Above"), this, [this] { insertParagraphAdjacent(true); });
    insertMenu->addAction(tr("Paragraph Below"), this, [this] { insertParagraphAdjacent(false); });
}

void EditorArea::buildParagraphMenu(QMenu *menu)
{
    updateHeadingActionsChecked();
    for (QAction *action : m_headingActions) {
        menu->addAction(action);
    }
    menu->addAction(m_paragraphAction);

    menu->addSeparator();
    menu->addAction(m_promoteHeadingAction);
    menu->addAction(m_demoteHeadingAction);
}

void EditorArea::updateHeadingActionsChecked()
{
    const int currentLevel = m_normalEdit->textCursor().blockFormat().headingLevel();
    for (int i = 0; i < 6; ++i) {
        m_headingActions[i]->setChecked(currentLevel == i + 1);
    }
    m_paragraphAction->setChecked(currentLevel == 0);
}

void EditorArea::buildFormatMenu(QMenu *menu)
{
    menu->addAction(m_boldAction);
    menu->addAction(m_italicAction);
    menu->addAction(m_underlineAction);
    menu->addAction(m_strikeAction);
    menu->addAction(m_codeAction);

    menu->addSeparator();
    menu->addAction(m_linkAction);

    menu->addSeparator();
    menu->addAction(m_clearFormatAction);
}

void EditorArea::createEditingActions()
{
    m_saveAction = new QAction(tr("Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save); // Ctrl+S
    m_saveAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_saveAction, &QAction::triggered, this, [this] { save(); });
    addAction(m_saveAction);

    // Everything below only makes sense against rich-text formatting, so
    // each action is added to m_normalEdit specifically (not "this") with
    // an explicit WidgetShortcut context -- scoped to fire only while
    // normal mode's editor actually has focus, never while typing raw
    // markdown in source mode.
    auto addToNormalEdit = [this](QAction *action) {
        action->setShortcutContext(Qt::WidgetShortcut);
        m_normalEdit->addAction(action);
    };

    m_boldAction = new QAction(QIcon::fromTheme(QStringLiteral("format-text-bold")), tr("Bold"), this);
    m_boldAction->setShortcut(QKeySequence::Bold); // Ctrl+B
    connect(m_boldAction, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_normalEdit->textCursor();
        QTextCharFormat fmt;
        fmt.setFontWeight(cursor.charFormat().fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
        cursor.mergeCharFormat(fmt);
    });
    addToNormalEdit(m_boldAction);

    m_italicAction = new QAction(QIcon::fromTheme(QStringLiteral("format-text-italic")), tr("Italic"), this);
    m_italicAction->setShortcut(QKeySequence::Italic); // Ctrl+I
    connect(m_italicAction, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_normalEdit->textCursor();
        QTextCharFormat fmt;
        fmt.setFontItalic(!cursor.charFormat().fontItalic());
        cursor.mergeCharFormat(fmt);
    });
    addToNormalEdit(m_italicAction);

    m_underlineAction = new QAction(QIcon::fromTheme(QStringLiteral("format-text-underline")), tr("Underline"), this);
    m_underlineAction->setShortcut(QKeySequence::Underline); // Ctrl+U
    connect(m_underlineAction, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_normalEdit->textCursor();
        QTextCharFormat fmt;
        fmt.setFontUnderline(!cursor.charFormat().fontUnderline());
        cursor.mergeCharFormat(fmt);
    });
    addToNormalEdit(m_underlineAction);

    m_strikeAction = new QAction(QIcon::fromTheme(QStringLiteral("format-text-strikethrough")), tr("Strikethrough"), this);
    m_strikeAction->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_5));
    connect(m_strikeAction, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_normalEdit->textCursor();
        QTextCharFormat fmt;
        fmt.setFontStrikeOut(!cursor.charFormat().fontStrikeOut());
        cursor.mergeCharFormat(fmt);
    });
    addToNormalEdit(m_strikeAction);

    m_codeAction = new QAction(QIcon::fromTheme(QStringLiteral("format-text-code")), tr("Code"), this);
    m_codeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft));
    connect(m_codeAction, &QAction::triggered, this, [this] {
        QTextCursor cursor = m_normalEdit->textCursor();
        const bool isCode = cursor.charFormat().fontFixedPitch();
        QTextCharFormat fmt;
        fmt.setFontFixedPitch(!isCode);
        fmt.setFontFamilies(isCode ? QStringList() : QStringList{QStringLiteral("monospace")});
        fmt.setBackground(isCode ? QBrush(Qt::NoBrush) : QBrush(QColor(128, 128, 128, 60)));
        cursor.mergeCharFormat(fmt);
    });
    addToNormalEdit(m_codeAction);

    m_linkAction = new QAction(QIcon::fromTheme(QStringLiteral("insert-link")), tr("Link"), this);
    m_linkAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(m_linkAction, &QAction::triggered, this, &EditorArea::insertLink);
    addToNormalEdit(m_linkAction);

    m_clearFormatAction = new QAction(tr("Clear Formatting"), this);
    m_clearFormatAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backslash));
    connect(m_clearFormatAction, &QAction::triggered, this, &EditorArea::clearFormatting);
    addToNormalEdit(m_clearFormatAction);

    m_quoteAction = new QAction(QIcon::fromTheme(QStringLiteral("format-text-blockquote")), tr("Quote"), this);
    m_quoteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q));
    connect(m_quoteAction, &QAction::triggered, this, &EditorArea::toggleQuote);
    addToNormalEdit(m_quoteAction);

    m_orderedListAction = new QAction(QIcon::fromTheme(QStringLiteral("format-list-ordered")), tr("Numbered List"), this);
    m_orderedListAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft));
    connect(m_orderedListAction, &QAction::triggered, this, [this] { toggleList(QTextListFormat::ListDecimal); });
    addToNormalEdit(m_orderedListAction);

    m_unorderedListAction = new QAction(QIcon::fromTheme(QStringLiteral("format-list-unordered")), tr("Bullet List"), this);
    m_unorderedListAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight));
    connect(m_unorderedListAction, &QAction::triggered, this, [this] { toggleList(QTextListFormat::ListDisc); });
    addToNormalEdit(m_unorderedListAction);

    const QString headingLabels[] = {tr("Heading 1"), tr("Heading 2"), tr("Heading 3"),
                                      tr("Heading 4"), tr("Heading 5"), tr("Heading 6")};
    for (int i = 0; i < 6; ++i) {
        const int level = i + 1;
        QAction *action = new QAction(headingLabels[i], this);
        action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_0 + level)));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, level] { setHeadingLevel(level); });
        addToNormalEdit(action);
        m_headingActions[i] = action;
    }

    m_paragraphAction = new QAction(tr("Paragraph"), this);
    m_paragraphAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    m_paragraphAction->setCheckable(true);
    connect(m_paragraphAction, &QAction::triggered, this, [this] { setHeadingLevel(0); });
    addToNormalEdit(m_paragraphAction);

    m_promoteHeadingAction = new QAction(tr("Promote Heading"), this);
    m_promoteHeadingAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    connect(m_promoteHeadingAction, &QAction::triggered, this, [this] {
        const int level = m_normalEdit->textCursor().blockFormat().headingLevel();
        if (level > 1) {
            setHeadingLevel(level - 1);
        }
    });
    addToNormalEdit(m_promoteHeadingAction);

    m_demoteHeadingAction = new QAction(tr("Demote Heading"), this);
    m_demoteHeadingAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    connect(m_demoteHeadingAction, &QAction::triggered, this, [this] {
        const int level = m_normalEdit->textCursor().blockFormat().headingLevel();
        setHeadingLevel(level == 0 ? 1 : qMin(level + 1, 6));
    });
    addToNormalEdit(m_demoteHeadingAction);

    m_insertImageAction = new QAction(QIcon::fromTheme(QStringLiteral("insert-image")), tr("Image"), this);
    m_insertImageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    connect(m_insertImageAction, &QAction::triggered, this, &EditorArea::insertImage);
    addToNormalEdit(m_insertImageAction);

    m_insertTableAction = new QAction(tr("Table"), this);
    m_insertTableAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(m_insertTableAction, &QAction::triggered, this, &EditorArea::insertTable);
    addToNormalEdit(m_insertTableAction);

    m_insertCodeBlockAction = new QAction(tr("Code Block"), this);
    m_insertCodeBlockAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    connect(m_insertCodeBlockAction, &QAction::triggered, this, &EditorArea::insertCodeBlock);
    addToNormalEdit(m_insertCodeBlockAction);
}

void EditorArea::setHeadingLevel(int level)
{
    QTextCursor cursor = m_normalEdit->textCursor();
    QTextBlockFormat bfmt = cursor.blockFormat();
    bfmt.setHeadingLevel(level);
    if (level > 0) {
        applyHeadingSpacing(bfmt, level);
    }
    cursor.mergeBlockFormat(bfmt);

    // The heading-level property alone is only metadata (it's what
    // toMarkdown() reads to re-emit "# "); Qt doesn't derive a bigger
    // bold font from it automatically the way markdown import does,
    // so that has to be applied by hand for the change to be visible.
    static const qreal sizeScale[] = {1.0, 1.8, 1.5, 1.3, 1.15, 1.05, 1.0};
    QTextCursor blockCursor(cursor.block());
    blockCursor.select(QTextCursor::LineUnderCursor);
    QTextCharFormat cfmt;
    cfmt.setFontWeight(level > 0 ? QFont::Bold : QFont::Normal);
    cfmt.setFontPointSize(QFontInfo(m_normalEdit->font()).pointSizeF() * sizeScale[level]);
    blockCursor.mergeCharFormat(cfmt);
}

void EditorArea::toggleQuote()
{
    QTextCursor cursor = m_normalEdit->textCursor();
    QTextBlockFormat fmt = cursor.blockFormat();
    const bool isQuote = fmt.intProperty(QTextFormat::BlockQuoteLevel) > 0;
    fmt.setProperty(QTextFormat::BlockQuoteLevel, isQuote ? 0 : 1);
    fmt.setLeftMargin(isQuote ? 0 : 40);
    cursor.mergeBlockFormat(fmt);
}

void EditorArea::toggleList(int listStyle)
{
    QTextCursor cursor = m_normalEdit->textCursor();
    QTextListFormat fmt;
    fmt.setStyle(static_cast<QTextListFormat::Style>(listStyle));
    cursor.createList(fmt);
    // Newly created list defaults to whatever style the toolbar/menu
    // action hard-coded (always ListDisc for "unordered"); if this list
    // landed nested inside another one, restyle it (and everything
    // else) by actual depth.
    restyleListBullets();
}

void EditorArea::insertHorizontalRule()
{
    QTextCursor cursor = m_normalEdit->textCursor();
    cursor.insertHtml(QStringLiteral("<hr>"));
}

void EditorArea::insertCodeBlock()
{
    QTextCursor cursor = m_normalEdit->textCursor();
    QTextBlockFormat bfmt;
    // BlockCodeFence is what toMarkdown() actually keys off to emit a
    // proper ```fenced``` block on save (verified separately -- plain
    // monospace text without it round-trips as inline `code` spans
    // instead). Pressing Enter inside this block inherits the same
    // block format, so multi-line typing stays tagged correctly.
    bfmt.setProperty(QTextFormat::BlockCodeFence, QStringLiteral("`"));
    bfmt.setBackground(QColor(30, 30, 30));
    bfmt.setLeftMargin(12);
    bfmt.setRightMargin(12);
    bfmt.setTopMargin(8);
    bfmt.setBottomMargin(8);
    QTextCharFormat cfmt;
    cfmt.setFontFixedPitch(true);
    cfmt.setFontFamilies({QStringLiteral("monospace")});
    cfmt.setForeground(QColor(220, 220, 220));
    cursor.insertBlock(bfmt, cfmt);
    m_normalEdit->setTextCursor(cursor);
}

void EditorArea::insertTable()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Insert Table"));
    auto *form = new QFormLayout(&dialog);
    auto *colsSpin = new QSpinBox(&dialog);
    colsSpin->setRange(1, 20);
    colsSpin->setValue(3);
    auto *rowsSpin = new QSpinBox(&dialog);
    rowsSpin->setRange(1, 50);
    rowsSpin->setValue(3);
    form->addRow(tr("Columns:"), colsSpin);
    form->addRow(tr("Rows:"), rowsSpin);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    pinDialogAboveSlideWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QTextCursor cursor = m_normalEdit->textCursor();
    QTextTableFormat fmt;
    fmt.setBorder(1);
    fmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    fmt.setBorderBrush(QColor(180, 180, 180));
    fmt.setCellPadding(4);
    fmt.setCellSpacing(0);
    fmt.setWidth(QTextLength(QTextLength::PercentageLength, 100));

    const int rows = rowsSpin->value();
    const int cols = colsSpin->value();
    QTextTable *table = cursor.insertTable(rows, cols, fmt);
    // Qt's markdown exporter can't produce a valid "| --- | --- |"
    // separator row from a table where every cell is completely
    // empty (verified: it emits bare "||||" instead) -- a single
    // space per cell is enough to fix that and is invisible either way.
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            table->cellAt(row, col).firstCursorPosition().insertText(QStringLiteral(" "));
        }
    }
    m_normalEdit->restyleTable(table);
    m_normalEdit->setTextCursor(cursor);
}

void EditorArea::insertLink()
{
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Insert Link"));
    dialog.setLabelText(tr("Link URL:"));
    dialog.setTextValue(QStringLiteral("https://"));
    pinDialogAboveSlideWindow(&dialog);
    const bool ok = dialog.exec() == QDialog::Accepted;
    const QString url = dialog.textValue();
    if (!ok || url.trimmed().isEmpty()) {
        return;
    }

    QTextCursor cursor = m_normalEdit->textCursor();
    QTextCharFormat fmt;
    fmt.setAnchor(true);
    fmt.setAnchorHref(url.trimmed());
    fmt.setForeground(QColor(0x2980b9));
    fmt.setFontUnderline(true);

    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(fmt);
    } else {
        cursor.insertText(url.trimmed(), fmt);
        m_normalEdit->setTextCursor(cursor);
    }
}

void EditorArea::insertImage()
{
    QFileDialog dialog(this, tr("Insert Image"), QString(), tr("Image Files (*.png *.jpg *.jpeg *.gif *.bmp *.svg *.webp)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    // The native/portal file dialog is a separate process we have no
    // window-flag control over at all; force Qt's own widget so
    // pinDialogAboveSlideWindow() actually has something to act on.
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    pinDialogAboveSlideWindow(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }

    const QString path = dialog.selectedFiles().first();
    const QImage image(path);
    if (image.isNull()) {
        return;
    }

    QTextCursor cursor = m_normalEdit->textCursor();
    cursor.insertImage(image, path);
    m_normalEdit->rescaleImages();
}

void EditorArea::handlePastedImage(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    QString dir = QFileInfo(m_currentFile).absolutePath();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        dir = m_fileManager->currentFolder();
    }
    const QString assetsDir = QDir(dir).filePath(QStringLiteral("assets"));
    QDir().mkpath(assetsDir);
    const QString fileName = QStringLiteral("pasted-%1.png")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")));
    const QString path = QDir(assetsDir).filePath(fileName);
    if (!image.save(path, "PNG")) {
        return;
    }

    QTextCursor cursor = m_normalEdit->textCursor();
    cursor.insertImage(image, path);
    m_normalEdit->rescaleImages();
}

void EditorArea::insertParagraphAdjacent(bool above)
{
    QTextCursor cursor = m_normalEdit->textCursor();
    cursor.movePosition(above ? QTextCursor::StartOfBlock : QTextCursor::EndOfBlock);
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    if (above) {
        cursor.movePosition(QTextCursor::PreviousBlock);
    }
    m_normalEdit->setTextCursor(cursor);
}

void EditorArea::clearFormatting()
{
    QTextCursor cursor = m_normalEdit->textCursor();
    if (!cursor.hasSelection()) {
        return;
    }
    cursor.setCharFormat(QTextCharFormat());
    QTextBlockFormat bfmt;
    cursor.setBlockFormat(bfmt);
}

void EditorArea::markDirty()
{
    m_dirty = true;
    updateWindowTitle();
}

void EditorArea::updateWindowTitle()
{
    QString name = m_currentFile.isEmpty() ? tr("Untitled") : QFileInfo(m_currentFile).completeBaseName();
    if (m_dirty) {
        name += QStringLiteral(" *");
    }
    m_titleLabel->setText(name);
    Q_EMIT titleChanged(name);
}
