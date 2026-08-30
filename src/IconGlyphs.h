#pragma once

#include <QIcon>
#include <QColor>

// Simple line-art glyphs drawn with QPainter rather than pulled from the
// system icon theme: icon-theme names ("zoom-in", "sidebar-collapse", ...)
// resolved to missing or wrong-looking icons depending on the active theme
// on this GNOME setup, and a themed toolbar needs to recolor its icons to
// match whichever color theme is selected, which theme-provided icons
// generally can't do reliably.
enum class Glyph
{
    ZoomOut,
    ZoomReset,
    ZoomIn,
    SidebarToggle,
    ModePreview,
    ModeSource,
    Save,
    FolderOpen,
    NewNote,
    Trash,
    Rename,
    Star,
};

QIcon makeGlyphIcon(Glyph glyph, const QColor &color, int size = 22);
