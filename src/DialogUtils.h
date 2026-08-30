#pragma once

class QWidget;

// SlideWindow is pinned above everything else -- KX11Extras::KeepAbove
// on X11, LayerShellQt's Overlay compositor layer on Wayland. A plain
// QDialog only gets Qt::WindowStaysOnTopHint, which is enough on X11
// but does essentially nothing on Wayland: regular xdg-toplevel windows
// (which is all a QDialog ever is) paint in a tier strictly below any
// layer-shell surface, Overlay included, no matter what window flags
// are set on them. There's no per-window "above everything" hint on
// Wayland by design. So on Wayland this also promotes the dialog itself
// to an Overlay-layer surface (same trick as the main window), which is
// the only way to make it paint above our own pinned window; on X11 it
// just sets the same KeepAbove-equivalent hint SlideWindow uses.
//
// Call this after constructing a dialog and before exec()/show().
void pinDialogAboveSlideWindow(QWidget *dialog);
