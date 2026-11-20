// The printable calibration sheet: black 10 mm lines on white paper, which the
// detector reads reliably. Drawn with QPainter, so the same code renders the
// PDF and test images.
#pragma once
#include <QImage>
#include <QPageSize>
#include <QString>

constexpr double kSheetSquareMM = 10.0;

QSizeF sheetSizeMM(QPageSize::PageSizeId page);
// Draws the whole page; the painter is set up so 1 unit = 1 mm.
void drawSheet(class QPainter& p, QPageSize::PageSizeId page);
bool writeSheetPdf(const QString& path, QPageSize::PageSizeId page);
QImage renderSheet(QPageSize::PageSizeId page, double pxPerMM);
// US Letter in North America, A4 elsewhere.
QPageSize::PageSizeId localPaperSize();
