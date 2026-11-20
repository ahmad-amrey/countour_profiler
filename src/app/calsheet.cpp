#include "calsheet.h"

#include <QFont>
#include <QLocale>
#include <QPainter>
#include <QPdfWriter>
#include <algorithm>
#include <cmath>

namespace {
// Draw text with a cap height of roughly `mm` millimetres.
void text(QPainter& p, const QRectF& r, int flags, const QString& s, double mm, bool bold, const QColor& c) {
    QFont f;
    f.setFamilies({"Inter", "Segoe UI", "Helvetica Neue", "Arial", "DejaVu Sans"});
    f.setPixelSize(100);  // drawn in a scaled sub-space below
    f.setBold(bold);
    p.save();
    const double k = mm / 70.0;  // 100 px font ~ 70 px cap height
    p.scale(k, k);
    p.setFont(f);
    p.setPen(c);
    p.drawText(QRectF(r.x() / k, r.y() / k, r.width() / k, r.height() / k), flags, s);
    p.restore();
}
}  // namespace

QSizeF sheetSizeMM(QPageSize::PageSizeId page) { return QPageSize(page).size(QPageSize::Millimeter); }

void drawSheet(QPainter& p, QPageSize::PageSizeId pageId) {
    const QSizeF page = sheetSizeMM(pageId);
    const double s = kSheetSquareMM;
    const double margin = 12, header = 12, footer = 22;
    p.fillRect(QRectF(0, 0, page.width(), page.height()), Qt::white);

    // Grid of whole squares, centred.
    const QRectF avail(margin, margin + header, page.width() - 2 * margin, page.height() - 2 * margin - header - footer);
    const int nx = int(std::floor(avail.width() / s)), ny = int(std::floor(avail.height() / s));
    const QRectF g(avail.center().x() - nx * s / 2, avail.center().y() - ny * s / 2, nx * s, ny * s);
    const double lw = 0.35;
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Qt::black, lw, Qt::SolidLine, Qt::FlatCap));
    for (int i = 0; i <= nx; ++i) p.drawLine(QPointF(g.x() + i * s, g.top() - lw / 2), QPointF(g.x() + i * s, g.bottom() + lw / 2));
    for (int j = 0; j <= ny; ++j) p.drawLine(QPointF(g.left() - lw / 2, g.y() + j * s), QPointF(g.right() + lw / 2, g.y() + j * s));

    // Header.
    const QColor ink(40, 40, 40), soft(110, 110, 110);
    const QRectF head(margin, margin, page.width() - 2 * margin, header - 3);
    text(p, head, Qt::AlignLeft | Qt::AlignVCenter, "Contour Profiler · calibration sheet", 3.0, true, ink);
    text(p, head, Qt::AlignRight | Qt::AlignVCenter, "10 mm squares · print at 100 % (actual size)", 2.4, false, soft);

    // Footer: 100 mm check bar and a hint.
    const double fy = page.height() - margin - footer + 6, bx = margin, fullW = page.width() - 2 * margin;
    p.setPen(QPen(ink, 0.3, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(bx, fy + 4), QPointF(bx + 100, fy + 4));
    for (int t = 0; t <= 10; ++t) p.drawLine(QPointF(bx + t * 10, fy + 4 - (t % 5 ? 1.6 : 3.0)), QPointF(bx + t * 10, fy + 4));
    text(p, QRectF(bx, fy + 5.5, fullW, 4), Qt::AlignLeft | Qt::AlignTop,
         "This bar must measure exactly 100 mm. If it does not, enter the measured square size in the app.", 2.0,
         false, soft);
    text(p, QRectF(bx, fy + 10, fullW, 4), Qt::AlignLeft | Qt::AlignTop,
         "Lay the wire inside the grid and photograph it from any angle.", 2.0, false, soft);
}

bool writeSheetPdf(const QString& path, QPageSize::PageSizeId page) {
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(page));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    pdf.setResolution(1200);
    pdf.setTitle("Contour Profiler calibration sheet (10 mm)");
    pdf.setCreator("Contour Profiler");
    QPainter p;
    if (!p.begin(&pdf)) return false;
    const double k = pdf.resolution() / 25.4;  // device units per mm
    p.scale(k, k);
    drawSheet(p, page);
    return p.end();
}

QImage renderSheet(QPageSize::PageSizeId page, double pxPerMM) {
    const QSizeF mm = sheetSizeMM(page);
    QImage img(int(std::lround(mm.width() * pxPerMM)), int(std::lround(mm.height() * pxPerMM)), QImage::Format_RGB888);
    img.fill(Qt::white);
    QPainter p(&img);
    p.scale(pxPerMM, pxPerMM);
    drawSheet(p, page);
    p.end();
    return img;
}

QPageSize::PageSizeId localPaperSize() {
    switch (QLocale::system().territory()) {
        case QLocale::UnitedStates:
        case QLocale::Canada:
        case QLocale::Mexico:
        case QLocale::Philippines:
        case QLocale::Chile:
        case QLocale::Colombia:
        case QLocale::Venezuela:
            return QPageSize::Letter;
        default:
            return QPageSize::A4;
    }
}
