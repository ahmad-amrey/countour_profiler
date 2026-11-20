#include "theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>

namespace theme {

QFont uiFont(int px, int weight) {
    QFont f;
    f.setFamilies({"Inter", "Segoe UI Variable Text", "Segoe UI", "SF Pro Text", "Helvetica Neue", "Noto Sans",
                   "Cantarell", "Arial"});
    f.setPixelSize(px);
    f.setWeight(QFont::Weight(weight));
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QString styleSheet() {
    return QStringLiteral(R"QSS(
* { color: #ECEEF1; outline: none; }
QMainWindow, QWidget#Root { background: #0F1114; }
QWidget#Header { background: #16181C; border-bottom: 1px solid #262A31; }
QWidget#Rail { background: #16181C; border-right: 1px solid #262A31; }
QWidget#Panel, QWidget#PanelPage { background: #16181C; }
QScrollArea#PanelScroll { background: #16181C; border: none; border-left: 1px solid #262A31; }
QWidget#Start { background: #0F1114; }
QFrame#Divider { background: #262A31; max-height: 1px; min-height: 1px; border: none; }

QLabel { background: transparent; }
QLabel[role="title"] { font-size: 15px; font-weight: 600; }
QLabel[role="h1"] { font-size: 26px; font-weight: 600; }
QLabel[role="h2"] { font-size: 18px; font-weight: 600; }
QLabel[role="h3"] { font-size: 13px; font-weight: 600; }
QLabel[role="big"] { font-size: 22px; font-weight: 600; }
QLabel[role="muted"] { color: #A2A9B4; font-size: 13px; }
QLabel[role="small"] { color: #A2A9B4; font-size: 12px; }
QLabel[role="faint"] { color: #8A919C; font-size: 12px; }
QLabel[role="chip"] { background: #1F232A; color: #A2A9B4; font-size: 12px; border-radius: 14px; padding: 0 12px; min-height: 28px; max-height: 28px; }
QLabel[role="value"] { font-size: 12px; color: #ECEEF1; }
QLabel[role="hint"] { background: rgba(22,24,28,235); border: 1px solid #2E333C; border-radius: 9px; padding: 8px 12px; color: #A2A9B4; font-size: 12px; }

QPushButton { border-radius: 10px; font-size: 13px; font-weight: 500; padding: 0 14px; min-height: 36px; background: #1F232A; border: 1px solid #2E333C; }
QPushButton:hover { background: #262A31; }
QPushButton:pressed { background: #2A2F38; }
QPushButton:disabled { color: #6B727D; background: #1A1D22; border-color: #262A31; }
QPushButton[variant="primary"] { background: #FF8A3D; color: #1A1205; border: none; font-weight: 600; padding: 0 18px; }
QPushButton[variant="primary"]:hover { background: #FF9A57; }
QPushButton[variant="primary"]:pressed { background: #F07A2D; }
QPushButton[variant="primary"]:disabled { background: #2A2F38; color: #8A919C; }
QPushButton[variant="ghost"] { background: transparent; }
QPushButton[variant="ghost"]:hover { background: #1F232A; }
QPushButton[variant="large"] { min-height: 44px; font-size: 14px; border-radius: 11px; }
QPushButton[variant="link"] { background: transparent; border: none; color: #FFB07A; padding: 0 4px; min-height: 28px; }
QPushButton[variant="link"]:hover { color: #FFC89F; }
QPushButton[variant="chip"] { background: #1F232A; border: none; color: #A2A9B4; font-size: 12px; border-radius: 14px; padding: 0 10px 0 12px; min-height: 28px; max-height: 28px; }
QPushButton[variant="chip"]:hover { background: #2A2F38; color: #ECEEF1; }
QPushButton[variant="flat"] { background: transparent; border: none; color: #A2A9B4; padding: 0 6px; }
QPushButton[variant="flat"]:hover { background: #1F232A; color: #ECEEF1; }
QPushButton[variant="icon"] { background: transparent; border: none; padding: 0; min-height: 34px; min-width: 34px; max-width: 34px; border-radius: 9px; }
QPushButton[variant="icon"]:hover { background: #262A31; }
QToolButton[variant="tool"] { background: transparent; border: none; border-radius: 10px; min-width: 42px; min-height: 42px; max-width: 42px; max-height: 42px; }
QToolButton[variant="tool"]:hover { background: #22262D; }
QToolButton[variant="tool"]:checked { background: #2A2F38; }
QToolButton[variant="tool"]:disabled { background: transparent; }

QFrame#Segmented { background: #121418; border: 1px solid #262A31; border-radius: 11px; }
QFrame#Segmented[floating="true"] { background: rgba(22,24,28,235); border: 1px solid #2E333C; }
QPushButton[variant="seg"] { background: transparent; border: none; border-radius: 8px; color: #A2A9B4; min-height: 32px; padding: 0 14px; }
QPushButton[variant="seg"]:hover { color: #ECEEF1; }
QPushButton[variant="seg"]:checked { background: #2A2F38; color: #ECEEF1; font-weight: 600; }
QFrame#FloatBox { background: rgba(22,24,28,235); border: 1px solid #2E333C; border-radius: 11px; }
QFrame#Card { background: rgba(24,26,31,246); border: 1px solid #2E333C; border-radius: 18px; }
QFrame#DropZone { background: #14161A; border: 2px dashed #3A404A; border-radius: 24px; }
QFrame#DropZone[hover="true"] { border-color: #FF8A3D; background: #1A1714; }
QFrame#IconTile { background: #1F232A; border: 1px solid #2E333C; border-radius: 20px; }
QFrame#Chip { background: #16181C; border: 1px solid #262A31; border-radius: 20px; }
QFrame#Preview { background: #121418; border: 1px solid #262A31; border-radius: 14px; }
QFrame#SelectedRow { background: #22262D; border: 1px solid #3A404A; border-radius: 10px; }

QComboBox, QDoubleSpinBox, QSpinBox, QLineEdit { background: #1F232A; border: 1px solid #2E333C; border-radius: 9px; padding: 0 10px; min-height: 34px; font-size: 13px; selection-background-color: #FF8A3D; selection-color: #1A1205; }
QComboBox:hover, QDoubleSpinBox:hover, QSpinBox:hover, QLineEdit:hover { border-color: #3A404A; }
QComboBox:focus, QDoubleSpinBox:focus, QLineEdit:focus { border-color: #FF8A3D; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #1F232A; border: 1px solid #2E333C; selection-background-color: #2A2F38; selection-color: #ECEEF1; padding: 4px; }
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button, QSpinBox::up-button, QSpinBox::down-button { width: 0; border: none; }

QSlider::groove:horizontal { height: 4px; background: #2E333C; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #FF8A3D; border-radius: 2px; }
QSlider::handle:horizontal { background: #ECEEF1; width: 16px; height: 16px; margin: -6px 0; border-radius: 8px; }
QSlider::handle:horizontal:hover { background: #FFFFFF; }

QCheckBox { font-size: 13px; spacing: 8px; }
QCheckBox[role="small"] { color: #A2A9B4; font-size: 12px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #3A404A; background: #1F232A; }
QCheckBox::indicator:checked { background: #FF8A3D; border-color: #FF8A3D; }

QTableWidget, QListWidget { background: #121418; border: 1px solid #262A31; border-radius: 10px; gridline-color: #262A31; font-size: 12px; }
QTableWidget::item:selected, QListWidget::item:selected { background: #2A2F38; color: #ECEEF1; }
QListWidget::item { border-radius: 8px; padding: 0 6px; }
QListWidget::item:hover { background: #1C1F25; }
QHeaderView::section { background: #16181C; color: #A2A9B4; border: none; border-bottom: 1px solid #262A31; padding: 4px; font-size: 11px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #2E333C; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #2E333C; border-radius: 4px; min-width: 30px; }

QToolTip { background: #1F232A; color: #ECEEF1; border: 1px solid #2E333C; padding: 6px 8px; border-radius: 6px; }
QMenu { background: #1F232A; border: 1px solid #2E333C; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 8px 18px; border-radius: 6px; font-size: 13px; }
QMenu::item:selected { background: #2A2F38; }
QMenu::separator { height: 1px; background: #2E333C; margin: 6px 8px; }
QDialog { background: #1A1D22; }
QMessageBox QLabel { font-size: 13px; }
QStatusBar { background: #16181C; color: #A2A9B4; border-top: 1px solid #262A31; font-size: 12px; }
)QSS");
}

void apply() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, surface);
    p.setColor(QPalette::AlternateBase, chrome);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, surface);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, onAccent);
    p.setColor(QPalette::ToolTipBase, surface);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, faint);
    p.setColor(QPalette::Disabled, QPalette::Text, faint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, faint);
    QApplication::setPalette(p);
    QApplication::setFont(uiFont(13));
    qApp->setStyleSheet(styleSheet());
}

// --- icons -------------------------------------------------------------------
QPixmap pixmap(Icon which, const QColor& color, int size, qreal dpr) {
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    auto dot = [&](double x, double y, double r) {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(x, y), r, r);
        p.restore();
    };
    switch (which) {
        case Icon::Logo:
            p.setPen(QPen(QColor("#2E333C"), 1.2));
            p.setBrush(QColor("#1F232A"));
            p.drawRoundedRect(QRectF(1, 1, 22, 22), 6.5, 6.5);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            path.moveTo(7.4, 17.5);
            path.lineTo(7.4, 10.2);
            path.quadTo(7.4, 6.5, 11, 6.5);
            path.quadTo(16.6, 6.5, 17.5, 12);
            path.lineTo(17.5, 17.5);
            p.drawPath(path);
            break;
        case Icon::Camera:
            p.drawRoundedRect(QRectF(3, 5, 18, 14), 3, 3);
            p.drawEllipse(QPointF(12, 12), 3.5, 3.5);
            path.moveTo(8, 5); path.lineTo(9.5, 3); path.lineTo(14.5, 3); path.lineTo(16, 5);
            p.drawPath(path);
            break;
        case Icon::Grid:
            p.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
            p.drawLine(QPointF(3, 9), QPointF(21, 9)); p.drawLine(QPointF(3, 15), QPointF(21, 15));
            p.drawLine(QPointF(9, 3), QPointF(9, 21)); p.drawLine(QPointF(15, 3), QPointF(15, 21));
            break;
        case Icon::Help:
            p.drawEllipse(QPointF(12, 12), 9, 9);
            path.moveTo(9.5, 9.5);
            path.cubicTo(9.5, 7.6, 11, 7, 12, 7);
            path.cubicTo(13.6, 7, 14.5, 8.2, 14.5, 9.4);
            path.cubicTo(14.5, 11.2, 12, 11.4, 12, 13.6);
            p.drawPath(path);
            dot(12, 17.3, 1.1);
            break;
        case Icon::Open:
            path.moveTo(3, 7); path.lineTo(3, 18); path.quadTo(3, 19, 4, 19); path.lineTo(20, 19);
            path.quadTo(21, 19, 21, 18); path.lineTo(21, 9); path.quadTo(21, 8, 20, 8); path.lineTo(11, 8);
            path.lineTo(9, 5); path.lineTo(4, 5); path.quadTo(3, 5, 3, 6); path.closeSubpath();
            p.drawPath(path);
            break;
        case Icon::Download:
            p.drawLine(QPointF(12, 4), QPointF(12, 15));
            path.moveTo(7, 10); path.lineTo(12, 15); path.lineTo(17, 10);
            p.drawPath(path);
            p.drawLine(QPointF(5, 20), QPointF(19, 20));
            break;
        case Icon::Sliders:
            p.drawLine(QPointF(4, 7), QPointF(14, 7)); p.drawLine(QPointF(18, 7), QPointF(20, 7));
            p.drawLine(QPointF(4, 17), QPointF(8, 17)); p.drawLine(QPointF(12, 17), QPointF(20, 17));
            p.drawEllipse(QPointF(16, 7), 2, 2); p.drawEllipse(QPointF(10, 17), 2, 2);
            break;
        case Icon::Check:
            p.setPen(QPen(color, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            path.moveTo(5, 12.5); path.lineTo(9.5, 17); path.lineTo(19, 7.5);
            p.drawPath(path);
            break;
        case Icon::Close:
            p.drawLine(QPointF(6, 6), QPointF(18, 18)); p.drawLine(QPointF(18, 6), QPointF(6, 18));
            break;
        case Icon::Plus:
            p.drawLine(QPointF(12, 5), QPointF(12, 19)); p.drawLine(QPointF(5, 12), QPointF(19, 12));
            break;
        case Icon::Minus:
            p.drawLine(QPointF(5, 12), QPointF(19, 12));
            break;
        case Icon::Fit:
            path.moveTo(4, 9); path.lineTo(4, 4); path.lineTo(9, 4);
            path.moveTo(20, 9); path.lineTo(20, 4); path.lineTo(15, 4);
            path.moveTo(4, 15); path.lineTo(4, 20); path.lineTo(9, 20);
            path.moveTo(20, 15); path.lineTo(20, 20); path.lineTo(15, 20);
            p.drawPath(path);
            break;
        case Icon::Pointer:
            path.moveTo(5, 3); path.lineTo(19, 11); path.lineTo(13, 12.5); path.lineTo(10, 19); path.closeSubpath();
            p.drawPath(path);
            break;
        case Icon::Draw:
            path.moveTo(4, 20); path.cubicTo(7, 12, 10, 8, 13, 8); path.cubicTo(16, 8, 17, 11, 20, 8);
            p.drawPath(path);
            p.drawEllipse(QPointF(4, 20), 1.5, 1.5); p.drawEllipse(QPointF(20, 8), 1.5, 1.5);
            break;
        case Icon::Target:
            p.drawEllipse(QPointF(12, 12), 7, 7);
            p.drawLine(QPointF(12, 2), QPointF(12, 6)); p.drawLine(QPointF(12, 18), QPointF(12, 22));
            p.drawLine(QPointF(2, 12), QPointF(6, 12)); p.drawLine(QPointF(18, 12), QPointF(22, 12));
            break;
        case Icon::Pipette:
            path.moveTo(14, 4); path.lineTo(20, 10); path.lineTo(11, 19); path.lineTo(5, 19); path.lineTo(5, 13);
            path.closeSubpath();
            p.drawPath(path);
            p.drawLine(QPointF(12, 6), QPointF(18, 12));
            break;
        case Icon::AddPoint:
            p.drawEllipse(QPointF(12, 12), 4, 4);
            p.drawLine(QPointF(12, 3), QPointF(12, 6)); p.drawLine(QPointF(12, 18), QPointF(12, 21));
            p.drawLine(QPointF(3, 12), QPointF(6, 12)); p.drawLine(QPointF(18, 12), QPointF(21, 12));
            break;
        case Icon::Undo:
        case Icon::Redo: {
            if (which == Icon::Redo) { p.translate(24, 0); p.scale(-1, 1); }
            path.moveTo(9, 7); path.lineTo(4, 7); path.lineTo(4, 2);
            p.drawPath(path);
            QPainterPath arc;
            arc.moveTo(4, 7);
            arc.arcTo(QRectF(4, 4, 16, 16), 160, -290);
            p.drawPath(arc);
            break;
        }
        case Icon::ChevronLeft:
            path.moveTo(15, 6); path.lineTo(9, 12); path.lineTo(15, 18);
            p.drawPath(path);
            break;
        case Icon::ChevronRight:
            path.moveTo(9, 6); path.lineTo(15, 12); path.lineTo(9, 18);
            p.drawPath(path);
            break;
        case Icon::More:
            dot(5.5, 12, 1.7); dot(12, 12, 1.7); dot(18.5, 12, 1.7);
            break;
        case Icon::Warning:
            path.moveTo(12, 4); path.lineTo(21, 20); path.lineTo(3, 20); path.closeSubpath();
            p.drawPath(path);
            p.drawLine(QPointF(12, 10), QPointF(12, 14));
            dot(12, 17, 1.0);
            break;
    }
    return pm;
}

QIcon icon(Icon which, const QColor& color, int size) {
    QIcon ic;
    for (qreal dpr : {1.0, 2.0}) ic.addPixmap(pixmap(which, color, size, dpr));
    return ic;
}

}  // namespace theme
