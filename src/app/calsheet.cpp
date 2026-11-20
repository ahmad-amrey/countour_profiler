#include "calsheet.h"

#include <QFont>
#include <QPainter>
#include <QPdfWriter>
#include <algorithm>
#include <cmath>

namespace {
QFont sheetFont(double mm, bool bold = false) {
    QFont f;
    f.setFamilies({"Inter", "Segoe UI", "Helvetica Neue", "Arial", "DejaVu Sans"});
    f.setPixelSize(100);  // scaled below: fonts are drawn in a 1/100 mm sub-space
    f.setBold(bold);
    (void)mm;
    return f;
}

// Draw text with a cap height of roughly `mm` millimetres.
void text(QPainter& p, const QRectF& r, int flags, const QString& s, double mm, bool bold, const QColor& c) {
    p.save();
    const double k = mm / 70.0;  // 100 px font ~ 70 px cap height
    p.scale(k, k);
    p.setFont(sheetFont(mm, bold));
    p.setPen(c);
    p.drawText(QRectF(r.x() / k, r.y() / k, r.width() / k, r.height() / k), flags, s);
    p.restore();
}
}  // namespace

QSizeF sheetSizeMM(const SheetOptions& o) {
    QSizeF s = QPageSize(o.page).size(QPageSize::Millimeter);
    if (o.landscape) s.transpose();
    return s;
}

void drawSheet(QPainter& p, const SheetOptions& o) {
    const QSizeF page = sheetSizeMM(o);
    const double s = o.squareMM;
    const double margin = 12, header = 12, footer = 22;
    p.fillRect(QRectF(0, 0, page.width(), page.height()), Qt::white);

    // Grid area: whole squares, centred.
    const QRectF avail(margin, margin + header, page.width() - 2 * margin, page.height() - 2 * margin - header - footer);
    int nx = int(std::floor(avail.width() / s)), ny = int(std::floor(avail.height() / s));
    if (o.checkerboard) {
        // Keep a white quiet zone of one square around the board.
        nx = std::max(2, nx - 2);
        ny = std::max(2, ny - 2);
    }
    const double gw = nx * s, gh = ny * s;
    const QRectF g(avail.center().x() - gw / 2, avail.center().y() - gh / 2, gw, gh);

    p.setRenderHint(QPainter::Antialiasing, true);
    if (o.checkerboard) {
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                if ((i + j) & 1) p.drawRect(QRectF(g.x() + i * s, g.y() + j * s, s, s));
    } else {
        const QColor bg = o.dark ? QColor(0, 0, 0) : QColor(255, 255, 255);
        const QColor ln = o.dark ? QColor(235, 235, 235) : QColor(0, 0, 0);
        const double lw = std::clamp(0.035 * s, 0.25, 1.0);  // 0.35 mm for 10 mm squares
        if (o.dark) {
            // Dark field extends half a square beyond the outer lines.
            p.fillRect(g.adjusted(-s / 2, -s / 2, s / 2, s / 2), bg);
        }
        p.setPen(QPen(ln, lw, Qt::SolidLine, Qt::FlatCap));
        for (int i = 0; i <= nx; ++i) p.drawLine(QPointF(g.x() + i * s, g.top() - lw / 2), QPointF(g.x() + i * s, g.bottom() + lw / 2));
        for (int j = 0; j <= ny; ++j) p.drawLine(QPointF(g.left() - lw / 2, g.y() + j * s), QPointF(g.right() + lw / 2, g.y() + j * s));
    }

    // Header.
    const QColor ink(40, 40, 40), soft(110, 110, 110);
    const QString kind = o.checkerboard ? QObject::tr("checkerboard") : QObject::tr("line grid");
    text(p, QRectF(margin, margin, page.width() - 2 * margin, header - 3), Qt::AlignLeft | Qt::AlignVCenter,
         QObject::tr("Contour Profiler · calibration sheet"), 3.0, true, ink);
    text(p, QRectF(margin, margin, page.width() - 2 * margin, header - 3), Qt::AlignRight | Qt::AlignVCenter,
         QObject::tr("%1 mm %2 · print at 100 % (actual size)").arg(s, 0, 'g', 4).arg(kind), 2.4, false, soft);

    // Footer: 100 mm check bar.
    const double fy = page.height() - margin - footer + 6;
    const double barL = 100.0;
    const double bx = margin;
    p.setPen(QPen(ink, 0.3, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(bx, fy + 4), QPointF(bx + barL, fy + 4));
    for (int t = 0; t <= 10; ++t) {
        const double x = bx + t * 10;
        const double h = (t % 5 == 0) ? 3.0 : 1.6;
        p.drawLine(QPointF(x, fy + 4 - h), QPointF(x, fy + 4));
    }
    const double fullW = page.width() - 2 * margin;
    text(p, QRectF(bx, fy + 5.5, fullW, 4), Qt::AlignLeft | Qt::AlignTop,
         QObject::tr("This bar must measure exactly 100 mm. If it does not, set the true square size in the app."),
         2.0, false, soft);
    const QString tip = o.checkerboard ? QObject::tr("Use a dark or coloured wire: it must stand out on both black and white squares.")
                        : o.dark       ? QObject::tr("Best for shiny / silver wire. Keep the wire inside the grid.")
                                       : QObject::tr("Use a dark or coloured wire on white paper. Keep the wire inside the grid.");
    text(p, QRectF(bx, fy + 10, fullW, 4), Qt::AlignLeft | Qt::AlignTop, tip, 2.0, false, soft);
}

bool writeSheetPdf(const QString& path, const SheetOptions& o, QString* error) {
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(o.page));
    pdf.setPageOrientation(o.landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    pdf.setResolution(1200);
    pdf.setTitle(QObject::tr("Contour Profiler calibration sheet"));
    pdf.setCreator("Contour Profiler");
    QPainter p;
    if (!p.begin(&pdf)) {
        if (error) *error = QObject::tr("Cannot write %1").arg(path);
        return false;
    }
    const double k = pdf.resolution() / 25.4;  // device units per mm
    p.scale(k, k);
    drawSheet(p, o);
    p.end();
    return true;
}

QImage renderSheet(const SheetOptions& o, double pxPerMM) {
    const QSizeF mm = sheetSizeMM(o);
    QImage img(int(std::lround(mm.width() * pxPerMM)), int(std::lround(mm.height() * pxPerMM)), QImage::Format_RGB888);
    img.fill(Qt::white);
    QPainter p(&img);
    p.scale(pxPerMM, pxPerMM);
    drawSheet(p, o);
    p.end();
    return img;
}
