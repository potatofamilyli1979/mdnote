#pragma once

#include <QToolButton>
#include <QWidget>
#include <vector>

struct Theme;
class QLabel;

// One 32px bicolor circular chip in the popup grid: split diagonally,
// upper-left = theme.accent ("toolbarColor"), lower-right = theme.content
// ("paperColor"). Hovering/keyboard-focusing previews live; clicking
// commits. Drawn entirely in paintEvent() rather than composed from
// stock widgets -- there's no stock Qt control for "two-color circle".
class ThemeSwatch : public QWidget
{
    Q_OBJECT

public:
    // theme == nullptr represents "system default" (defaultFlatTheme()).
    ThemeSwatch(const Theme *theme, const QString &key, QWidget *parent = nullptr);

    QString key() const { return m_key; }
    QString label() const;
    void setSelected(bool selected);
    // Keyboard-equivalent of hover, driven centrally by
    // ThemePickerPopup's arrow-key navigation rather than real Qt widget
    // focus (a 5x2 grid doesn't match the linear tab order Qt's focus
    // system assumes).
    void setKeyboardHighlighted(bool highlighted);

Q_SIGNALS:
    void previewed(const QString &key);
    void previewCanceled();
    void committed(const QString &key);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;

private:
    const Theme *m_theme; // null == system default
    QString m_key;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_keyboardHighlighted = false;
};

// The Qt::Popup panel: a 5x2 grid of ThemeSwatch plus a status line at
// the bottom ("Current: ..." / "Preview: ..."). Fixed white/dark background
// deliberately NOT following the active theme -- it's the color picker,
// coloring it would be circular.
class ThemePickerPopup : public QWidget
{
    Q_OBJECT

public:
    explicit ThemePickerPopup(QWidget *parent = nullptr);

    void setAppliedThemeKey(const QString &key);
    void popupBelow(QWidget *anchor);

Q_SIGNALS:
    void previewRequested(const QString &key);
    void previewCanceled();
    void themeCommitted(const QString &key);

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showPreviewStatus(const QString &previewKey);
    void showAppliedStatus();
    void setKeyboardIndex(int index);
    void commitTheme(const QString &key);

    std::vector<ThemeSwatch *> m_swatches;
    QLabel *m_statusLabel;
    QString m_appliedKey;
    int m_keyboardIndex = -1;
};

// Toolbar entry point: a 30px circular button (paperColor background,
// matching the other icon buttons) with a small 15px bicolor preview
// circle for the currently-applied theme. Opens ThemePickerPopup below
// itself on click.
class ThemePickerButton : public QToolButton
{
    Q_OBJECT

public:
    explicit ThemePickerButton(QWidget *parent = nullptr);

    // theme == nullptr for "system default". paperColor drives this
    // button's own background (matching the other flat circular icon
    // buttons elsewhere in the toolbar).
    void setButtonBackground(const QColor &paperColor);
    void setCurrentThemeKey(const QString &key);

Q_SIGNALS:
    void themePreviewed(const QString &key);
    void previewCanceled();
    void themeCommitted(const QString &key);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void togglePopup();

    QString m_currentKey;
    QColor m_backgroundColor;
    ThemePickerPopup *m_popup = nullptr;
};
