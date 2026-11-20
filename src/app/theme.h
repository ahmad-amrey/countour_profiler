// "Graphite" look: dark, photo-first workspace with one warm accent.
#pragma once
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

namespace theme {

inline const QColor bg("#0F1114");        // window
inline const QColor stage("#0B0C0E");     // behind photos
inline const QColor chrome("#16181C");    // header, rail, panel
inline const QColor surface("#1F232A");   // inputs, secondary buttons
inline const QColor raised("#2A2F38");    // selected segment
inline const QColor card("#181A1F");      // floating cards
inline const QColor border("#2E333C");
inline const QColor divider("#262A31");
inline const QColor text("#ECEEF1");
inline const QColor muted("#A2A9B4");
inline const QColor faint("#8A919C");
inline const QColor accent("#FF8A3D");    // profile + primary action
inline const QColor onAccent("#1A1205");
inline const QColor line("#5AA9FF");      // straight segments
inline const QColor ok("#46C38A");
inline const QColor warn("#F2B544");

QFont uiFont(int px, int weight = QFont::Normal);
QString styleSheet();
void apply();  // palette + style sheet + font on the application

// Stroke icons drawn in a 24x24 box (no SVG module needed).
enum class Icon {
    Logo, Camera, Grid, Help, Open, Download, Sliders, Check, Close, Plus, Minus, Fit,
    Pointer, Draw, Target, Pipette, AddPoint, Undo, Redo, ChevronLeft, ChevronRight, More, Warning
};
QIcon icon(Icon which, const QColor& color, int size = 18);
QPixmap pixmap(Icon which, const QColor& color, int size, qreal dpr);

}  // namespace theme
