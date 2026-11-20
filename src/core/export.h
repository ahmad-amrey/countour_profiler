// CAD / vector export. Functions return file contents as strings; the caller
// writes them (lets the GUI use Unicode-safe file APIs on every platform).
#pragma once
#include <string>
#include <vector>

#include "geom.h"

namespace cp {

struct ExportPolyline {
    std::vector<Pt> pts;  // millimetres, CAD convention (Y up)
    // Optional arc bulges: bulge[i] bends the segment pts[i] -> pts[i+1]
    // (tan(sweep/4), positive = counter-clockwise). Empty = straight lines.
    std::vector<double> bulge;
    bool closed = false;
    std::string layer = "PROFILE";
    int color = 7;  // AutoCAD colour index
};

// AutoCAD R12 ASCII DXF (AC1009) with POLYLINE entities, units = mm.
// R12 is readable by practically every CAD program (AutoCAD, LibreCAD,
// FreeCAD, Fusion 360, SolidWorks, Inkscape, QCAD, DraftSight...).
std::string makeDXF(const std::vector<ExportPolyline>& polys);
// SVG with a millimetre viewBox (Y flipped back to SVG's downward axis).
std::string makeSVG(const std::vector<ExportPolyline>& polys);
// Simple "x_mm,y_mm" list (one polyline per block, blank line between).
std::string makeCSV(const std::vector<ExportPolyline>& polys);

}  // namespace cp
