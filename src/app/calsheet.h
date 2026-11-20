// Printable calibration sheets (line grid or checkerboard) that the detector
// reads reliably. Drawn with QPainter, so the same code renders the PDF and
// preview/test images.
#pragma once
#include <QImage>
#include <QPageSize>
#include <QString>

struct SheetOptions {
    bool checkerboard = false;
    bool dark = false;          // light lines on black (best for silver wire); line grid only
    double squareMM = 10;
    QPageSize::PageSizeId page = QPageSize::A4;
    bool landscape = false;
};

QSizeF sheetSizeMM(const SheetOptions& o);
// Draws the whole page; the painter is set up so 1 unit = 1 mm.
void drawSheet(class QPainter& p, const SheetOptions& o);
bool writeSheetPdf(const QString& path, const SheetOptions& o, QString* error = nullptr);
QImage renderSheet(const SheetOptions& o, double pxPerMM);
