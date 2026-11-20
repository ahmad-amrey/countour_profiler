// Automatic detection of a planar calibration grid (checkerboard or a ruled
// line grid such as a cutting mat / graph paper) in a perspective photo.
#pragma once
#include <string>
#include <vector>

#include "geom.h"
#include "image.h"

namespace cp {

enum class PatternType { LineGrid = 0, Checkerboard = 1 };

struct GridNode {
    int i = 0, j = 0;  // lattice index (world = i*square, j*square)
    Pt img;            // sub-pixel position in the full-resolution image
};

struct GridResult {
    bool ok = false;
    std::vector<GridNode> nodes;
    std::vector<Pt> candidates;  // all corner candidates (for snapping in the UI)
    std::vector<std::pair<Pt, Pt>> lines;  // detected straight lines (line grids, diagnostics)
    Mat3 latticeToImage;         // (i, j) -> image
    double rmsPx = 0;            // homography fit residual
    double stepPx = 0;           // median lattice step in pixels
    int polarity = 0;            // +1 bright lines, -1 dark lines, 0 checkerboard
    std::string message;
};

// grayFull: full-resolution grey image (0..255).
GridResult detectGrid(const ImageF& grayFull, PatternType type);

}  // namespace cp
