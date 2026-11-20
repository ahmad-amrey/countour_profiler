// Reverse engineering of clean CAD geometry from points: a polyline (traced
// or clicked) is rebuilt as the fewest straight lines and circular arcs that
// stay within a tolerance. Segments share their end points (continuous).
#pragma once
#include <cmath>
#include <vector>

#include "geom.h"

namespace cp {

struct Segment {
    bool arc = false;
    Pt a, b;            // end points
    Pt c;               // arc centre
    double r = 0;       // arc radius
    double sweep = 0;   // arc signed sweep (radians, in the input's coordinate system)
    int i0 = 0, i1 = 0; // source vertex range
    double maxErr = 0;  // largest deviation of the source vertices (mm)

    double length() const { return arc ? std::fabs(sweep) * r : dist(a, b); }
    // Bulge for polyline export (tan of a quarter of the sweep).
    double bulge() const { return arc ? std::tan(sweep / 4) : 0; }
    Pt pointAt(double t) const;  // t in [0, 1]
};

struct FitOptions {
    double tol = 0.05;         // max deviation of any source vertex (same units as points)
    double maxRadius = 5000;   // larger arcs become lines
    double arcPenalty = 0.15;  // an arc costs (1 + penalty) segments: lines win ties
};

std::vector<Segment> fitLinesArcs(const std::vector<Pt>& pts, bool closed, const FitOptions& opt = {});

// Dense points along the segments (for drawing), at most maxStep apart.
std::vector<Pt> sampleSegments(const std::vector<Segment>& segs, double maxStep);

}  // namespace cp
