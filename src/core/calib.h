// Calibration helpers shared by the GUI and the command-line tools.
#pragma once
#include <vector>

#include "geom.h"
#include "grid_detect.h"

namespace cp {

struct Calibration {
    bool ok = false;
    Mat3 Hw2i, Hi2w;                  // world (mm) <-> image (px)
    double rmsMM = 0;                 // reference-point residual RMS
    std::vector<double> residualsMM;  // per point
};

// img[i] <-> world[i]; needs >= 4 points, not all collinear.
Calibration calibrate(const std::vector<Pt>& img, const std::vector<Pt>& world);

// Bounding box of points, grown by margin.
RectD boundingBox(const std::vector<Pt>& pts, double margin);

// Image pixels per world millimetre at a world location.
double pxPerMM(const Mat3& Hw2i, Pt world);

// World coordinates for a detected grid node.
inline Pt gridWorld(const GridNode& n, double squareMM) { return {n.i * squareMM, n.j * squareMM}; }

}  // namespace cp
